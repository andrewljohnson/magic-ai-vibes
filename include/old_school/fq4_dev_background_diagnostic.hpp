#pragma once

#include "old_school/fq4_dev_evaluator.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_dev_background_diagnostic {

inline constexpr std::string_view kSchema =
    "old-school-fq4-dev2-background-diagnostic-v1";
inline constexpr std::string_view kRejectedCandidateFingerprint =
    "712600783152e89ff1a53394149764db227e55289a656530342226b7e1ee6151";
inline constexpr std::string_view kFitInputSha256 =
    "586b121c3c9bdb1a61305cac86882cd20b5d2ba332b4d5a54defc2c7756393a1";
inline constexpr std::size_t kFitExamples = 88;
inline constexpr std::size_t kFitOptions = 548;
inline constexpr std::size_t kBackgroundOptions = 22;
inline constexpr std::array<std::size_t, fq4_dev_bundle::kDeckCount>
    kBackgroundOptionsPerDeck{2, 2, 2, 2, 3};
inline constexpr double kMaterialKl = 0.01;
inline constexpr std::string_view kStackCensusSchema =
    "old-school-fq4-dev3-stack-census-v1";
// The frozen learned-priority-policy-features-v1 contract assigns the
// shared public stack-size observation to feature 20 as stack.size() / 5.
// The contract digest below is also pinned by the production formatter.
inline constexpr std::size_t kStackSizeFeatureIndex = 20;
inline constexpr std::size_t kStackSizeEncodingDenominator = 5;
inline constexpr std::size_t kStackCensusSelectedRows = 192;
inline constexpr std::size_t kStackCensusSelectedOptions = 1141;

struct DeckMetrics {
    std::size_t roots = 0;
    std::size_t options = 0;
    double parent_to_candidate_kl = 0.0;
    double total_variation = 0.0;
    std::size_t exact_support_changes = 0;
    double maximum_combined_score_delta = 0.0;

    bool operator==(const DeckMetrics&) const = default;
};

struct SplitMetrics {
    std::array<DeckMetrics, fq4_dev_bundle::kDeckCount> decks;
    std::size_t roots = 0;
    std::size_t options = 0;
    double deck_balanced_parent_to_candidate_kl = 0.0;
    double deck_balanced_total_variation = 0.0;
    std::size_t exact_support_changes = 0;
    double maximum_combined_score_delta = 0.0;

    bool operator==(const SplitMetrics&) const = default;
};

struct Measurements {
    SplitMetrics fit;
    SplitMetrics check;
    double green_white_mean_kl = 0.0;
    double blue_ru_mean_kl = 0.0;
    bool material_green_or_white = false;
    bool green_white_exceeds_blue_ru = false;
    bool hypothesis_supported = false;

    bool operator==(const Measurements&) const = default;
};

struct Report {
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    fq4_dev_evaluator::FitAccounting fit_accounting;
    Measurements parent_control;
    Measurements measurements;
    bool parent_immutable = false;
    bool candidate_exact = false;
    bool nonpriority_components_identical = false;

    bool operator==(const Report&) const = default;
};

struct ParentControlReport {
    std::string parent_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    Measurements measurements;
    bool parent_immutable = false;

    bool operator==(const ParentControlReport&) const = default;
};

struct StackContextCount {
    std::size_t roots = 0;
    std::size_t options = 0;

    bool operator==(const StackContextCount&) const = default;
};

struct StackDeckCensus {
    StackContextCount empty;
    StackContextCount active;

    bool operator==(const StackDeckCensus&) const = default;
};

struct StackRoleCensus {
    std::array<StackDeckCensus, fq4_dev_bundle::kDeckCount>
        decks;

    bool operator==(const StackRoleCensus&) const = default;
};

struct StackSplitCensus {
    StackRoleCensus positive;
    StackRoleCensus background;

    bool operator==(const StackSplitCensus&) const = default;
};

struct StackCensus {
    StackSplitCensus fit;
    StackSplitCensus check;
    std::size_t selected_rows = 0;
    std::size_t selected_options = 0;
    std::size_t action_invariant_rows = 0;
    std::size_t exact_stack_encoding_rows = 0;
    std::size_t role_overlap_rows = 0;

    bool operator==(const StackCensus&) const = default;
};

struct StackCensusReport {
    std::string bundle_schema;
    std::size_t bundle_bytes = 0;
    std::string bundle_sha256;
    std::string feature_schema;
    std::size_t feature_count = 0;
    std::string feature_contract_sha256;
    std::size_t stack_size_feature_index = 0;
    std::size_t stack_size_encoding_denominator = 0;
    StackCensus census;
    bool bundle_immutable = false;

    bool operator==(const StackCensusReport&) const = default;
};

Measurements measure(
    const fq4_dev_evaluator::PreparedCorpus& corpus,
    const fq4_dev_evaluator::CorpusLogits& parent,
    const fq4_dev_evaluator::CorpusLogits& candidate);

StackCensus measure_stack_census(
    const std::vector<fq4_dev_bundle::SelectedRow>& fit,
    const std::vector<fq4_dev_bundle::SelectedRow>& check);
StackCensusReport run_stack_census();
std::string format_stack_census_report(
    const StackCensusReport& report);
ParentControlReport run_parent_control();
std::string format_parent_control_report(
    const ParentControlReport& report);
Report run_fixed();
std::string format_report(const Report& report);

} // namespace old_school::fq4_dev_background_diagnostic
