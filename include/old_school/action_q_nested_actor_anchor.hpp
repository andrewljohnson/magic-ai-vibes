#pragma once

#include "old_school/action_q_nested_actor_early_stop.hpp"
#include "old_school/fq4_neutral_evaluator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::action_q_nested_actor_anchor {

namespace g1 = action_q_nested_actor_distill;
namespace g2 = action_q_nested_actor_early_stop;
namespace neutral_eval = fq4_neutral_evaluator;
namespace neutral_artifact = fq4_neutral_supplement;

inline constexpr std::uint64_t kSelectorSeed =
    202607282201ULL;
inline constexpr std::string_view kRequiredCorpusDigest =
    "3b1fa739cbe5ece28f581f7361bc7ba22e9bc0d6f90f39add"
    "ce783f1e7df346b";
inline constexpr std::string_view
    kRequiredControlFingerprint =
        "e0217302d83a4949950af84ab754e38be6ebbd6c2adac6a419"
        "3f05f70b7a1376";
inline constexpr std::uint64_t kParentArtifactBytes =
    3'111'437;
inline constexpr std::uint64_t kNeutralArtifactBytes =
    661'475;
inline constexpr std::string_view kNeutralArtifactSha256 =
    "47d94823f043971f6f9f0aa5f552bfae210af9615d8f6dc739"
    "2e52dad3eaa105";
inline constexpr std::string_view
    kNeutralSelectedOrderSha256 =
        "c0925e709daaeefdb7d7714db6b07e68bd70b60f0a201d162"
        "6ee0fde28f61b7b";
inline constexpr std::size_t kTeacherExamples = 320;
inline constexpr std::size_t kTeacherOptions = 1023;
inline constexpr std::size_t kDevExamples = 319;
inline constexpr std::size_t kDevOptions = 1018;
inline constexpr std::size_t kAnchorExamples = 160;
inline constexpr std::size_t kAnchorOptions = 439;
inline constexpr std::size_t kNeutralDevExamples = 160;
inline constexpr std::size_t kNeutralDevOptions = 438;
inline constexpr std::size_t kAnchoredExamples =
    kTeacherExamples + kAnchorExamples;
inline constexpr std::size_t kAnchoredOptions =
    kTeacherOptions + kAnchorOptions;
inline constexpr std::size_t kRowsPerDeck =
    neutral_artifact::kRowsPerDeckAndSplit;
inline constexpr double kTeacherRowWeight = 1.0 / 64.0;
inline constexpr double kAnchorRowWeight = 1.0 / 32.0;

enum class Command {
    Run,
};

enum class TrainingSourceKind : std::uint8_t {
    Teacher = 0,
    Anchor = 1,
};

struct TrainingRow {
    DeckId owner_deck = DeckId::Green;
    TrainingSourceKind source =
        TrainingSourceKind::Teacher;
    LearnedValuePriorityTrainingExample example;
};

struct SourceIdentity {
    std::string corpus_digest;
    neutral_artifact::FileIdentity dev1_bundle;
    neutral_artifact::FileIdentity parent_artifact;
    std::string parent_model_fingerprint;
    neutral_artifact::FileIdentity neutral_file;
    std::string neutral_selected_order_sha256;

    bool operator==(const SourceIdentity&) const = default;
};

struct TrainingBatch {
    std::vector<LearnedValuePriorityTrainingExample> examples;
    std::vector<TrainingSourceKind> sources;
    std::vector<DeckId> owner_decks;
    std::size_t teacher_examples = 0;
    std::size_t teacher_options = 0;
    std::size_t anchor_examples = 0;
    std::size_t anchor_options = 0;
    std::array<double, kDeckCount>
        teacher_loss_mass_by_deck{};
    std::array<double, kDeckCount>
        anchor_loss_mass_by_deck{};
    SourceIdentity source_identity;
    std::string digest;

    bool valid(bool expect_anchors) const;
};

