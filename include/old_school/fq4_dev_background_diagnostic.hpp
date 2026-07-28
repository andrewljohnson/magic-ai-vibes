#pragma once

#include "old_school/fq4_dev_evaluator.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

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

Measurements measure(
    const fq4_dev_evaluator::PreparedCorpus& corpus,
    const fq4_dev_evaluator::CorpusLogits& parent,
    const fq4_dev_evaluator::CorpusLogits& candidate);

ParentControlReport run_parent_control();
std::string format_parent_control_report(
    const ParentControlReport& report);
Report run_fixed();
std::string format_report(const Report& report);

} // namespace old_school::fq4_dev_background_diagnostic
