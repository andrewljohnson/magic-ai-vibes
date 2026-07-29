#pragma once

#include "old_school/action_q_nested_actor_broad_distill.hpp"
#include "old_school/decision_density_priority.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::decision_density_labels {

namespace density = decision_density_census;
namespace priority = decision_density_priority;
namespace aq4 = action_q_nested_actor_broad_distill;

inline constexpr std::string_view kIdentifier =
    "AQ18-DBC6-L1-DEEP-LABEL-CACHE";
inline constexpr std::string_view kCacheSchema =
    "old-school-aq18-dbc6-label-cache-v1";
inline constexpr std::uint64_t kCacheVersion = 1;
inline constexpr std::uint64_t kLabelSeed =
    202607291802ULL;
inline constexpr std::string_view kRequiredParentFingerprint =
    density::kRequiredParentFingerprint;
inline constexpr std::string_view kRequiredSourceManifest =
    priority::kRequiredCensusManifest;
inline constexpr std::string_view kRequiredSelectionManifest =
    priority::kRequiredSelectionManifest;
inline constexpr std::string_view kRequiredPreflightDigest =
    aq4::kFrozenPreflightDigest;
inline constexpr std::string_view kProductionCachePath =
    "build/model-cache/"
    "old-school-aq18-dbc6-label-cache-v1.bin";
inline constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
inline constexpr std::uintmax_t kParentArtifactBytes =
    3'111'437;
inline constexpr std::string_view kParentArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";
inline constexpr std::size_t kWorlds = 8;
inline constexpr std::size_t kExpectedTrainRoots =
    priority::kExpectedTrainRoots;
inline constexpr std::size_t kExpectedDevRoots =
    priority::kExpectedDevRoots;
inline constexpr std::size_t kExpectedTrainOptions = 1088;
inline constexpr std::size_t kExpectedDevOptions = 513;
inline constexpr std::size_t kExpectedCells =
    2 * kDeckCount * priority::kWidthStrata;
inline constexpr std::size_t kExpectedAliasPairs = 91;
inline constexpr std::size_t kExpectedActionWorldCellsPerArm =
    (kExpectedTrainOptions + kExpectedDevOptions) * kWorlds;
inline constexpr std::size_t kMaximumCacheBytes =
    64U * 1024U * 1024U;
inline constexpr double kTrainRootWeight = 1.0 / 60.0;
inline constexpr double kDevRootWeight = 1.0 / 30.0;
inline constexpr double kStablePairMinimumDelta = 0.03;
inline constexpr double kNormal95CriticalValue = 1.96;
inline constexpr double kMaterialCorrectionGap = 0.01;

