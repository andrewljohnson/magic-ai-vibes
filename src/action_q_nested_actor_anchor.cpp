#include "old_school/action_q_nested_actor_anchor.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_dev_evaluator.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <numeric>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::action_q_nested_actor_anchor {
namespace {

namespace bundle = fq4_dev_bundle;
namespace dev = fq4_dev_evaluator;
namespace integrity = artifact_integrity;

constexpr std::array<std::size_t, kDeckCount>
    kNeutralDevOptionsByDeck{82, 86, 80, 103, 87};

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(
        "AQ4-G3 " + std::string(message));
}

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        throw std::out_of_range(
            "AQ4-G3 owner deck is invalid");
    }
    return result;
}

bool finite(double value) {
    return std::isfinite(value);
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

void digest_size(
    integrity::Sha256Accumulator& digest,
    std::size_t value) {
    digest_u64(
        digest, static_cast<std::uint64_t>(value));
}

void digest_double(
    integrity::Sha256Accumulator& digest,
    double value) {
    digest_u64(
        digest, std::bit_cast<std::uint64_t>(value));
}

void digest_string(
    integrity::Sha256Accumulator& digest,
    std::string_view value) {
    digest_size(digest, value.size());
    digest.update(value);
}

bool valid_training_example(
    const LearnedValuePriorityTrainingExample& example) {
    if (example.options.size() < 2 ||
        example.base_scores.size() !=
            example.options.size() ||
        example.target_probabilities.size() !=
            example.options.size() ||
        !finite(example.weight) ||
        example.weight <= 0.0) {
        return false;
    }
    long double target_total = 0.0L;
    for (std::size_t option = 0;
         option < example.options.size(); ++option) {
        if (example.options[option].size() !=
                g1::kPolicyFeatureCount ||
            !std::all_of(
                example.options[option].begin(),
                example.options[option].end(),
                finite) ||
            !finite(example.base_scores[option]) ||
            !finite(
                example.target_probabilities[option]) ||
            example.target_probabilities[option] <
                0.0 ||
            example.target_probabilities[option] >
                1.0) {
            return false;
        }
        target_total +=
            static_cast<long double>(
                example.target_probabilities[option]);
    }
    return std::abs(target_total - 1.0L) <=
           1.0e-9L;
}

std::size_t option_count(
    std::span<const g1::RootExample> examples) {
    return std::accumulate(
        examples.begin(), examples.end(),
        std::size_t{0},
        [](std::size_t total,
           const g1::RootExample& example) {
            if (example.manifest.actions.size() >
                std::numeric_limits<std::size_t>::max() -
                    total) {
                throw std::overflow_error(
                    "AQ4-G3 option count overflow");
            }
            return total +
                   example.manifest.actions.size();
        });
}

std::size_t neutral_option_count(
    std::span<
        const neutral_eval::PreparedNeutralRow> rows) {
    return std::accumulate(
        rows.begin(), rows.end(), std::size_t{0},
        [](std::size_t total,
           const neutral_eval::PreparedNeutralRow& row) {
            if (row.actions.size() >
                std::numeric_limits<std::size_t>::max() -
                    total) {
                throw std::overflow_error(
                    "AQ4-G3 neutral option count overflow");
            }
            return total + row.actions.size();
        });
}

std::vector<std::vector<double>> neutral_options(
    const neutral_eval::PreparedNeutralRow& row) {
    std::vector<std::vector<double>> result;
    result.reserve(row.actions.size());
    for (const auto& action : row.actions) {
        result.push_back(action.features);
    }
    return result;
}

std::vector<double> neutral_base_scores(
    const neutral_eval::PreparedNeutralRow& row) {
    std::vector<double> result;
    result.reserve(row.actions.size());
    for (const auto& action : row.actions) {
        result.push_back(action.base_score);
    }
    return result;
}

std::vector<double> neutral_parent_scores(
    const neutral_eval::PreparedNeutralRow& row) {
    std::vector<double> result;
    result.reserve(row.actions.size());
    for (const auto& action : row.actions) {
        const double score =
            action.base_score +
            action.parent_residual;
        if (!finite(score)) {
            fail("neutral parent score is nonfinite");
        }
        result.push_back(score);
    }
    return result;
}

bool exact_file_identity(
    const integrity::RegularFileSnapshot& snapshot,
    const neutral_artifact::FileIdentity& expected) {
    return snapshot.byte_size == expected.bytes &&
           snapshot.sha256 == expected.sha256;
}

bool exact_source_identity(
    const SourceIdentity& identity) {
    return identity == required_source_identity();
}

void require_parent(
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            g1::kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ4-G3 requires exact frozen C16");
    }
}

void add_failure(
    GateReport& report, bool condition,
    std::string_view message) {
    if (!condition) {
        report.failures.emplace_back(message);
    }
}

bool fit_identity_exact(
    const g1::FitReport& fit,
    std::size_t expected_examples,
    std::size_t expected_options) {
    return fit.candidate &&
           fit.parent_fingerprint_before ==
               g1::kRequiredParentFingerprint &&
           fit.parent_fingerprint_after ==
               g1::kRequiredParentFingerprint &&
           fit.candidate_fingerprint ==
               learned_model_fingerprint(fit.candidate) &&
           fit.candidate_components ==
               learned_model_component_fingerprints(
                   fit.candidate) &&
           fit.optimizer == g1::optimizer_config() &&
           fit.fit_examples == expected_examples &&
           fit.fit_options == expected_options &&
           fit.parent_immutable &&
           fit.repeated_fit_bit_identical &&
           fit.only_priority_component_changed;
}

