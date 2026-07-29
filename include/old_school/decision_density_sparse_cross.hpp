#pragma once

#include "old_school/decision_density_bilinear.hpp"
#include "old_school/decision_density_sparse_support.hpp"
#include "old_school/learned_priority_sparse_cross.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::decision_density_sparse_cross {

namespace aq19 = decision_density_bilinear;
namespace support = decision_density_sparse_support;

inline constexpr std::string_view kIdentifier =
    "AQ20-DBC6-S16-SPARSE-CROSS";
inline constexpr std::string_view kSchema =
    "old-school-aq20-sparse-cross-v1";
inline constexpr std::uint64_t kFitTag =
    UINT64_C(202607292001);
inline constexpr std::uint64_t kC16SelectorSeed =
    UINT64_C(202607292011);
inline constexpr std::uint64_t kAq19SelectorSeed =
    UINT64_C(202607292012);
inline constexpr std::size_t kTermCount = 16;
inline constexpr double kPairTemperature = 0.10;
inline constexpr double kResidualWeight = 0.10;
inline constexpr double kStepFraction = 0.25;
inline constexpr double kMaximumAbsoluteBeta = 1.0;

inline constexpr std::string_view kRequiredSupportDigest =
    "b6a321bc76d137550ddf74e9afa2dfa03874124c47b9af879f0b63e551f8a4a7";
inline constexpr std::array<std::size_t, support::kPartitionCount>
    kRequiredActiveCoordinates{
        8'384, 7'865, 7'739, 7'777, 7'938};
inline constexpr std::array<std::size_t, support::kPartitionCount>
    kRequiredEligibleCoordinates{
        353, 175, 176, 193, 194};
inline constexpr std::array<std::string_view, support::kPartitionCount>
    kRequiredSupportTableSha256{
        "cfbd1d4eb1356cb347e47be2d47c1be8bc5c9c00bc6d0e0d7af8b14720884e93",
        "b77022091035f30a6d2760c10652c0ed08c20604c4377b9053632049d6423b19",
        "7e6470b6fb204d212e87e292c29ed6edcbebe81d114c59c0b049e0b7bc5c9afc",
        "13b728281d3e926a27072aab1b1addf394dae014571a8a044bf9ceac12989eb2",
        "d6016121fd8c1a62836681d7cc7da53284cd9de92e9cdd726edb53b83cc3bb63",
    };
inline constexpr std::string_view kRequiredAq19ParameterSha256 =
    "3114c898085375b7c39a8d8a7add5b0ab87dc70916d676deccd28d45e0942194";

static_assert(kTermCount == kLearnedPrioritySparseCrossTermCount);
static_assert(aq19::kStateFeatureCount ==
              kLearnedPrioritySparseCrossStateFeatureCount);
static_assert(aq19::kActionFeatureCount ==
              kLearnedPrioritySparseCrossActionFeatureCount);

using Metrics = aq19::Metrics;
using Term = LearnedPrioritySparseCrossTerm;
using Terms = LearnedPrioritySparseCrossTerms;

struct StageDerivative {
    double gradient = 0.0;
    double diagonal = 0.0;
    double beta = 0.0;
    double actual_improvement = 0.0;
    bool valid = false;
    bool clipped = false;

    bool operator==(const StageDerivative&) const = default;
};

struct SelectedTerm {
    Term term;
    std::size_t root_support = 0;
    std::size_t group_support = 0;
    double maximum_group_leverage = 0.0;
    StageDerivative derivative;

    bool operator==(const SelectedTerm&) const = default;
};

struct FitReport {
    Terms terms;
    std::array<SelectedTerm, kTermCount> selected{};
    std::string term_sha256;
    std::size_t eligible_coordinates = 0;
    std::size_t representative_coordinates = 0;
    std::size_t completed_stages = 0;
    bool completed = false;
    std::string failure;
    Metrics c16_metrics;
    Metrics candidate_metrics;
    std::vector<std::vector<double>> residuals;
    double maximum_absolute_centered_logit = 0.0;
    std::size_t saturated_roots = 0;
    double saturated_root_fraction = 0.0;

    bool operator==(const FitReport&) const = default;
};

struct GroupedOofReport {
    std::array<FitReport, aq19::kFoldCount> fits{};
    std::array<FitReport, aq19::kFoldCount> repeated_fits{};
    std::array<Metrics, aq19::kFoldCount> c16_holdout{};
    std::array<Metrics, aq19::kFoldCount> aq19_holdout{};
    std::array<Metrics, aq19::kFoldCount> candidate_holdout{};
    std::vector<std::vector<double>> candidate_residuals;
    Metrics c16;
    Metrics aq19;
    Metrics candidate;
    bool physical_groups_disjoint = false;
    bool every_root_predicted_once = false;
    bool repeated_fits_bit_identical = false;
    bool repeated_scores_bit_identical = false;

    bool operator==(const GroupedOofReport&) const = default;
};

struct OofGateInputs {
    Metrics c16_train;
    Metrics aq19_train;
    Metrics candidate_train;
    Metrics c16_oof;
    Metrics aq19_oof;
    Metrics candidate_oof;
    std::array<Metrics, aq19::kFoldCount> aq19_folds{};
    std::array<Metrics, aq19::kFoldCount> candidate_folds{};
    std::size_t changed_exact_max_roots = 0;
    bool frozen_inputs_exact = false;
    bool support_identity_exact = false;
    bool comparator_reproduced = false;
    bool full_fit_complete = false;
    bool fold_local_preparation = false;
    bool grouped_oof_exact = false;
    bool repeated_fits_bit_identical = false;

    bool operator==(const OofGateInputs&) const = default;
};

struct Gate {
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const Gate&) const = default;
};