static_assert(kExpectedTrainRoots == 300);
static_assert(kExpectedDevRoots == 150);
static_assert(kExpectedCells == 30);
static_assert(kExpectedActionWorldCellsPerArm == 12'808);
static_assert(density::kPolicyFeatureCount == 893);

enum class Command : std::uint8_t {
    Publish,
};

// This is the complete persisted root identity. The source schedule is
// deliberately projected to opaque domain-separated hashes before it enters
// this type.
struct ProjectedRoot {
    density::Split split = density::Split::Train;
    DeckId owner_deck = DeckId::Green;
    priority::WidthStratum width_stratum =
        priority::WidthStratum::B2;
    std::string stable_root_id;
    std::string selection_key;
    std::string information_action_fingerprint;
    std::string physical_game_group;
    std::string actor_game_group;
    std::uint64_t label_seed = 0;

    bool operator==(const ProjectedRoot&) const = default;
};

struct SearchAccounting {
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t bootstrapped_evaluations = 0;
    std::size_t inner_rollout_evaluations = 0;
    std::size_t inner_search_invocations = 0;
    std::size_t inner_search_max_depth = 0;
    std::vector<std::vector<std::size_t>>
        inner_rollout_evaluations_by_cell;
    std::vector<std::vector<std::size_t>>
        inner_search_invocations_by_cell;
    std::vector<std::vector<std::size_t>>
        inner_search_max_depth_by_cell;

    bool operator==(const SearchAccounting&) const = default;
};

struct RootLabel {
    ProjectedRoot identity;
    std::vector<PriorityAction> actions;
    std::vector<std::string> action_descriptors;
    std::vector<std::vector<double>> option_rows;
    std::vector<std::vector<double>> base_q_samples;
    std::vector<std::vector<std::uint8_t>>
        base_terminal_flags;
    std::vector<std::vector<double>>
        base_shallow_prior_samples;
    std::vector<std::vector<double>>
        base_continuation_samples;
    std::vector<double> base_aggregate_scores;
    SearchAccounting base_accounting;
    std::vector<std::vector<double>> teacher_q_samples;
    std::vector<std::vector<std::uint8_t>>
        teacher_terminal_flags;
    std::vector<std::vector<double>>
        teacher_shallow_prior_samples;
    std::vector<std::vector<double>>
        teacher_continuation_samples;
    std::vector<double> teacher_aggregate_scores;
    SearchAccounting teacher_accounting;
    std::vector<double> target_probabilities;
    double weight = 0.0;

    bool operator==(const RootLabel&) const = default;
};

struct CellMetrics {
    density::Split split = density::Split::Train;
    DeckId owner_deck = DeckId::Green;
    priority::WidthStratum width_stratum =
        priority::WidthStratum::B2;
    std::size_t roots = 0;
    std::size_t options = 0;
    std::size_t stable_pairs = 0;
    std::size_t differing_roots = 0;
    std::size_t alias_pairs = 0;
    std::size_t material_alias_conflicts = 0;
    double weight_mass = 0.0;
    double exact_max_agreement = 0.0;
    double stable_pair_agreement = 0.0;
    double listwise_cross_entropy = 0.0;
    double teacher_regret = 0.0;
    double maximum_absolute_alias_correction_gap = 0.0;

    bool operator==(const CellMetrics&) const = default;
};

struct AliasDiagnostic {
    density::Split split = density::Split::Train;
    DeckId owner_deck = DeckId::Green;
    priority::WidthStratum width_stratum =
        priority::WidthStratum::B4Plus;
    std::string stable_root_id;
    std::size_t left_action = 0;
    std::size_t right_action = 0;
    std::array<double, kWorlds> paired_corrections{};
    double paired_mean = 0.0;
    double paired_standard_error = 0.0;
    double correction_gap = 0.0;
    double target_probability_gap = 0.0;
    bool material_conflict = false;

    bool operator==(const AliasDiagnostic&) const = default;
};

struct Diagnostics {
    std::array<CellMetrics, kExpectedCells> cells{};
    std::vector<AliasDiagnostic> aliases;
    std::array<bool, 2 * kDeckCount> deck_signal{};
    std::size_t material_alias_conflicts = 0;
    double maximum_absolute_alias_correction_gap = 0.0;
    double equal_deck_train_exact_max_agreement = 0.0;
    double equal_deck_train_stable_pair_agreement = 0.0;
    double equal_deck_train_listwise_cross_entropy = 0.0;
    double equal_deck_train_teacher_regret = 0.0;
    double equal_deck_dev_exact_max_agreement = 0.0;
    double equal_deck_dev_stable_pair_agreement = 0.0;
    double equal_deck_dev_listwise_cross_entropy = 0.0;
    double equal_deck_dev_teacher_regret = 0.0;

    bool operator==(const Diagnostics&) const = default;
};

struct Corpus {
    std::string parent_artifact_sha256;
    std::string parent_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    std::string source_manifest_hash;
    std::string selection_manifest_hash;
    std::string preflight_digest;
    LearnedSearchConfig base_search_template;
    LearnedSearchConfig teacher_search_template;
    std::vector<RootLabel> train;
    std::vector<RootLabel> dev;
    Diagnostics diagnostics;
    std::string digest;

    bool operator==(const Corpus&) const = default;
};

struct RunReport {
    Corpus corpus;
    std::filesystem::path cache_path;
    std::uintmax_t cache_bytes = 0;
    std::string cache_sha256;
    bool sentinel_labels_bit_identical = false;
    std::size_t sentinel_relabels = 0;
    bool hidden_repartition_bit_identical = false;
    bool roundtrip_bit_identical = false;
    bool strict_reload_bit_identical = false;
    bool parent_immutable = false;
    bool artifact_published = false;
};

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

LearnedSearchConfig base_search_config(
    std::uint64_t seed);
LearnedSearchConfig teacher_search_config(
    std::uint64_t seed);
std::uint64_t root_label_seed(
    const density::RootCoordinate& coordinate);

std::string canonical_corpus_digest(
    const Corpus& corpus);
void validate_root_label(const RootLabel& root);
void validate_corpus(const Corpus& corpus);
Diagnostics evaluate_diagnostics(
    std::span<const RootLabel> train,
    std::span<const RootLabel> dev,
    std::span<const priority::AliasGroup> aliases);

std::string encode_cache(const Corpus& corpus);
Corpus decode_cache(std::string_view bytes);
Corpus roundtrip_cache(const Corpus& corpus);
void write_cache_atomic_no_replace(
    const std::filesystem::path& path,
    const Corpus& corpus);
Corpus load_cache(
    const std::filesystem::path& path);

RunReport run_and_publish(
    std::shared_ptr<const LearnedModel> parent,
    const std::filesystem::path& path =
        std::filesystem::path(kProductionCachePath));
void print_report(
    std::ostream& output, const RunReport& report);

namespace testing {

ProjectedRoot project_root(
    const priority::SelectedRoot& selected);
RootLabel make_root_label(
    ProjectedRoot identity,
    std::vector<PriorityAction> actions,
    std::vector<std::string> descriptors,
    std::vector<std::vector<double>> option_rows,
    const LearnedActionSamples& base,
    const LearnedActionSamples& teacher);
Corpus make_unfrozen_corpus(
    std::vector<RootLabel> train,
    std::vector<RootLabel> dev,
    std::vector<priority::AliasGroup> aliases);
std::string encode_unfrozen_cache(
    const Corpus& corpus);
Corpus decode_unfrozen_cache(
    std::string_view bytes);
void write_bytes_atomic_no_replace(
    const std::filesystem::path& path,
    std::string_view bytes);

} // namespace testing

} // namespace old_school::decision_density_labels