bool offline_bound_to_fit(
    const g1::OfflineReport& offline,
    const g1::FitReport& fit) {
    return fit.candidate &&
           offline.parent_fingerprint ==
               g1::kRequiredParentFingerprint &&
           offline.candidate_fingerprint ==
               fit.candidate_fingerprint &&
           offline.parent_fit == fit.parent_fit &&
           offline.candidate_fit ==
               fit.candidate_fit &&
           offline.parent_check ==
               fit.parent_check &&
           offline.candidate_check ==
               fit.candidate_check &&
           offline.parent_immutable ==
               fit.parent_immutable &&
           offline.repeated_fit_bit_identical ==
               fit.repeated_fit_bit_identical &&
           offline.only_priority_component_changed ==
               fit.only_priority_component_changed;
}

} // namespace

SourceIdentity required_source_identity() {
    return {
        .corpus_digest =
            std::string(kRequiredCorpusDigest),
        .dev1_bundle = {
            .bytes = bundle::kPublishedArtifactBytes,
            .sha256 =
                std::string(
                    bundle::kPublishedArtifactSha256),
        },
        .parent_artifact = {
            .bytes = kParentArtifactBytes,
            .sha256 =
                std::string(
                    bundle::kParentArtifactSha256),
        },
        .parent_model_fingerprint =
            std::string(g1::kRequiredParentFingerprint),
        .neutral_file = {
            .bytes = kNeutralArtifactBytes,
            .sha256 =
                std::string(kNeutralArtifactSha256),
        },
        .neutral_selected_order_sha256 =
            std::string(
                kNeutralSelectedOrderSha256),
    };
}

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments) {
    if (arguments.size() == 1 &&
        arguments.front() == "--run") {
        return Command::Run;
    }
    return std::nullopt;
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-action-q-nested-actor-anchor "
           "--run\n";
}

std::string canonical_training_digest(
    const TrainingBatch& batch,
    const LearnedValuePriorityHeadUpdateConfig& optimizer) {
    if (batch.examples.size() !=
            batch.sources.size() ||
        batch.examples.size() !=
            batch.owner_decks.size()) {
        throw std::invalid_argument(
            "AQ4-G3 digest row vectors disagree");
    }
    integrity::Sha256Accumulator digest;
    digest_string(
        digest,
        "old-school-action-q-aq4-g3-mixed-fit-v1");
    const SourceIdentity& identity =
        batch.source_identity;
    digest_string(digest, identity.corpus_digest);
    digest_u64(digest, identity.dev1_bundle.bytes);
    digest_string(
        digest, identity.dev1_bundle.sha256);
    digest_u64(
        digest, identity.parent_artifact.bytes);
    digest_string(
        digest, identity.parent_artifact.sha256);
    digest_string(
        digest, identity.parent_model_fingerprint);
    digest_string(
        digest, bundle::kParentCriticFingerprint);
    digest_string(
        digest, bundle::kParentPriorityFingerprint);
    digest_string(
        digest, bundle::kParentAttackFingerprint);
    digest_string(
        digest, bundle::kParentBlockFingerprint);
    digest_string(
        digest, bundle::kParentDamageOrderFingerprint);
    digest_u64(digest, identity.neutral_file.bytes);
    digest_string(
        digest, identity.neutral_file.sha256);
    digest_string(
        digest,
        identity.neutral_selected_order_sha256);

    digest_size(digest, optimizer.batch_size);
    digest_size(digest, optimizer.epochs);
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

    digest_size(digest, batch.examples.size());
    for (std::size_t row = 0;
         row < batch.examples.size(); ++row) {
        digest_u64(
            digest,
            static_cast<std::uint64_t>(
                batch.sources[row]));
        digest_u64(
            digest,
            static_cast<std::uint64_t>(
                batch.owner_decks[row]));
        const auto& example = batch.examples[row];
        digest_size(digest, example.options.size());
        for (const auto& option : example.options) {
            digest_size(digest, option.size());
            for (const double value : option) {
                digest_double(digest, value);
            }
        }
        digest_size(
            digest, example.base_scores.size());
        for (const double value :
             example.base_scores) {
            digest_double(digest, value);
        }
        digest_size(
            digest,
            example.target_probabilities.size());
        for (const double value :
             example.target_probabilities) {
            digest_double(digest, value);
        }
        digest_double(digest, example.weight);
    }
    return digest.finish();
}

TrainingBatch build_training_batch(
    std::span<const TrainingRow> rows,
    const LearnedValuePriorityHeadUpdateConfig& optimizer) {
    return build_training_batch(
        rows, optimizer, required_source_identity());
}

TrainingBatch build_training_batch(
    std::span<const TrainingRow> rows,
    const LearnedValuePriorityHeadUpdateConfig& optimizer,
    SourceIdentity source_identity) {
    if (rows.empty()) {
        throw std::invalid_argument(
            "AQ4-G3 training batch is empty");
    }
    TrainingBatch result;
    result.examples.reserve(rows.size());
    result.sources.reserve(rows.size());
    result.owner_decks.reserve(rows.size());
    result.source_identity =
        std::move(source_identity);
    bool saw_anchor = false;
    for (const TrainingRow& row : rows) {
        const std::size_t deck =
            deck_index(row.owner_deck);
        if (!valid_training_example(row.example)) {
            throw std::invalid_argument(
                "AQ4-G3 training example is malformed");
        }
        switch (row.source) {
        case TrainingSourceKind::Teacher:
            if (saw_anchor) {
                throw std::invalid_argument(
                    "AQ4-G3 teacher followed an anchor");
            }
            ++result.teacher_examples;
            result.teacher_options +=
                row.example.options.size();
            result.teacher_loss_mass_by_deck[deck] +=
                row.example.weight;
            break;
        case TrainingSourceKind::Anchor:
            saw_anchor = true;
            ++result.anchor_examples;
            result.anchor_options +=
                row.example.options.size();
            result.anchor_loss_mass_by_deck[deck] +=
                row.example.weight;
            break;
        default:
            throw std::invalid_argument(
                "AQ4-G3 training source is invalid");
        }
        result.examples.push_back(row.example);
        result.sources.push_back(row.source);
        result.owner_decks.push_back(row.owner_deck);
    }
    result.digest =
        canonical_training_digest(result, optimizer);
    return result;
}