struct ControlReport {
    g1::FitReport fit;
    bool corpus_digest_exact = false;
    bool fingerprint_exact = false;
    bool aggregate_metrics_bit_exact = false;

    bool gate_passed() const;
};

struct GateReport {
    bool source_identity_exact = false;
    bool control_exact = false;
    bool mixed_batch_exact = false;
    bool repeated_fit_bit_identical = false;
    bool parent_immutable = false;
    bool only_priority_component_changed = false;
    bool g1_offline_passed = false;
    bool ancestral_passed = false;
    bool neutral_baseline_nonzero = false;
    bool neutral_per_deck_nonworsening = false;
    bool neutral_kl_halved = false;
    bool neutral_support_changes_halved = false;
    std::vector<std::string> failures;

    bool passed() const;
    bool operator==(const GateReport&) const = default;
};

struct OfflineRunReport {
    std::string corpus_digest;
    std::size_t corpus_reconstructions = 0;
    SourceIdentity source_identity;
    neutral_artifact::FileIdentity neutral_identity;
    std::string neutral_selected_order_sha256;
    bool source_identity_exact = false;
    ControlReport control;
    TrainingBatch anchored_training;
    g1::FitReport anchored_fit;
    g1::OfflineReport anchored_offline;
    std::string neutral_parent_fingerprint;
    std::string neutral_baseline_fingerprint;
    std::string neutral_anchored_fingerprint;
    neutral_eval::NeutralDriftMetrics neutral_dev;
    GateReport gate;

    bool gate_passed() const;
    bool selection_ready() const;
};

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

// Pure constructor/digest seam. Rows must be in their intended pre-shuffle
// order. Production supplies the frozen G1 prefix followed by neutral FIT.
TrainingBatch build_training_batch(
    std::span<const TrainingRow> rows,
    const LearnedValuePriorityHeadUpdateConfig& optimizer);
TrainingBatch build_training_batch(
    std::span<const TrainingRow> rows,
    const LearnedValuePriorityHeadUpdateConfig& optimizer,
    SourceIdentity source_identity);
TrainingBatch build_training_batch(
    const g1::Corpus& corpus,
    const neutral_eval::PreparedNeutralCorpus& neutral,
    const LearnedValuePriorityHeadUpdateConfig& optimizer,
    bool include_anchors);
std::string canonical_training_digest(
    const TrainingBatch& batch,
    const LearnedValuePriorityHeadUpdateConfig& optimizer);
SourceIdentity required_source_identity();

bool control_matches_frozen_result(
    const g1::FitReport& fit);
neutral_eval::NeutralDriftMetrics score_neutral_check(
    const neutral_eval::PreparedNeutralCorpus& neutral,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> unanchored,
    std::shared_ptr<const LearnedModel> anchored);
GateReport evaluate_gate(
    bool source_identity_exact,
    const ControlReport& control,
    const TrainingBatch& anchored_training,
    const g1::FitReport& anchored_fit,
    const g1::OfflineReport& anchored_offline,
    const neutral_eval::NeutralDriftMetrics& neutral_dev);

OfflineRunReport run_offline(
    std::shared_ptr<const LearnedModel> parent);
BotBenchmarkSummary run_selector(
    std::shared_ptr<const LearnedModel> parent,
    const OfflineRunReport& report);

void print_offline(
    std::ostream& output,
    const OfflineRunReport& report);
void print_selector(
    std::ostream& output,
    const BotBenchmarkSummary& summary);

namespace testing {

// Production validates the complete frozen Corpus first, then delegates to
// this split-explicit seam. The DEV spans are census-checked but can never
// cross the update boundary.
TrainingBatch build_training_batch_from_splits(
    std::span<const g1::RootExample> teacher_fit,
    std::span<const g1::RootExample> teacher_dev,
    const neutral_eval::PreparedNeutralCorpus& neutral,
    const LearnedValuePriorityHeadUpdateConfig& optimizer,
    bool include_anchors);

} // namespace testing

} // namespace old_school::action_q_nested_actor_anchor
