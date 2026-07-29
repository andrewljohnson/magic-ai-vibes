#pragma once

#include "old_school/decision_density_census.hpp"

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

namespace old_school::decision_density_priority {

namespace density = decision_density_census;

inline constexpr std::string_view kIdentifier =
    "AQ17-DBC6-S0-DENSITY-SELECT";
inline constexpr std::string_view kSelectionSchema =
    "old-school-aq17-dbc6-select-v1";
inline constexpr std::uint64_t kSelectionSeed =
    202607291811ULL;
inline constexpr std::string_view kRequiredCensusManifest =
    "7de71c44a3d1d1fa20eb1b738bc8c675e83c4336f284e95f7401a4b79ea345cc";
inline constexpr std::size_t kRequiredTrainRoots = 3597;
inline constexpr std::size_t kRequiredDevRoots = 1687;
inline constexpr std::size_t kTrainRootsPerCell = 20;
inline constexpr std::size_t kDevRootsPerCell = 10;
inline constexpr std::size_t kWidthStrata = 3;
inline constexpr std::size_t kActorGameCellCap = 2;
inline constexpr std::size_t kActorGameTotalCap =
    kActorGameCellCap * kWidthStrata;
inline constexpr std::size_t kExpectedTrainRoots =
    kDeckCount * kWidthStrata * kTrainRootsPerCell;
inline constexpr std::size_t kExpectedDevRoots =
    kDeckCount * kWidthStrata * kDevRootsPerCell;

static_assert(kExpectedTrainRoots == 300);
static_assert(kExpectedDevRoots == 150);
static_assert(kActorGameTotalCap == 6);

enum class Command : std::uint8_t {
    Select,
};

enum class WidthStratum : std::uint8_t {
    B2 = 0,
    B3 = 1,
    B4Plus = 2,
};

// Ephemeral authenticated replay material. This type is intentionally not
// retained by SelectionManifest or RunReport.
struct PopulationRoot {
    density::ManifestRoot source_root;
    std::vector<PriorityAction> actions;
    std::vector<std::vector<double>> option_rows;
    bool hidden_repartition_witness = false;
};

struct SelectedRoot {
    density::ManifestRoot source_root;
    WidthStratum width_stratum = WidthStratum::B2;
    std::string selection_key;

    bool operator==(const SelectedRoot&) const = default;
};

struct AliasGroup {
    density::Split split = density::Split::Train;
    DeckId owner_deck = DeckId::Green;
    WidthStratum width_stratum = WidthStratum::B2;
    std::string stable_root_id;
    std::vector<std::string> action_descriptors;

    bool operator==(const AliasGroup&) const = default;
};

struct CellCensus {
    density::Split split = density::Split::Train;
    DeckId owner_deck = DeckId::Green;
    WidthStratum width_stratum = WidthStratum::B2;
    std::size_t quota = 0;
    std::size_t source_roots = 0;
    std::size_t source_distinct_actor_games = 0;
    std::size_t source_two_round_capacity = 0;
    std::size_t selected_roots = 0;
    std::size_t selected_options = 0;
    std::size_t selected_potential_pairs = 0;
    std::size_t selected_distinct_actor_games = 0;
    std::array<std::size_t, 2> actor_seats{};
    std::array<std::size_t, 2> play_draw{};
    std::size_t max_roots_per_actor_game = 0;
    std::size_t collision_groups = 0;
    std::size_t collision_pairs = 0;
    std::size_t option_pair_denominator = 0;

    bool operator==(const CellCensus&) const = default;
};

struct SelectionManifest {
    std::string parent_fingerprint;
    std::string source_manifest_hash;
    std::uint64_t selection_seed = kSelectionSeed;
    std::size_t train_quota_per_cell =
        kTrainRootsPerCell;
    std::size_t dev_quota_per_cell =
        kDevRootsPerCell;
    std::vector<SelectedRoot> selected_roots;
    std::array<CellCensus, 2 * kDeckCount * kWidthStrata>
        cells{};
    std::vector<AliasGroup> alias_groups;
    std::size_t train_roots = 0;
    std::size_t dev_roots = 0;
    std::size_t selected_options = 0;
    std::size_t selected_potential_pairs = 0;
    std::size_t distinct_physical_games = 0;
    std::size_t max_roots_per_actor_game = 0;
    std::size_t collision_pairs = 0;
    std::string manifest_hash;

    bool operator==(const SelectionManifest&) const = default;
};

struct RunReport {
    SelectionManifest manifest;
    bool repeated_selector_bit_identical = false;
    bool hidden_repartition_witness = false;
    std::string hidden_witness_root_id;
    std::size_t source_collections = 0;
};

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

WidthStratum width_stratum(std::size_t legal_actions);
std::size_t cell_index(
    density::Split split, DeckId deck,
    WidthStratum stratum);
std::string selection_key(
    std::string_view source_manifest_hash,
    std::string_view stable_root_id);
std::string canonical_manifest_hash(
    const SelectionManifest& manifest);
void validate_manifest(
    const SelectionManifest& manifest);

RunReport run(std::shared_ptr<const LearnedModel> parent);
void print_report(
    std::ostream& output, const RunReport& report);

namespace testing {

SelectionManifest select_population(
    std::string parent_fingerprint,
    std::string source_manifest_hash,
    std::span<const PopulationRoot> population,
    std::size_t train_quota_per_cell,
    std::size_t dev_quota_per_cell);
bool rows_bit_identical(
    std::span<const double> left,
    std::span<const double> right);

} // namespace testing

} // namespace old_school::decision_density_priority