TrainingBatch build_training_batch(
    const g1::Corpus& corpus,
    const neutral_eval::PreparedNeutralCorpus& neutral,
    const LearnedValuePriorityHeadUpdateConfig& optimizer,
    bool include_anchors) {
    g1::validate_corpus(corpus);
    g1::require_frozen_census(corpus.census);
    return testing::build_training_batch_from_splits(
        corpus.fit, corpus.check, neutral, optimizer,
        include_anchors);
}

TrainingBatch
testing::build_training_batch_from_splits(
    std::span<const g1::RootExample> teacher_fit,
    std::span<const g1::RootExample> teacher_dev,
    const neutral_eval::PreparedNeutralCorpus& neutral,
    const LearnedValuePriorityHeadUpdateConfig& optimizer,
    bool include_anchors) {
    if (optimizer != g1::optimizer_config() ||
        teacher_fit.size() != kTeacherExamples ||
        option_count(teacher_fit) != kTeacherOptions ||
        teacher_dev.size() != kDevExamples ||
        option_count(teacher_dev) != kDevOptions) {
        throw std::invalid_argument(
            "AQ4-G3 G1 corpus or optimizer drifted");
    }
    if (neutral.fit.size() != kAnchorExamples ||
        neutral_option_count(neutral.fit) !=
            kAnchorOptions ||
        neutral.check.size() !=
            kNeutralDevExamples ||
        neutral_option_count(neutral.check) !=
            kNeutralDevOptions) {
        throw std::invalid_argument(
            "AQ4-G3 neutral corpus census drifted");
    }
    std::vector<TrainingRow> rows;
    rows.reserve(
        kTeacherExamples +
        (include_anchors ? kAnchorExamples : 0U));
    for (const g1::RootExample& root : teacher_fit) {
        rows.push_back({
            .owner_deck =
                root.manifest.coordinate.owner_deck(),
            .source = TrainingSourceKind::Teacher,
            .example = {
                .options = root.manifest.options,
                .base_scores = root.base_scores,
                .target_probabilities =
                    root.target_probabilities,
                .weight = root.weight,
            },
        });
    }
    if (include_anchors) {
        for (std::size_t index = 0;
             index < neutral.fit.size(); ++index) {
            const auto& row = neutral.fit[index];
            const std::size_t expected_deck =
                index / kRowsPerDeck;
            if (row.split != bundle::Split::Fit ||
                row.owner_deck != expected_deck) {
                throw std::invalid_argument(
                    "AQ4-G3 neutral FIT order drifted");
            }
            rows.push_back({
                .owner_deck =
                    static_cast<DeckId>(
                        row.owner_deck),
                .source = TrainingSourceKind::Anchor,
                .example = {
                    .options = neutral_options(row),
                    .base_scores =
                        neutral_base_scores(row),
                    .target_probabilities =
                        neutral_eval::
                            neutral_behavior_target(row),
                    .weight = kAnchorRowWeight,
                },
            });
        }
    }
    TrainingBatch result =
        build_training_batch(rows, optimizer);
    if (!result.valid(include_anchors)) {
        throw std::runtime_error(
            "AQ4-G3 production training batch is invalid");
    }
    return result;
}

bool TrainingBatch::valid(bool expect_anchors) const {
    try {
        if (!exact_source_identity(source_identity) ||
            examples.size() != sources.size() ||
            examples.size() != owner_decks.size() ||
            teacher_examples != kTeacherExamples ||
            teacher_options != kTeacherOptions ||
            anchor_examples !=
                (expect_anchors ? kAnchorExamples : 0U) ||
            anchor_options !=
                (expect_anchors ? kAnchorOptions : 0U) ||
            examples.size() !=
                kTeacherExamples +
                    (expect_anchors
                         ? kAnchorExamples
                         : 0U)) {
            return false;
        }
        std::array<std::size_t, kDeckCount>
            teacher_counts{};
        std::array<std::size_t, kDeckCount>
            anchor_counts{};
        std::array<double, kDeckCount>
            teacher_mass{};
        std::array<double, kDeckCount> anchor_mass{};
        std::size_t observed_teacher_options = 0;
        std::size_t observed_anchor_options = 0;
        bool saw_anchor = false;
        for (std::size_t row = 0;
             row < examples.size(); ++row) {
            if (!valid_training_example(examples[row])) {
                return false;
            }
            const std::size_t deck =
                deck_index(owner_decks[row]);
            if (sources[row] ==
                TrainingSourceKind::Teacher) {
                if (saw_anchor ||
                    examples[row].weight !=
                        kTeacherRowWeight) {
                    return false;
                }
                ++teacher_counts[deck];
                teacher_mass[deck] +=
                    examples[row].weight;
                observed_teacher_options +=
                    examples[row].options.size();
            } else if (
                sources[row] ==
                TrainingSourceKind::Anchor) {
                saw_anchor = true;
                if (!expect_anchors ||
                    examples[row].weight !=
                        kAnchorRowWeight) {
                    return false;
                }
                ++anchor_counts[deck];
                anchor_mass[deck] +=
                    examples[row].weight;
                observed_anchor_options +=
                    examples[row].options.size();
            } else {
                return false;
            }
        }
        if (observed_teacher_options !=
                teacher_options ||
            observed_anchor_options !=
                anchor_options ||
            teacher_mass !=
                teacher_loss_mass_by_deck ||
            anchor_mass !=
                anchor_loss_mass_by_deck) {
            return false;
        }
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            if (teacher_counts[deck] != 64 ||
                teacher_mass[deck] != 1.0 ||
                anchor_counts[deck] !=
                    (expect_anchors ? 32U : 0U) ||
                anchor_mass[deck] !=
                    (expect_anchors ? 1.0 : 0.0)) {
                return false;
            }
        }
        return digest ==
               canonical_training_digest(
                   *this, g1::optimizer_config());
    } catch (const std::exception&) {
        return false;
    }
}