struct DevGateInputs {
    Metrics c16;
    Metrics aq19;
    Metrics candidate;

    bool operator==(const DevGateInputs&) const = default;
};

enum class EvidenceStage : std::uint8_t {
    OofRejected,
    DevRejected,
    CounterPending,
    CounterRejected,
    SelectorsAuthorized,
};

struct ConditionalPath {
    EvidenceStage stage = EvidenceStage::OofRejected;
    bool dev_candidate_opened = false;
    bool counter_gate_opened = false;
    bool c16_selector_seed_opened = false;
    bool aq19_selector_seed_opened = false;
    std::size_t gameplay_games = 0;

    bool operator==(const ConditionalPath&) const = default;
};

struct RunReport {
    std::filesystem::path cache_path;
    std::uintmax_t cache_bytes = 0;
    std::string cache_sha256;
    std::string corpus_digest;
    std::string parent_fingerprint;
    std::string fold_manifest;
    std::string support_digest;
    bool cache_identity_exact = false;
    bool corpus_identity_exact = false;
    bool fold_manifest_exact = false;
    bool support_identity_exact = false;
    bool comparator_reproduced = false;
    aq19::Metrics c16_train;
    aq19::Metrics aq19_train;
    aq19::Metrics c16_oof;
    aq19::Metrics aq19_oof;
    FitReport full_fit;
    FitReport repeated_full_fit;
    GroupedOofReport oof;
    std::size_t changed_exact_max_roots = 0;
    Gate oof_gate;
    std::optional<Metrics> c16_dev;
    std::optional<Metrics> aq19_dev;
    std::optional<Metrics> candidate_dev;
    std::optional<Gate> dev_gate;
    ConditionalPath path;
    bool tactical_seed_opened = false;
    bool selector_seed_opened = false;
    std::size_t gameplay_games = 0;
};

bool parse_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

bool exact_support_identity(
    const support::CensusReport& report);
StageDerivative coordinate_step(
    double gradient, double diagonal);
FitReport fit(
    const aq19::Dataset& dataset,
    const support::PartitionReport& support_report);
GroupedOofReport evaluate_grouped_oof(
    const aq19::Dataset& train,
    const aq19::FoldAssignment& assignment,
    const support::CensusReport& support_report,
    const aq19::FoldReport& aq19_oof);
Gate evaluate_oof_gate(const OofGateInputs& inputs);
Gate evaluate_dev_gate(const DevGateInputs& inputs);
ConditionalPath authorize_path(
    const Gate& oof_gate,
    const std::optional<Gate>& dev_gate,
    std::optional<bool> counter_gate_passed);

RunReport run_offline();
void print_report(
    std::ostream& output, const RunReport& report);

namespace testing {

struct ColumnSpec {
    std::size_t state_feature = 0;
    std::size_t action_feature = 0;
    double sigma = 1.0;
    std::size_t root_support = support::kMinimumRootSupport;
    std::size_t group_support = support::kMinimumGroupSupport;
    double maximum_group_leverage = 0.05;

    bool operator==(const ColumnSpec&) const = default;
};

struct PreparedColumnsProbe {
    std::size_t input_coordinates = 0;
    std::size_t representatives = 0;
    std::vector<std::pair<std::size_t, std::size_t>>
        coordinates;
    std::string canonical_sha256;

    bool operator==(const PreparedColumnsProbe&) const = default;
};

struct DerivativeProbe {
    double objective = 0.0;
    double gradient = 0.0;
    double diagonal = 0.0;

    bool operator==(const DerivativeProbe&) const = default;
};

struct StageCandidate {
    std::size_t state_feature = 0;
    std::size_t action_feature = 0;
    StageDerivative derivative;

    bool operator==(const StageCandidate&) const = default;
};

PreparedColumnsProbe prepare_columns_probe(
    const aq19::Dataset& dataset,
    std::span<const ColumnSpec> columns);
FitReport fit_with_columns(
    const aq19::Dataset& dataset,
    std::span<const ColumnSpec> columns);
DerivativeProbe derivative_probe(
    const aq19::Dataset& dataset,
    std::span<const Term> current_terms,
    const ColumnSpec& candidate);
double objective(
    const aq19::Dataset& dataset,
    std::span<const Term> terms);
std::optional<std::size_t> select_stage_candidate(
    std::span<const StageCandidate> candidates);
std::vector<std::vector<double>> residuals(
    const aq19::Dataset& dataset,
    std::span<const Term> terms);

} // namespace testing

} // namespace old_school::decision_density_sparse_cross
