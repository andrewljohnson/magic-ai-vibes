#pragma once

#include "old_school/decision_density_bilinear.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::decision_density_sparse_support {

namespace aq19 = decision_density_bilinear;
namespace labels = decision_density_labels;

inline constexpr std::string_view kIdentifier =
    "AQ20-DBC6-S0-SPARSE-SUPPORT";
inline constexpr std::string_view kSchema =
    "old-school-aq20-sparse-support-v1";
inline constexpr std::size_t kMinimumRootSupport = 24;
inline constexpr std::size_t kMinimumGroupSupport = 12;
inline constexpr double kMaximumGroupLeverage = 0.10;
inline constexpr std::size_t kRequiredEligibleCoordinates = 16;
inline constexpr std::size_t kCoordinateCount =
    aq19::kStateFeatureCount *
    aq19::kActionFeatureCount;
inline constexpr std::size_t kPartitionCount =
    1 + aq19::kFoldCount;

static_assert(aq19::kStateFeatureCount == 674);
static_assert(aq19::kActionFeatureCount == 219);
static_assert(kCoordinateCount == 147'606);
static_assert(kPartitionCount == 5);

struct CoordinateRow {
    std::size_t root_support = 0;
    std::size_t group_support = 0;
    double weighted_energy = 0.0;
    double maximum_group_weighted_energy = 0.0;
    double maximum_group_leverage = 0.0;
    bool finite = true;
    bool active = false;
    bool eligible = false;

    bool operator==(const CoordinateRow&) const = default;
};

struct PartitionReport {
    std::string name;
    std::size_t roots = 0;
    std::size_t physical_groups = 0;
    std::size_t active_coordinates = 0;
    std::size_t eligible_coordinates = 0;
    std::size_t minimum_eligible_root_support = 0;
    std::size_t minimum_eligible_group_support = 0;
    double maximum_eligible_group_leverage = 0.0;
    std::string canonical_table_sha256;
    std::vector<CoordinateRow> coordinates;

    bool operator==(const PartitionReport&) const = default;
};

struct CensusReport {
    std::array<PartitionReport, kPartitionCount> partitions;
    std::string fold_manifest;
    std::string cross_partition_digest;
    bool every_partition_has_16_eligible = false;
    std::size_t teacher_fields_read = 0;
    std::size_t candidate_scores = 0;
    std::size_t selected_terms = 0;
    std::size_t optimizer_steps = 0;
    std::size_t model_created = 0;
    bool tactical_seed_opened = false;
    bool selector_seed_opened = false;
    std::size_t gameplay_games = 0;

    bool operator==(const CensusReport&) const = default;
};

struct RunReport {
    std::filesystem::path cache_path;
    std::uintmax_t cache_bytes = 0;
    std::string cache_sha256;
    std::string corpus_digest;
    CensusReport census;
    bool cache_identity_exact = false;
    bool corpus_identity_exact = false;
    bool fold_manifest_exact = false;
};

bool parse_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

// These functions consume only identities and AQ19's 674/219 feature
// projection. They deliberately do not inspect any base/teacher score,
// sample, target, or accounting field.
aq19::Dataset project_train_label_blind(
    const labels::Corpus& source);
PartitionReport census_partition(
    std::string name,
    const aq19::Dataset& dataset);
CensusReport census(
    const aq19::Dataset& train,
    const aq19::FoldAssignment& folds);

std::string canonical_partition_table(
    const PartitionReport& report);
std::string canonical_report_bytes(
    const CensusReport& report);

RunReport run();
void print_report(
    std::ostream& output, const RunReport& report);

} // namespace old_school::decision_density_sparse_support