bool control_matches_frozen_result(
    const g1::FitReport& fit) {
    return g2::control_matches_frozen_result(fit) &&
           fit.candidate_fingerprint ==
               kRequiredControlFingerprint;
}

bool ControlReport::gate_passed() const {
    const bool observed_fingerprint =
        fit.candidate &&
        fit.candidate_fingerprint ==
            kRequiredControlFingerprint &&
        learned_model_fingerprint(fit.candidate) ==
            kRequiredControlFingerprint;
    const bool observed_metrics =
        control_matches_frozen_result(fit);
    const bool observed_identity =
        fit_identity_exact(
            fit, kTeacherExamples,
            kTeacherOptions);
    return corpus_digest_exact &&
           fingerprint_exact &&
           fingerprint_exact ==
               observed_fingerprint &&
           aggregate_metrics_bit_exact &&
           aggregate_metrics_bit_exact ==
               observed_metrics &&
           observed_metrics &&
           observed_identity;
}

neutral_eval::NeutralDriftMetrics score_neutral_check(
    const neutral_eval::PreparedNeutralCorpus& neutral,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> unanchored,
    std::shared_ptr<const LearnedModel> anchored) {
    require_parent(parent);
    if (!unanchored || !anchored ||
        learned_model_fingerprint(unanchored) !=
            kRequiredControlFingerprint ||
        neutral.check.size() !=
            kNeutralDevExamples ||
        neutral_option_count(neutral.check) !=
            kNeutralDevOptions) {
        throw std::invalid_argument(
            "AQ4-G3 neutral scorer inputs drifted");
    }
    const std::string parent_before =
        learned_model_fingerprint(parent);
    const std::string unanchored_before =
        learned_model_fingerprint(unanchored);
    const std::string anchored_before =
        learned_model_fingerprint(anchored);
    std::vector<neutral_eval::NeutralScoreTriplet>
        triplets;
    triplets.reserve(neutral.check.size());
    for (std::size_t index = 0;
         index < neutral.check.size(); ++index) {
        const auto& row = neutral.check[index];
        const std::size_t expected_deck =
            index / kRowsPerDeck;
        if (row.split != bundle::Split::Check ||
            row.owner_deck != expected_deck) {
            throw std::invalid_argument(
                "AQ4-G3 neutral DEV order drifted");
        }
        const auto options = neutral_options(row);
        const auto base = neutral_base_scores(row);
        const auto baseline_logits =
            learned_policy_head_logits(
                options,
                LearnedPolicyDecisionKind::Priority,
                unanchored);
        const auto anchored_logits =
            learned_policy_head_logits(
                options,
                LearnedPolicyDecisionKind::Priority,
                anchored);
        triplets.push_back({
            .owner_deck = row.owner_deck,
            .parent_combined_scores =
                neutral_parent_scores(row),
            .baseline_combined_scores =
                action_q_explore::combined_scores(
                    base, baseline_logits,
                    g1::kCandidateResidualWeight),
            .anchored_combined_scores =
                action_q_explore::combined_scores(
                    base, anchored_logits,
                    g1::kCandidateResidualWeight),
        });
    }
    const auto result =
        neutral_eval::measure_neutral_check(triplets);
    if (learned_model_fingerprint(parent) !=
            parent_before ||
        learned_model_fingerprint(unanchored) !=
            unanchored_before ||
        learned_model_fingerprint(anchored) !=
            anchored_before) {
        fail("neutral scoring mutated a model");
    }
    return result;
}

bool GateReport::passed() const {
    return failures.empty() &&
           source_identity_exact &&
           control_exact &&
           mixed_batch_exact &&
           repeated_fit_bit_identical &&
           parent_immutable &&
           only_priority_component_changed &&
           g1_offline_passed &&
           ancestral_passed &&
           neutral_baseline_nonzero &&
           neutral_per_deck_nonworsening &&
           neutral_kl_halved &&
           neutral_support_changes_halved;
}

GateReport evaluate_gate(
    bool source_identity_exact,
    const ControlReport& control,
    const TrainingBatch& anchored_training,
    const g1::FitReport& anchored_fit,
    const g1::OfflineReport& anchored_offline,
    const neutral_eval::NeutralDriftMetrics&
        neutral_dev) {
    GateReport report;
    report.source_identity_exact =
        source_identity_exact;
    report.control_exact = control.gate_passed();
    report.mixed_batch_exact =
        anchored_training.valid(true) &&
        fit_identity_exact(
            anchored_fit, kAnchoredExamples,
            kAnchoredOptions) &&
        anchored_fit.optimizer ==
            g1::optimizer_config();
    report.repeated_fit_bit_identical =
        anchored_fit.repeated_fit_bit_identical &&
        anchored_offline.repeated_fit_bit_identical;
    report.parent_immutable =
        anchored_fit.parent_immutable &&
        anchored_offline.parent_immutable;
    report.only_priority_component_changed =
        anchored_fit.only_priority_component_changed &&
        anchored_offline
            .only_priority_component_changed;
    report.g1_offline_passed =
        offline_bound_to_fit(
            anchored_offline, anchored_fit) &&
        anchored_offline.gate_passed();
    report.ancestral_passed =
        anchored_offline.ancestral.gate_passed();

    bool neutral_decks_valid = true;
    bool per_deck_nonworsening = true;
    std::size_t summed_rows = 0;
    std::size_t summed_options = 0;
    std::size_t summed_baseline_support = 0;
    std::size_t summed_anchored_support = 0;
    double summed_baseline_kl = 0.0;
    double summed_anchored_kl = 0.0;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& value =
            neutral_dev.decks[deck];
        const bool deck_valid =
            value.rows == kRowsPerDeck &&
            value.options ==
                kNeutralDevOptionsByDeck[deck] &&
            finite(
                value
                    .baseline_parent_to_candidate_kl) &&
            finite(
                value
                    .anchored_parent_to_candidate_kl) &&
            value.baseline_parent_to_candidate_kl >=
                0.0 &&
            value.anchored_parent_to_candidate_kl >=
                0.0 &&
            value.baseline_exact_support_changes <=
                value.rows &&
            value.anchored_exact_support_changes <=
                value.rows;
        neutral_decks_valid =
            neutral_decks_valid && deck_valid;
        per_deck_nonworsening =
            per_deck_nonworsening &&
            deck_valid &&
            value.anchored_parent_to_candidate_kl <=
                value.baseline_parent_to_candidate_kl &&
            value.anchored_exact_support_changes <=
                value.baseline_exact_support_changes;
        if (deck_valid) {
            summed_rows += value.rows;
            summed_options += value.options;
            summed_baseline_support +=
                value.baseline_exact_support_changes;
            summed_anchored_support +=
                value.anchored_exact_support_changes;
            summed_baseline_kl +=
                value.baseline_parent_to_candidate_kl;
            summed_anchored_kl +=
                value.anchored_parent_to_candidate_kl;
        }
    }
    const double recomputed_baseline_kl =
        summed_baseline_kl /
        static_cast<double>(kDeckCount);
    const double recomputed_anchored_kl =
        summed_anchored_kl /
        static_cast<double>(kDeckCount);
    const bool neutral_valid =
        neutral_dev.finite_probabilities &&
        neutral_decks_valid &&
        neutral_dev.rows == kNeutralDevExamples &&
        neutral_dev.rows == summed_rows &&
        neutral_dev.options == kNeutralDevOptions &&
        neutral_dev.options == summed_options &&
        neutral_dev.baseline_exact_support_changes ==
            summed_baseline_support &&
        neutral_dev.anchored_exact_support_changes ==
            summed_anchored_support &&
        finite(
            neutral_dev
                .baseline_equal_deck_kl) &&
        finite(
            neutral_dev
                .anchored_equal_deck_kl) &&
        neutral_dev.baseline_equal_deck_kl >= 0.0 &&
        neutral_dev.anchored_equal_deck_kl >= 0.0 &&
        neutral_dev.baseline_equal_deck_kl ==
            recomputed_baseline_kl &&
        neutral_dev.anchored_equal_deck_kl ==
            recomputed_anchored_kl;
    report.neutral_baseline_nonzero =
        neutral_valid &&
        neutral_dev.baseline_equal_deck_kl > 0.0 &&
        neutral_dev.baseline_exact_support_changes >
            0;
    report.neutral_per_deck_nonworsening =
        neutral_valid &&
        per_deck_nonworsening;
    report.neutral_kl_halved =
        neutral_valid &&
        2.0 *
                neutral_dev
                    .anchored_equal_deck_kl <=
            neutral_dev
                .baseline_equal_deck_kl;
    report.neutral_support_changes_halved =
        neutral_valid &&
        neutral_dev.anchored_exact_support_changes <=
            neutral_dev.baseline_exact_support_changes /
                2U;

    add_failure(
        report, report.source_identity_exact,
        "source identity or immutability failed");
    add_failure(
        report, report.control_exact,
        "omitted-anchor G1 control failed");
    add_failure(
        report, report.mixed_batch_exact,
        "mixed training batch or fit boundary failed");
    add_failure(
        report, report.repeated_fit_bit_identical,
        "anchored repeat fit was not bit-identical");
    add_failure(
        report, report.parent_immutable,
        "C16 was mutated");
    add_failure(
        report,
        report.only_priority_component_changed,
        "a non-Priority component changed");
    add_failure(
        report, report.g1_offline_passed,
        "an original G1 offline gate failed");
    add_failure(
        report, report.ancestral_passed,
        "the full Ancestral gate failed");
    add_failure(
        report, report.neutral_baseline_nonzero,
        "neutral baseline was zero or invalid");
    add_failure(
        report,
        report.neutral_per_deck_nonworsening,
        "a neutral DEV deck worsened");
    add_failure(
        report, report.neutral_kl_halved,
        "equal-deck neutral KL was not halved");
    add_failure(
        report,
        report.neutral_support_changes_halved,
        "neutral support changes were not halved");
    return report;
}

bool OfflineRunReport::gate_passed() const {
    try {
        if (corpus_reconstructions != 1 ||
            corpus_digest != kRequiredCorpusDigest ||
            source_identity !=
                required_source_identity() ||
            neutral_identity !=
                source_identity.neutral_file ||
            neutral_selected_order_sha256 !=
                kNeutralSelectedOrderSha256 ||
            neutral_parent_fingerprint !=
                g1::kRequiredParentFingerprint ||
            neutral_baseline_fingerprint !=
                kRequiredControlFingerprint ||
            neutral_anchored_fingerprint !=
                anchored_fit.candidate_fingerprint) {
            return false;
        }
        const GateReport expected =
            evaluate_gate(
                source_identity_exact, control,
                anchored_training, anchored_fit,
                anchored_offline, neutral_dev);
        return gate == expected && gate.passed();
    } catch (const std::exception&) {
        return false;
    }
}

bool OfflineRunReport::selection_ready() const {
    try {
        if (corpus_reconstructions != 1 ||
            corpus_digest != kRequiredCorpusDigest ||
            source_identity !=
                required_source_identity() ||
            neutral_identity !=
                source_identity.neutral_file ||
            neutral_selected_order_sha256 !=
                kNeutralSelectedOrderSha256 ||
            !source_identity_exact ||
            !control.gate_passed() ||
            !anchored_training.valid(true) ||
            anchored_fit.optimizer !=
                g1::optimizer_config() ||
            !fit_identity_exact(
                anchored_fit, kAnchoredExamples,
                kAnchoredOptions) ||
            !offline_bound_to_fit(
                anchored_offline, anchored_fit) ||
            neutral_parent_fingerprint !=
                g1::kRequiredParentFingerprint ||
            neutral_baseline_fingerprint !=
                kRequiredControlFingerprint ||
            neutral_anchored_fingerprint !=
                anchored_fit.candidate_fingerprint ||
            !gate_passed()) {
            return false;
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

OfflineRunReport run_offline(
    std::shared_ptr<const LearnedModel> parent) {
    require_parent(parent);
    OfflineRunReport report;
    report.source_identity =
        required_source_identity();
    report.neutral_identity =
        report.source_identity.neutral_file;
    report.neutral_selected_order_sha256 =
        report.source_identity
            .neutral_selected_order_sha256;

    const auto parent_before =
        integrity::snapshot_regular_file(
            std::string(dev::kParentArtifactPath));
    const auto dev1_before =
        integrity::snapshot_regular_file(
            std::string(bundle::kArtifactPath));
    const auto neutral_before =
        integrity::snapshot_regular_file(
            neutral_artifact::
                production_artifact_path());
    if (!exact_file_identity(
            parent_before,
            report.source_identity.parent_artifact) ||
        !exact_file_identity(
            dev1_before,
            report.source_identity.dev1_bundle) ||
        !exact_file_identity(
            neutral_before,
            report.source_identity.neutral_file)) {
        fail("frozen source file identity drifted");
    }
    const bundle::Bundle dev1_artifact =
        bundle::load_published();
    const neutral_artifact::Contract contract =
        neutral_artifact::make_contract(
            dev1_artifact.manifest,
            neutral_artifact::
                accepted_dev4_capacity());
    const neutral_artifact::Artifact artifact =
        neutral_artifact::load_published(
            contract, report.neutral_identity);
    const std::string selected_order =
        bundle::format_sha256(
            artifact.manifest
                .selected_order_sha256);
    if (selected_order !=
            kNeutralSelectedOrderSha256) {
        fail("neutral selected-order identity drifted");
    }
    const neutral_eval::PreparedNeutralCorpus
        neutral = neutral_eval::prepare(artifact);

    const g1::Census census =
        g1::collect_census(parent);
    g1::require_frozen_census(census);
    const g1::Corpus corpus =
        g1::collect_corpus(parent, census);
    report.corpus_reconstructions = 1;
    report.corpus_digest =
        g2::canonical_corpus_digest(corpus);
    report.control.fit =
        g1::fit_with_optimizer(
            corpus, parent,
            g1::optimizer_config());
    report.control.corpus_digest_exact =
        report.corpus_digest ==
            kRequiredCorpusDigest;
    report.control.fingerprint_exact =
        report.control.fit.candidate &&
        report.control.fit.candidate_fingerprint ==
            kRequiredControlFingerprint &&
        learned_model_fingerprint(
            report.control.fit.candidate) ==
            kRequiredControlFingerprint;
    report.control.aggregate_metrics_bit_exact =
        control_matches_frozen_result(
            report.control.fit);

    if (report.control.gate_passed()) {
        report.anchored_training =
            build_training_batch(
                corpus, neutral,
                g1::optimizer_config(), true);
        const auto supplements =
            std::span<
                const LearnedValuePriorityTrainingExample>(
                    report.anchored_training.examples)
                .subspan(kTeacherExamples);
        report.anchored_fit =
            g1::fit_with_optimizer_and_supplement(
                corpus, parent,
                g1::optimizer_config(),
                supplements);
        report.anchored_offline =
            g1::evaluate_offline(
                corpus, report.anchored_fit,
                parent,
                report.anchored_fit.candidate);
        report.neutral_parent_fingerprint =
            learned_model_fingerprint(parent);
        report.neutral_baseline_fingerprint =
            learned_model_fingerprint(
                report.control.fit.candidate);
        report.neutral_anchored_fingerprint =
            learned_model_fingerprint(
                report.anchored_fit.candidate);
        report.neutral_dev =
            score_neutral_check(
                neutral, parent,
                report.control.fit.candidate,
                report.anchored_fit.candidate);
    }

    const auto parent_after =
        integrity::snapshot_regular_file(
            std::string(dev::kParentArtifactPath));
    const auto dev1_after =
        integrity::snapshot_regular_file(
            std::string(bundle::kArtifactPath));
    const auto neutral_after =
        integrity::snapshot_regular_file(
            neutral_artifact::
                production_artifact_path());
    report.source_identity_exact =
        parent_before == parent_after &&
        dev1_before == dev1_after &&
        neutral_before == neutral_after &&
        exact_file_identity(
            parent_after,
            report.source_identity.parent_artifact) &&
        exact_file_identity(
            dev1_after,
            report.source_identity.dev1_bundle) &&
        exact_file_identity(
            neutral_after,
            report.source_identity.neutral_file) &&
        learned_model_fingerprint(parent) ==
            report.source_identity
                .parent_model_fingerprint &&
        selected_order ==
            report.source_identity
                .neutral_selected_order_sha256;
    report.gate =
        evaluate_gate(
            report.source_identity_exact,
            report.control,
            report.anchored_training,
            report.anchored_fit,
            report.anchored_offline,
            report.neutral_dev);
    return report;
}

BotBenchmarkSummary run_selector(
    std::shared_ptr<const LearnedModel> parent,
    const OfflineRunReport& report) {
    require_parent(parent);
    if (!report.selection_ready()) {
        throw std::invalid_argument(
            "AQ4-G3 selector requires a bound eligible candidate");
    }
    const std::string parent_before =
        learned_model_fingerprint(parent);
    const auto parent_components =
        learned_model_component_fingerprints(parent);
    if (report.control.fit.parent_components !=
            parent_components ||
        report.anchored_fit.parent_components !=
            parent_components) {
        throw std::runtime_error(
            "AQ4-G3 selector parent component identity drifted");
    }
    const auto replayed =
        update_learned_value_priority_head(
            parent,
            report.anchored_training.examples,
            report.anchored_fit.optimizer);
    if (learned_model_fingerprint(parent) !=
            parent_before ||
        learned_model_fingerprint(replayed) !=
            report.anchored_fit
                .candidate_fingerprint ||
        learned_model_component_fingerprints(replayed) !=
            report.anchored_fit
                .candidate_components) {
        throw std::runtime_error(
            "AQ4-G3 selector candidate is not caused by the bound batch");
    }
    const auto parent_file_before =
        integrity::snapshot_regular_file(
            std::string(dev::kParentArtifactPath));
    const auto dev1_file_before =
        integrity::snapshot_regular_file(
            std::string(bundle::kArtifactPath));
    const auto neutral_file_before =
        integrity::snapshot_regular_file(
            neutral_artifact::
                production_artifact_path());
    if (!exact_file_identity(
            parent_file_before,
            report.source_identity.parent_artifact) ||
        !exact_file_identity(
            dev1_file_before,
            report.source_identity.dev1_bundle) ||
        !exact_file_identity(
            neutral_file_before,
            report.source_identity.neutral_file)) {
        throw std::runtime_error(
            "AQ4-G3 source changed before selector");
    }

    GameConfig game;
    game.max_turns = 500;
    game.learned_training_seed = 424242;
    game.learned_search_depth = 1;
    const BotBenchmarkSummary summary =
        run_bot_benchmark(
            g1::kSelectorRepetitions,
            kSelectorSeed,
            g1::selector_bot_config(
                report.anchored_fit.candidate,
                g1::kCandidateResidualWeight),
            g1::selector_bot_config(parent, 0.0),
            game, false);
    g1::validate_selector_summary(
        summary, parent,
        report.anchored_fit.candidate,
        kSelectorSeed);
    const auto parent_file_after =
        integrity::snapshot_regular_file(
            std::string(dev::kParentArtifactPath));
    const auto dev1_file_after =
        integrity::snapshot_regular_file(
            std::string(bundle::kArtifactPath));
    const auto neutral_file_after =
        integrity::snapshot_regular_file(
            neutral_artifact::
                production_artifact_path());
    if (parent_file_after != parent_file_before ||
        dev1_file_after != dev1_file_before ||
        neutral_file_after != neutral_file_before ||
        learned_model_fingerprint(parent) !=
            parent_before ||
        learned_model_component_fingerprints(parent) !=
            parent_components) {
        throw std::runtime_error(
            "AQ4-G3 source changed during selector");
    }
    return summary;
}

void print_offline(
    std::ostream& output,
    const OfflineRunReport& report) {
    output
        << "schema=old-school-action-q-aq4-g3-anchor-v1\n"
        << "mode=run"
        << " corpus_digest=" << report.corpus_digest
        << " corpus_reconstructions="
        << report.corpus_reconstructions
        << " parent_artifact_sha256="
        << report.source_identity.parent_artifact.sha256
        << " neutral_artifact_sha256="
        << report.neutral_identity.sha256
        << " neutral_selected_order_sha256="
        << report.neutral_selected_order_sha256
        << " source_identity_exact="
        << report.source_identity_exact << '\n'
        << "control fingerprint="
        << report.control.fit.candidate_fingerprint
        << " corpus_digest_exact="
        << report.control.corpus_digest_exact
        << " fingerprint_exact="
        << report.control.fingerprint_exact
        << " aggregate_metrics_bit_exact="
        << report.control
               .aggregate_metrics_bit_exact
        << " result="
        << (report.control.gate_passed()
                ? "PASS"
                : "FAIL")
        << '\n'
        << "mixed_batch digest="
        << report.anchored_training.digest
        << " teacher_examples="
        << report.anchored_training.teacher_examples
        << " teacher_options="
        << report.anchored_training.teacher_options
        << " anchor_examples="
        << report.anchored_training.anchor_examples
        << " anchor_options="
        << report.anchored_training.anchor_options
        << " valid="
        << report.anchored_training.valid(true)
        << '\n'
        << "anchored fingerprint="
        << report.anchored_fit.candidate_fingerprint
        << " g1_offline="
        << report.anchored_offline.gate_passed()
        << " ancestral="
        << report.anchored_offline
               .ancestral.gate_passed()
        << " neutral_baseline_nonzero="
        << report.gate.neutral_baseline_nonzero
        << " neutral_per_deck_nonworsening="
        << report.gate
               .neutral_per_deck_nonworsening
        << " neutral_kl_halved="
        << report.gate.neutral_kl_halved
        << " neutral_support_halved="
        << report.gate
               .neutral_support_changes_halved
        << '\n'
        << "tactical descriptor_order="
        << report.anchored_offline
               .descriptor_order_identity
        << " redundant_counter="
        << report.anchored_offline
               .redundant_counter_pass
        << " braingeyser_productive="
        << report.anchored_offline
               .braingeyser_x_zero_excluded
        << " sick_bear_growth_pass="
        << report.anchored_offline
               .sick_bear_growth_pass
        << " live_force_spike="
        << report.anchored_offline.live_force_spike
        << '\n'
        << std::setprecision(17)
        << "ancestral self_score="
        << report.anchored_offline.ancestral.self_score
        << " opponent_score="
        << report.anchored_offline
               .ancestral.opponent_score
        << " complete_actions="
        << report.anchored_offline
               .ancestral.complete_legal_actions_exact
        << " fingerprint_exact="
        << report.anchored_offline
               .ancestral
               .information_action_fingerprint_exact
        << " hidden_identity="
        << report.anchored_offline
               .ancestral
               .hidden_repartition_bit_identical
        << " self_above="
        << report.anchored_offline
               .ancestral
               .self_strictly_above_opponent
        << " opponent_absent="
        << report.anchored_offline
               .ancestral
               .opponent_absent_from_support
        << " selected_support="
        << report.anchored_offline
               .ancestral.selected_support.size()
        << '\n';
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        output
            << "loss_mass deck="
            << deck_name(static_cast<DeckId>(deck))
            << " teacher="
            << report.anchored_training
                   .teacher_loss_mass_by_deck[deck]
            << " anchor="
            << report.anchored_training
                   .anchor_loss_mass_by_deck[deck]
            << '\n';
    }
    const auto print_metric =
        [&](std::string_view split,
            std::string_view deck,
            double parent_agreement,
            double candidate_agreement,
            double parent_regret,
            double candidate_regret) {
            output
                << "teacher_metric split=" << split
                << " deck=" << deck
                << " parent_agreement="
                << parent_agreement
                << " candidate_agreement="
                << candidate_agreement
                << " parent_regret=" << parent_regret
                << " candidate_regret="
                << candidate_regret
                << " regret_delta="
                << candidate_regret - parent_regret
                << '\n';
        };
    print_metric(
        "FIT", "ALL",
        report.anchored_fit.parent_fit
            .equal_deck_top_one_expected_agreement,
        report.anchored_fit.candidate_fit
            .equal_deck_top_one_expected_agreement,
        report.anchored_fit.parent_fit
            .equal_deck_mean_regret,
        report.anchored_fit.candidate_fit
            .equal_deck_mean_regret);
    print_metric(
        "DEV", "ALL",
        report.anchored_fit.parent_check
            .equal_deck_top_one_expected_agreement,
        report.anchored_fit.candidate_check
            .equal_deck_top_one_expected_agreement,
        report.anchored_fit.parent_check
            .equal_deck_mean_regret,
        report.anchored_fit.candidate_check
            .equal_deck_mean_regret);
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const std::string_view name =
            deck_name(static_cast<DeckId>(deck));
        print_metric(
            "FIT", name,
            report.anchored_fit.parent_fit
                .decks[deck]
                .top_one_expected_agreement,
            report.anchored_fit.candidate_fit
                .decks[deck]
                .top_one_expected_agreement,
            report.anchored_fit.parent_fit
                .decks[deck].mean_regret,
            report.anchored_fit.candidate_fit
                .decks[deck].mean_regret);
        print_metric(
            "DEV", name,
            report.anchored_fit.parent_check
                .decks[deck]
                .top_one_expected_agreement,
            report.anchored_fit.candidate_check
                .decks[deck]
                .top_one_expected_agreement,
            report.anchored_fit.parent_check
                .decks[deck].mean_regret,
            report.anchored_fit.candidate_check
                .decks[deck].mean_regret);
    }
    output
        << "neutral scope=ALL"
        << " baseline_kl="
        << report.neutral_dev
               .baseline_equal_deck_kl
        << " anchored_kl="
        << report.neutral_dev
               .anchored_equal_deck_kl
        << " baseline_support_changes="
        << report.neutral_dev
               .baseline_exact_support_changes
        << " anchored_support_changes="
        << report.neutral_dev
               .anchored_exact_support_changes
        << '\n';
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& value =
            report.neutral_dev.decks[deck];
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
    output
        << "result="
        << (report.gate_passed()
                ? "OFFLINE_PASS"
                : "REJECT")
        << " selector_seed_opened=0"
        << " artifact_published=0\n";
    for (const std::string& failure :
         report.gate.failures) {
        output << "failure=" << failure << '\n';
    }
}

void print_selector(
    std::ostream& output,
    const BotBenchmarkSummary& summary) {
    g1::print_selector(
        output, summary,
        g1::classify_selector(summary));
}

} // namespace old_school::action_q_nested_actor_anchor
