#include "old_school/decision_density_priority.hpp"

#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace old_school::decision_density_priority {
namespace {

constexpr std::string_view kManifestSchema =
    "old-school-aq17-dbc6-selected-manifest-v1";

struct ActorGameKey {
    density::Split split = density::Split::Train;
    std::size_t block_index = 0;
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::uint64_t game_seed = 0;
    std::size_t starting_player = 0;
    std::array<DeckId, 2> seat_decks{
        DeckId::Green,
        DeckId::Red,
    };
    std::size_t actor = 0;

    auto operator<=>(const ActorGameKey&) const = default;
};

struct PhysicalGameKey {
    density::Split split = density::Split::Train;
    std::size_t block_index = 0;
    std::size_t schedule_index = 0;
    std::uint64_t game_seed = 0;

    auto operator<=>(const PhysicalGameKey&) const = default;
};

struct Candidate {
    std::size_t source_index = 0;
    std::string key;
};

struct SourceCell {
    std::vector<Candidate> candidates;
    std::map<ActorGameKey, std::vector<Candidate>>
        by_actor_game;
};

bool sha256_is_canonical(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return
                       (character >= '0' &&
                        character <= '9') ||
                       (character >= 'a' &&
                        character <= 'f');
               });
}

void require_parent(
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            density::kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ17-DBC6 requires exact frozen C16");
    }
}

std::size_t deck_index(DeckId deck) {
    const std::size_t index =
        static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::out_of_range(
            "AQ17-DBC6 deck is invalid");
    }
    return index;
}

std::size_t stratum_index(WidthStratum stratum) {
    const std::size_t index =
        static_cast<std::size_t>(stratum);
    if (index >= kWidthStrata) {
        throw std::out_of_range(
            "AQ17-DBC6 width stratum is invalid");
    }
    return index;
}

std::string_view split_name(density::Split split) {
    switch (split) {
    case density::Split::Train:
        return "TRAIN";
    case density::Split::Dev:
        return "DEV";
    }
    throw std::out_of_range(
        "AQ17-DBC6 split is invalid");
}

std::string_view stratum_name(WidthStratum stratum) {
    switch (stratum) {
    case WidthStratum::B2:
        return "B2";
    case WidthStratum::B3:
        return "B3";
    case WidthStratum::B4Plus:
        return "B4+";
    }
    throw std::out_of_range(
        "AQ17-DBC6 width stratum is invalid");
}

ActorGameKey actor_game_key(
    const density::RootCoordinate& coordinate) {
    return {
        .split = coordinate.split,
        .block_index = coordinate.block_index,
        .schedule_index = coordinate.schedule_index,
        .pairing_index = coordinate.pairing_index,
        .game_seed = coordinate.game_seed,
        .starting_player = coordinate.starting_player,
        .seat_decks = coordinate.seat_decks,
        .actor = coordinate.actor,
    };
}

PhysicalGameKey physical_game_key(
    const density::RootCoordinate& coordinate) {
    return {
        .split = coordinate.split,
        .block_index = coordinate.block_index,
        .schedule_index = coordinate.schedule_index,
        .game_seed = coordinate.game_seed,
    };
}

auto canonical_root_order(
    const density::RootCoordinate& coordinate) {
    return std::tuple{
        density::split_index(coordinate.split),
        coordinate.block_index,
        coordinate.schedule_index,
        coordinate.actor,
        coordinate.nontrivial_ordinal,
        coordinate.trace_ordinal,
    };
}

void checked_add(
    std::size_t& total, std::size_t increment,
    std::string_view label) {
    if (increment >
        std::numeric_limits<std::size_t>::max() - total) {
        throw std::overflow_error(
            "AQ17-DBC6 " + std::string(label) +
            " overflow");
    }
    total += increment;
}

void append_u64(
    std::string& output, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<char>(
            (value >> shift) & 0xffU));
    }
}

void append_size(
    std::string& output, std::size_t value) {
    append_u64(output, static_cast<std::uint64_t>(value));
}

void append_string(
    std::string& output, std::string_view value) {
    append_size(output, value.size());
    output.append(value);
}

void append_coordinate(
    std::string& output,
    const density::RootCoordinate& coordinate) {
    append_u64(
        output,
        static_cast<std::uint64_t>(coordinate.split));
    append_size(output, coordinate.block_index);
    append_size(output, coordinate.schedule_index);
    append_size(output, coordinate.pairing_index);
    append_u64(output, coordinate.game_seed);
    append_size(output, coordinate.starting_player);
    for (const DeckId deck : coordinate.seat_decks) {
        append_u64(
            output, static_cast<std::uint64_t>(deck));
    }
    append_size(output, coordinate.actor);
    append_size(output, coordinate.trace_ordinal);
    append_size(output, coordinate.nontrivial_ordinal);
    append_size(
        output,
        coordinate.actor_game_nontrivial_roots);
}

void append_source_root(
    std::string& output,
    const density::ManifestRoot& root) {
    append_coordinate(output, root.coordinate);
    append_size(output, root.legal_action_count);
    append_size(output, root.potential_pairs);
    append_size(output, root.action_descriptors.size());
    for (const std::string& descriptor :
         root.action_descriptors) {
        append_string(output, descriptor);
    }
    append_string(
        output, root.information_action_fingerprint);
    append_string(output, root.stable_root_id);
}

void append_cell(
    std::string& output, const CellCensus& cell) {
    append_u64(
        output,
        static_cast<std::uint64_t>(cell.split));
    append_u64(
        output,
        static_cast<std::uint64_t>(cell.owner_deck));
    append_u64(
        output,
        static_cast<std::uint64_t>(
            cell.width_stratum));
    append_size(output, cell.quota);
    append_size(output, cell.source_roots);
    append_size(
        output, cell.source_distinct_actor_games);
    append_size(
        output, cell.source_two_round_capacity);
    append_size(output, cell.selected_roots);
    append_size(output, cell.selected_options);
    append_size(
        output, cell.selected_potential_pairs);
    append_size(
        output, cell.selected_distinct_actor_games);
    for (const std::size_t count : cell.actor_seats) {
        append_size(output, count);
    }
    for (const std::size_t count : cell.play_draw) {
        append_size(output, count);
    }
    append_size(
        output, cell.max_roots_per_actor_game);
    append_size(output, cell.collision_groups);
    append_size(output, cell.collision_pairs);
    append_size(
        output, cell.option_pair_denominator);
}

void append_alias_group(
    std::string& output, const AliasGroup& group) {
    append_u64(
        output,
        static_cast<std::uint64_t>(group.split));
    append_u64(
        output,
        static_cast<std::uint64_t>(
            group.owner_deck));
    append_u64(
        output,
        static_cast<std::uint64_t>(
            group.width_stratum));
    append_string(output, group.stable_root_id);
    append_size(
        output, group.action_descriptors.size());
    for (const std::string& descriptor :
         group.action_descriptors) {
        append_string(output, descriptor);
    }
}

bool rows_bit_identical_impl(
    std::span<const double> left,
    std::span<const double> right) {
    return left.size() == right.size() &&
           std::equal(
               left.begin(), left.end(), right.begin(),
               [](double first, double second) {
                   return
                       std::bit_cast<std::uint64_t>(first) ==
                       std::bit_cast<std::uint64_t>(second);
               });
}

std::string canonical_row_bits(
    std::span<const double> row) {
    std::string result;
    result.reserve(row.size() * sizeof(std::uint64_t));
    for (const double value : row) {
        append_u64(
            result, std::bit_cast<std::uint64_t>(value));
    }
    return result;
}

void authenticate_population_root(
    const PopulationRoot& root) {
    density::validate_manifest_root(root.source_root);
    if (root.actions.size() !=
            root.source_root.legal_action_count ||
        root.option_rows.size() != root.actions.size() ||
        density::canonical_information_action_fingerprint(
            root.actions,
            root.source_root.action_descriptors,
            root.option_rows) !=
            root.source_root
                .information_action_fingerprint) {
        throw std::invalid_argument(
            "AQ17-DBC6 live root authentication failed");
    }
}

std::vector<std::size_t> select_cell(
    SourceCell& source, std::size_t quota,
    CellCensus& census) {
    if (quota == 0) {
        throw std::invalid_argument(
            "AQ17-DBC6 cell quota is zero");
    }
    census.quota = quota;
    census.source_roots = source.candidates.size();
    census.source_distinct_actor_games =
        source.by_actor_game.size();
    for (auto& [unused, candidates] :
         source.by_actor_game) {
        static_cast<void>(unused);
        std::sort(
            candidates.begin(), candidates.end(),
            [](const Candidate& left,
               const Candidate& right) {
                return left.key < right.key;
            });
        checked_add(
            census.source_two_round_capacity,
            std::min(
                kActorGameCellCap,
                candidates.size()),
            "source two-round capacity");
    }
    if (census.source_two_round_capacity < quota) {
        throw std::runtime_error(
            "AQ17-DBC6 cell lacks two-round capacity");
    }

    std::vector<std::size_t> selected;
    selected.reserve(quota);
    for (std::size_t round = 0;
         round < kActorGameCellCap &&
         selected.size() < quota;
         ++round) {
        std::vector<Candidate> offers;
        for (const auto& [unused, candidates] :
             source.by_actor_game) {
            static_cast<void>(unused);
            if (candidates.size() > round) {
                offers.push_back(candidates[round]);
            }
        }
        std::sort(
            offers.begin(), offers.end(),
            [](const Candidate& left,
               const Candidate& right) {
                return left.key < right.key;
            });
        for (const Candidate& offer : offers) {
            if (selected.size() == quota) {
                break;
            }
            selected.push_back(offer.source_index);
        }
    }
    if (selected.size() != quota) {
        throw std::runtime_error(
            "AQ17-DBC6 selector did not fill a cell");
    }
    return selected;
}

void add_alias_groups(
    const PopulationRoot& root,
    WidthStratum stratum,
    CellCensus& cell,
    std::vector<AliasGroup>& aliases) {
    std::map<std::string, std::vector<std::size_t>>
        row_groups;
    for (std::size_t option = 0;
         option < root.option_rows.size(); ++option) {
        row_groups[canonical_row_bits(
                       root.option_rows[option])]
            .push_back(option);
    }
    std::vector<std::vector<std::size_t>> collisions;
    for (auto& [unused, indices] : row_groups) {
        static_cast<void>(unused);
        if (indices.size() >= 2) {
            collisions.push_back(std::move(indices));
        }
    }
    std::sort(
        collisions.begin(), collisions.end(),
        [](const auto& left, const auto& right) {
            return left.front() < right.front();
        });
    for (const auto& indices : collisions) {
        AliasGroup group{
            .split = root.source_root.coordinate.split,
            .owner_deck =
                root.source_root.coordinate.owner_deck(),
            .width_stratum = stratum,
            .stable_root_id =
                root.source_root.stable_root_id,
        };
        group.action_descriptors.reserve(indices.size());
        for (const std::size_t index : indices) {
            group.action_descriptors.push_back(
                root.source_root
                    .action_descriptors[index]);
        }
        const std::size_t pairs =
            density::potential_pair_count(indices.size());
        checked_add(
            cell.collision_pairs, pairs,
            "cell collision-pair count");
        checked_add(
            cell.collision_groups, 1,
            "cell collision-group count");
        aliases.push_back(std::move(group));
    }
}

SelectionManifest select_population_impl(
    std::string parent_fingerprint,
    std::string source_manifest_hash,
    std::span<const PopulationRoot> population,
    std::size_t train_quota_per_cell,
    std::size_t dev_quota_per_cell) {
    if (parent_fingerprint !=
            density::kRequiredParentFingerprint ||
        !sha256_is_canonical(source_manifest_hash) ||
        train_quota_per_cell == 0 ||
        dev_quota_per_cell == 0) {
        throw std::invalid_argument(
            "AQ17-DBC6 selection identity is invalid");
    }

    std::array<SourceCell,
               2 * kDeckCount * kWidthStrata>
        source_cells;
    std::set<std::string> stable_root_ids;
    std::set<std::string> selection_keys;
    std::optional<decltype(canonical_root_order(
        std::declval<density::RootCoordinate>()))>
        previous_order;
    for (std::size_t index = 0;
         index < population.size(); ++index) {
        const PopulationRoot& root = population[index];
        authenticate_population_root(root);
        const auto order =
            canonical_root_order(
                root.source_root.coordinate);
        if (previous_order.has_value() &&
            order <= *previous_order) {
            throw std::invalid_argument(
                "AQ17-DBC6 source order is noncanonical");
        }
        previous_order = order;
        if (!stable_root_ids.insert(
                 root.source_root.stable_root_id)
                 .second) {
            throw std::invalid_argument(
                "AQ17-DBC6 duplicate stable root ID");
        }
        const std::string key =
            selection_key(
                source_manifest_hash,
                root.source_root.stable_root_id);
        if (!selection_keys.insert(key).second) {
            throw std::invalid_argument(
                "AQ17-DBC6 duplicate selection key");
        }
        const WidthStratum stratum =
            width_stratum(
                root.source_root.legal_action_count);
        SourceCell& cell =
            source_cells[cell_index(
                root.source_root.coordinate.split,
                root.source_root.coordinate.owner_deck(),
                stratum)];
        const Candidate candidate{
            .source_index = index,
            .key = key,
        };
        cell.candidates.push_back(candidate);
        cell.by_actor_game[
            actor_game_key(
                root.source_root.coordinate)]
            .push_back(candidate);
    }

    SelectionManifest manifest{
        .parent_fingerprint =
            std::move(parent_fingerprint),
        .source_manifest_hash =
            std::move(source_manifest_hash),
        .selection_seed = kSelectionSeed,
        .train_quota_per_cell =
            train_quota_per_cell,
        .dev_quota_per_cell =
            dev_quota_per_cell,
    };
    std::set<std::size_t> selected_indices;
    for (std::size_t split = 0; split < 2; ++split) {
        const density::Split split_value =
            split == 0
                ? density::Split::Train
                : density::Split::Dev;
        const std::size_t quota =
            split == 0
                ? train_quota_per_cell
                : dev_quota_per_cell;
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            for (std::size_t stratum = 0;
                 stratum < kWidthStrata; ++stratum) {
                const auto deck_value =
                    static_cast<DeckId>(deck);
                const auto stratum_value =
                    static_cast<WidthStratum>(stratum);
                const std::size_t position =
                    cell_index(
                        split_value, deck_value,
                        stratum_value);
                CellCensus& cell =
                    manifest.cells[position];
                cell.split = split_value;
                cell.owner_deck = deck_value;
                cell.width_stratum = stratum_value;
                const auto selected =
                    select_cell(
                        source_cells[position],
                        quota, cell);
                for (const std::size_t source_index :
                     selected) {
                    if (!selected_indices.insert(
                             source_index)
                             .second) {
                        throw std::logic_error(
                            "AQ17-DBC6 selected a root twice");
                    }
                }
            }
        }
    }

    std::map<ActorGameKey, std::size_t>
        selected_per_actor_game;
    std::set<PhysicalGameKey> physical_games;
    for (const std::size_t source_index :
         selected_indices) {
        const PopulationRoot& source =
            population[source_index];
        const WidthStratum stratum =
            width_stratum(
                source.source_root.legal_action_count);
        const std::size_t position =
            cell_index(
                source.source_root.coordinate.split,
                source.source_root.coordinate.owner_deck(),
                stratum);
        CellCensus& cell = manifest.cells[position];
        manifest.selected_roots.push_back({
            .source_root = source.source_root,
            .width_stratum = stratum,
            .selection_key =
                selection_key(
                    manifest.source_manifest_hash,
                    source.source_root.stable_root_id),
        });
        checked_add(
            cell.selected_roots, 1,
            "cell selected-root count");
        checked_add(
            cell.selected_options,
            source.source_root.legal_action_count,
            "cell selected-option count");
        checked_add(
            cell.selected_potential_pairs,
            source.source_root.potential_pairs,
            "cell selected-pair count");
        cell.option_pair_denominator =
            cell.selected_potential_pairs;
        ++cell.actor_seats[
            source.source_root.coordinate.actor];
        ++cell.play_draw[
            source.source_root.coordinate.actor ==
                    source.source_root.coordinate
                        .starting_player
                ? 0
                : 1];
        ++selected_per_actor_game[
            actor_game_key(
                source.source_root.coordinate)];
        physical_games.insert(
            physical_game_key(
                source.source_root.coordinate));
        add_alias_groups(
            source, stratum, cell,
            manifest.alias_groups);
    }

    manifest.distinct_physical_games =
        physical_games.size();
    for (const auto& [unused, count] :
         selected_per_actor_game) {
        static_cast<void>(unused);
        manifest.max_roots_per_actor_game =
            std::max(
                manifest.max_roots_per_actor_game,
                count);
    }
    for (CellCensus& cell : manifest.cells) {
        std::map<ActorGameKey, std::size_t> counts;
        for (const SelectedRoot& root :
             manifest.selected_roots) {
            if (root.source_root.coordinate.split ==
                    cell.split &&
                root.source_root.coordinate.owner_deck() ==
                    cell.owner_deck &&
                root.width_stratum ==
                    cell.width_stratum) {
                ++counts[actor_game_key(
                    root.source_root.coordinate)];
            }
        }
        cell.selected_distinct_actor_games =
            counts.size();
        for (const auto& [unused, count] : counts) {
            static_cast<void>(unused);
            cell.max_roots_per_actor_game =
                std::max(
                    cell.max_roots_per_actor_game,
                    count);
        }
        if (cell.selected_roots != cell.quota ||
            cell.selected_distinct_actor_games <
                (cell.quota + 1) / 2 ||
            cell.max_roots_per_actor_game >
                kActorGameCellCap) {
            throw std::runtime_error(
                "AQ17-DBC6 game-diversity gate failed");
        }
        if (cell.split == density::Split::Train) {
            checked_add(
                manifest.train_roots,
                cell.selected_roots,
                "TRAIN selected-root count");
        } else {
            checked_add(
                manifest.dev_roots,
                cell.selected_roots,
                "DEV selected-root count");
        }
        checked_add(
            manifest.selected_options,
            cell.selected_options,
            "selected-option count");
        checked_add(
            manifest.selected_potential_pairs,
            cell.selected_potential_pairs,
            "selected-pair count");
        checked_add(
            manifest.collision_pairs,
            cell.collision_pairs,
            "collision-pair count");
    }
    if (manifest.max_roots_per_actor_game >
            kActorGameTotalCap) {
        throw std::runtime_error(
            "AQ17-DBC6 total actor-game cap failed");
    }
    manifest.manifest_hash =
        canonical_manifest_hash(manifest);
    validate_manifest(manifest);
    return manifest;
}

} // namespace

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments) {
    if (arguments.size() == 1 &&
        arguments.front() == "--select") {
        return Command::Select;
    }
    return std::nullopt;
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-decision-density-priority "
           "--select\n";
}

WidthStratum width_stratum(
    std::size_t legal_actions) {
    if (legal_actions == 2) {
        return WidthStratum::B2;
    }
    if (legal_actions == 3) {
        return WidthStratum::B3;
    }
    if (legal_actions >= 4) {
        return WidthStratum::B4Plus;
    }
    throw std::invalid_argument(
        "AQ17-DBC6 legal-action width is trivial");
}

std::size_t cell_index(
    density::Split split, DeckId deck,
    WidthStratum stratum) {
    return
        (density::split_index(split) * kDeckCount +
         deck_index(deck)) *
            kWidthStrata +
        stratum_index(stratum);
}

std::string selection_key(
    std::string_view source_manifest_hash,
    std::string_view stable_root_id) {
    if (!sha256_is_canonical(source_manifest_hash) ||
        !sha256_is_canonical(stable_root_id)) {
        throw std::invalid_argument(
            "AQ17-DBC6 selection-key identity is invalid");
    }
    std::string payload;
    append_string(payload, kSelectionSchema);
    append_string(payload, source_manifest_hash);
    append_string(
        payload, std::to_string(kSelectionSeed));
    append_string(payload, stable_root_id);
    return artifact_integrity::sha256_string(payload);
}

std::string canonical_manifest_hash(
    const SelectionManifest& manifest) {
    std::string payload;
    append_string(payload, kManifestSchema);
    append_string(payload, kIdentifier);
    append_string(payload, density::kIdentifier);
    append_u64(payload, density::kCollectionRootSeed);
    append_size(payload, density::kScheduleGeneration);
    append_size(payload, density::kTrainBlocks.size());
    for (const std::size_t block :
         density::kTrainBlocks) {
        append_size(payload, block);
    }
    append_size(payload, density::kDevBlock);
    append_size(payload, kRequiredTrainRoots);
    append_size(payload, kRequiredDevRoots);
    append_size(payload, density::kPolicyFeatureCount);
    append_string(payload, kSelectionSchema);
    append_string(payload, manifest.parent_fingerprint);
    append_string(payload, manifest.source_manifest_hash);
    append_u64(payload, manifest.selection_seed);
    append_size(
        payload, manifest.train_quota_per_cell);
    append_size(
        payload, manifest.dev_quota_per_cell);
    append_size(payload, 2);
    append_size(payload, 3);
    append_size(payload, 4);
    append_size(payload, kActorGameCellCap);
    append_size(payload, kActorGameTotalCap);
    append_size(payload, manifest.cells.size());
    for (const CellCensus& cell : manifest.cells) {
        append_cell(payload, cell);
    }
    append_size(
        payload, manifest.selected_roots.size());
    for (const SelectedRoot& root :
         manifest.selected_roots) {
        append_source_root(payload, root.source_root);
        append_u64(
            payload,
            static_cast<std::uint64_t>(
                root.width_stratum));
        append_string(payload, root.selection_key);
    }
    append_size(payload, manifest.alias_groups.size());
    for (const AliasGroup& group :
         manifest.alias_groups) {
        append_alias_group(payload, group);
    }
    append_size(payload, manifest.train_roots);
    append_size(payload, manifest.dev_roots);
    append_size(payload, manifest.selected_options);
    append_size(
        payload, manifest.selected_potential_pairs);
    append_size(
        payload, manifest.distinct_physical_games);
    append_size(
        payload, manifest.max_roots_per_actor_game);
    append_size(payload, manifest.collision_pairs);
    return artifact_integrity::sha256_string(payload);
}

void validate_manifest(
    const SelectionManifest& manifest) {
    if (manifest.parent_fingerprint !=
            density::kRequiredParentFingerprint ||
        !sha256_is_canonical(
            manifest.source_manifest_hash) ||
        manifest.selection_seed != kSelectionSeed ||
        manifest.train_quota_per_cell == 0 ||
        manifest.dev_quota_per_cell == 0 ||
        !sha256_is_canonical(manifest.manifest_hash)) {
        throw std::invalid_argument(
            "AQ17-DBC6 selected-manifest identity is invalid");
    }
    std::set<std::string> roots;
    std::set<std::string> keys;
    std::map<std::string, const SelectedRoot*>
        selected_by_id;
    std::map<std::string, std::size_t>
        selected_positions;
    std::array<CellCensus,
               2 * kDeckCount * kWidthStrata>
        derived_cells{};
    std::array<std::map<ActorGameKey, std::size_t>,
               2 * kDeckCount * kWidthStrata>
        derived_actor_games;
    std::map<ActorGameKey, std::size_t>
        all_actor_games;
    std::set<PhysicalGameKey> physical_games;
    std::optional<decltype(canonical_root_order(
        std::declval<density::RootCoordinate>()))>
        previous_order;
    for (std::size_t selected_index = 0;
         selected_index < manifest.selected_roots.size();
         ++selected_index) {
        const SelectedRoot& root =
            manifest.selected_roots[selected_index];
        density::validate_manifest_root(root.source_root);
        const auto order =
            canonical_root_order(
                root.source_root.coordinate);
        if (previous_order.has_value() &&
            order <= *previous_order) {
            throw std::invalid_argument(
                "AQ17-DBC6 selected roots are not in source order");
        }
        previous_order = order;
        if (root.width_stratum !=
                width_stratum(
                    root.source_root
                        .legal_action_count) ||
            root.selection_key !=
                selection_key(
                    manifest.source_manifest_hash,
                    root.source_root.stable_root_id) ||
            !roots.insert(
                 root.source_root.stable_root_id)
                 .second ||
            !keys.insert(root.selection_key).second) {
            throw std::invalid_argument(
                "AQ17-DBC6 selected-root identity drifted");
        }
        selected_by_id.emplace(
            root.source_root.stable_root_id, &root);
        selected_positions.emplace(
            root.source_root.stable_root_id,
            selected_index);
        const std::size_t position =
            cell_index(
                root.source_root.coordinate.split,
                root.source_root.coordinate.owner_deck(),
                root.width_stratum);
        CellCensus& derived =
            derived_cells[position];
        ++derived.selected_roots;
        checked_add(
            derived.selected_options,
            root.source_root.legal_action_count,
            "derived option count");
        checked_add(
            derived.selected_potential_pairs,
            root.source_root.potential_pairs,
            "derived pair count");
        ++derived.actor_seats[
            root.source_root.coordinate.actor];
        ++derived.play_draw[
            root.source_root.coordinate.actor ==
                    root.source_root.coordinate
                        .starting_player
                ? 0
                : 1];
        ++derived_actor_games[position][
            actor_game_key(
                root.source_root.coordinate)];
        ++all_actor_games[
            actor_game_key(
                root.source_root.coordinate)];
        physical_games.insert(
            physical_game_key(
                root.source_root.coordinate));
    }
    for (std::size_t position = 0;
         position < derived_cells.size();
         ++position) {
        CellCensus& derived =
            derived_cells[position];
        derived.selected_distinct_actor_games =
            derived_actor_games[position].size();
        for (const auto& [unused, count] :
             derived_actor_games[position]) {
            static_cast<void>(unused);
            derived.max_roots_per_actor_game =
                std::max(
                    derived.max_roots_per_actor_game,
                    count);
        }
        derived.option_pair_denominator =
            derived.selected_potential_pairs;
    }

    std::size_t train_roots = 0;
    std::size_t dev_roots = 0;
    std::size_t options = 0;
    std::size_t pairs = 0;
    std::size_t collision_pairs = 0;
    std::size_t collision_groups = 0;
    for (std::size_t split = 0; split < 2; ++split) {
        const density::Split split_value =
            split == 0
                ? density::Split::Train
                : density::Split::Dev;
        const std::size_t quota =
            split == 0
                ? manifest.train_quota_per_cell
                : manifest.dev_quota_per_cell;
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            for (std::size_t stratum = 0;
                 stratum < kWidthStrata; ++stratum) {
                const auto deck_value =
                    static_cast<DeckId>(deck);
                const auto stratum_value =
                    static_cast<WidthStratum>(stratum);
                const CellCensus& cell =
                    manifest.cells[cell_index(
                        split_value, deck_value,
                        stratum_value)];
                if (cell.split != split_value ||
                    cell.owner_deck != deck_value ||
                    cell.width_stratum !=
                        stratum_value ||
                    cell.quota != quota ||
                    cell.source_roots <
                        cell.source_two_round_capacity ||
                    cell.source_distinct_actor_games >
                        cell.source_roots ||
                    cell.source_two_round_capacity <
                        quota ||
                    cell.selected_roots != quota ||
                    cell.selected_distinct_actor_games <
                        (quota + 1) / 2 ||
                    cell.selected_distinct_actor_games >
                        cell.selected_roots ||
                    cell.max_roots_per_actor_game >
                        kActorGameCellCap ||
                    cell.actor_seats[0] +
                            cell.actor_seats[1] !=
                        cell.selected_roots ||
                    cell.play_draw[0] +
                            cell.play_draw[1] !=
                        cell.selected_roots ||
                    cell.option_pair_denominator !=
                        cell.selected_potential_pairs ||
                    cell.collision_pairs >
                        cell.option_pair_denominator ||
                    cell.selected_roots !=
                        derived_cells[
                            cell_index(
                                split_value,
                                deck_value,
                                stratum_value)]
                            .selected_roots ||
                    cell.selected_options !=
                        derived_cells[
                            cell_index(
                                split_value,
                                deck_value,
                                stratum_value)]
                            .selected_options ||
                    cell.selected_potential_pairs !=
                        derived_cells[
                            cell_index(
                                split_value,
                                deck_value,
                                stratum_value)]
                            .selected_potential_pairs ||
                    cell.selected_distinct_actor_games !=
                        derived_cells[
                            cell_index(
                                split_value,
                                deck_value,
                                stratum_value)]
                            .selected_distinct_actor_games ||
                    cell.actor_seats !=
                        derived_cells[
                            cell_index(
                                split_value,
                                deck_value,
                                stratum_value)]
                            .actor_seats ||
                    cell.play_draw !=
                        derived_cells[
                            cell_index(
                                split_value,
                                deck_value,
                                stratum_value)]
                            .play_draw ||
                    cell.max_roots_per_actor_game !=
                        derived_cells[
                            cell_index(
                                split_value,
                                deck_value,
                                stratum_value)]
                            .max_roots_per_actor_game ||
                    cell.option_pair_denominator !=
                        derived_cells[
                            cell_index(
                                split_value,
                                deck_value,
                                stratum_value)]
                            .option_pair_denominator) {
                    throw std::invalid_argument(
                        "AQ17-DBC6 cell census is invalid");
                }
                if (split == 0) {
                    checked_add(
                        train_roots,
                        cell.selected_roots,
                        "validated TRAIN count");
                } else {
                    checked_add(
                        dev_roots,
                        cell.selected_roots,
                        "validated DEV count");
                }
                checked_add(
                    options, cell.selected_options,
                    "validated option count");
                checked_add(
                    pairs,
                    cell.selected_potential_pairs,
                    "validated pair count");
                checked_add(
                    collision_pairs,
                    cell.collision_pairs,
                    "validated collision count");
                checked_add(
                    collision_groups,
                    cell.collision_groups,
                    "validated collision-group count");
            }
        }
    }

    std::set<
        std::tuple<std::string, std::string, std::string>>
        alias_pairs;
    std::map<std::string, std::set<std::string>>
        aliased_descriptors;
    std::optional<std::pair<std::size_t, std::size_t>>
        previous_alias_order;
    std::size_t alias_pair_count = 0;
    for (const AliasGroup& group :
         manifest.alias_groups) {
        const auto selected =
            selected_by_id.find(group.stable_root_id);
        if (selected == selected_by_id.end() ||
            group.action_descriptors.size() < 2 ||
            group.split !=
                selected->second->source_root
                    .coordinate.split ||
            group.owner_deck !=
                selected->second->source_root
                    .coordinate.owner_deck() ||
            group.width_stratum !=
                selected->second->width_stratum) {
            throw std::invalid_argument(
                "AQ17-DBC6 alias group identity is invalid");
        }
        std::set<std::string> descriptors;
        std::size_t previous_index = 0;
        std::size_t first_index = 0;
        bool first_descriptor = true;
        for (const std::string& descriptor :
             group.action_descriptors) {
            const auto position = std::find(
                selected->second->source_root
                    .action_descriptors.begin(),
                selected->second->source_root
                    .action_descriptors.end(),
                descriptor);
            if (position ==
                    selected->second->source_root
                        .action_descriptors.end() ||
                !descriptors.insert(descriptor).second) {
                throw std::invalid_argument(
                    "AQ17-DBC6 alias descriptor is invalid");
            }
            const std::size_t index =
                static_cast<std::size_t>(
                    position -
                    selected->second->source_root
                        .action_descriptors.begin());
            if (!first_descriptor &&
                index <= previous_index) {
                throw std::invalid_argument(
                    "AQ17-DBC6 alias descriptor order drifted");
            }
            if (first_descriptor) {
                first_index = index;
            }
            first_descriptor = false;
            previous_index = index;
            if (!aliased_descriptors[
                     group.stable_root_id]
                     .insert(descriptor)
                     .second) {
                throw std::invalid_argument(
                    "AQ17-DBC6 alias groups overlap");
            }
        }
        const std::pair alias_order{
            selected_positions.at(
                group.stable_root_id),
            first_index,
        };
        if (previous_alias_order.has_value() &&
            alias_order <= *previous_alias_order) {
            throw std::invalid_argument(
                "AQ17-DBC6 alias group order drifted");
        }
        previous_alias_order = alias_order;
        const std::size_t position =
            cell_index(
                group.split, group.owner_deck,
                group.width_stratum);
        ++derived_cells[position].collision_groups;
        for (std::size_t left = 0;
             left < group.action_descriptors.size();
             ++left) {
            for (std::size_t right = left + 1;
                 right <
                     group.action_descriptors.size();
                 ++right) {
                if (!alias_pairs.emplace(
                         group.stable_root_id,
                         group.action_descriptors[left],
                         group.action_descriptors[right])
                         .second) {
                    throw std::invalid_argument(
                        "AQ17-DBC6 duplicate alias pair");
                }
                checked_add(
                    alias_pair_count, 1,
                    "alias-pair count");
                checked_add(
                    derived_cells[position]
                        .collision_pairs,
                    1, "derived collision-pair count");
            }
        }
    }
    for (std::size_t position = 0;
         position < manifest.cells.size();
         ++position) {
        if (manifest.cells[position].collision_groups !=
                derived_cells[position]
                    .collision_groups ||
            manifest.cells[position].collision_pairs !=
                derived_cells[position].collision_pairs) {
            throw std::invalid_argument(
                "AQ17-DBC6 alias cell census drifted");
        }
    }
    std::size_t derived_max_actor_game = 0;
    for (const auto& [unused, count] : all_actor_games) {
        static_cast<void>(unused);
        derived_max_actor_game =
            std::max(derived_max_actor_game, count);
    }
    if (train_roots != manifest.train_roots ||
        dev_roots != manifest.dev_roots ||
        train_roots + dev_roots !=
            manifest.selected_roots.size() ||
        options != manifest.selected_options ||
        pairs != manifest.selected_potential_pairs ||
        collision_pairs != manifest.collision_pairs ||
        collision_groups !=
            manifest.alias_groups.size() ||
        alias_pair_count != manifest.collision_pairs ||
        physical_games.size() !=
            manifest.distinct_physical_games ||
        derived_max_actor_game !=
            manifest.max_roots_per_actor_game ||
        manifest.max_roots_per_actor_game >
            kActorGameTotalCap ||
        manifest.distinct_physical_games == 0 ||
        manifest.manifest_hash !=
            canonical_manifest_hash(manifest)) {
        throw std::invalid_argument(
            "AQ17-DBC6 selected-manifest cross-sum drifted");
    }
}

RunReport run(std::shared_ptr<const LearnedModel> parent) {
    require_parent(parent);
    std::vector<PopulationRoot> population;
    population.reserve(
        kRequiredTrainRoots + kRequiredDevRoots);
    const density::AuthenticatedRootVisitor visitor =
        [&](const density::AuthenticatedRootView& view) {
            population.push_back({
                .source_root = view.manifest,
                .actions = {
                    view.actions.begin(),
                    view.actions.end(),
                },
                .option_rows = {
                    view.option_rows.begin(),
                    view.option_rows.end(),
                },
                .hidden_repartition_witness =
                    view.hidden_repartition_witness,
            });
        };
    const density::Collection source =
        density::collect_census(parent, visitor);
    density::validate_census(source.census);
    if (source.census.manifest_hash !=
            kRequiredCensusManifest ||
        source.census.parent_fingerprint !=
            density::kRequiredParentFingerprint ||
        source.census.root_seed !=
            density::kCollectionRootSeed ||
        source.census.splits[0].roots !=
            kRequiredTrainRoots ||
        source.census.splits[1].roots !=
            kRequiredDevRoots ||
        population.size() != source.census.roots.size()) {
        throw std::runtime_error(
            "AQ17-DBC6 frozen AQ16 source drifted");
    }
    for (std::size_t index = 0;
         index < population.size(); ++index) {
        if (population[index].source_root !=
            source.census.roots[index]) {
            throw std::runtime_error(
                "AQ17-DBC6 visitor/source order drifted");
        }
    }

    const SelectionManifest first =
        select_population_impl(
            learned_model_fingerprint(parent),
            source.census.manifest_hash,
            population,
            kTrainRootsPerCell,
            kDevRootsPerCell);
    const SelectionManifest repeated =
        select_population_impl(
            learned_model_fingerprint(parent),
            source.census.manifest_hash,
            population,
            kTrainRootsPerCell,
            kDevRootsPerCell);
    if (first != repeated) {
        throw std::runtime_error(
            "AQ17-DBC6 repeated pure selector drifted");
    }

    std::set<std::string> selected_ids;
    for (const SelectedRoot& root :
         first.selected_roots) {
        selected_ids.insert(
            root.source_root.stable_root_id);
    }
    std::string hidden_witness_root_id;
    for (const PopulationRoot& root : population) {
        if (root.hidden_repartition_witness &&
            selected_ids.contains(
                root.source_root.stable_root_id)) {
            hidden_witness_root_id =
                root.source_root.stable_root_id;
            break;
        }
    }
    if (hidden_witness_root_id.empty()) {
        throw std::runtime_error(
            "AQ17-DBC6 selected hidden witness was vacuous");
    }
    return {
        .manifest = first,
        .repeated_selector_bit_identical = true,
        .hidden_repartition_witness = true,
        .hidden_witness_root_id =
            std::move(hidden_witness_root_id),
        .source_collections = 1,
    };
}

void print_report(
    std::ostream& output, const RunReport& report) {
    validate_manifest(report.manifest);
    if (!report.repeated_selector_bit_identical ||
        !report.hidden_repartition_witness ||
        report.source_collections != 1 ||
        !sha256_is_canonical(
            report.hidden_witness_root_id) ||
        std::none_of(
            report.manifest.selected_roots.begin(),
            report.manifest.selected_roots.end(),
            [&](const SelectedRoot& root) {
                return
                    root.source_root.stable_root_id ==
                    report.hidden_witness_root_id;
            }) ||
        report.manifest.source_manifest_hash !=
            kRequiredCensusManifest ||
        report.manifest.train_roots !=
            kExpectedTrainRoots ||
        report.manifest.dev_roots !=
            kExpectedDevRoots) {
        throw std::invalid_argument(
            "AQ17-DBC6 production report gates failed");
    }
    output
        << "result=PASS"
        << " disposition=CENSUS_ONLY"
        << " identifier=" << kIdentifier
        << " parent="
        << report.manifest.parent_fingerprint
        << " source_manifest="
        << report.manifest.source_manifest_hash
        << " selected_manifest="
        << report.manifest.manifest_hash
        << " selection_schema=" << kSelectionSchema
        << " selection_seed=" << kSelectionSeed
        << " source_collections="
        << report.source_collections
        << " repeated_selector_bit_identical=1"
        << " hidden_repartition_witness=1"
        << " hidden_witness_root="
        << report.hidden_witness_root_id
        << " train_roots="
        << report.manifest.train_roots
        << " dev_roots="
        << report.manifest.dev_roots
        << " options="
        << report.manifest.selected_options
        << " potential_pairs="
        << report.manifest.selected_potential_pairs
        << " collision_pairs="
        << report.manifest.collision_pairs
        << " physical_games="
        << report.manifest.distinct_physical_games
        << " max_roots_per_actor_game="
        << report.manifest.max_roots_per_actor_game
        << " teacher_labels=0"
        << " candidate_scores=0"
        << " model_created=0"
        << " selector_opened=0"
        << " artifact_published=0\n";
    for (const CellCensus& cell :
         report.manifest.cells) {
        output
            << "cell split=" << split_name(cell.split)
            << " deck=" << deck_name(cell.owner_deck)
            << " width="
            << stratum_name(cell.width_stratum)
            << " quota=" << cell.quota
            << " source_roots=" << cell.source_roots
            << " source_actor_games="
            << cell.source_distinct_actor_games
            << " source_two_round_capacity="
            << cell.source_two_round_capacity
            << " roots=" << cell.selected_roots
            << " options=" << cell.selected_options
            << " potential_pairs="
            << cell.selected_potential_pairs
            << " actor_games="
            << cell.selected_distinct_actor_games
            << " seat0=" << cell.actor_seats[0]
            << " seat1=" << cell.actor_seats[1]
            << " play=" << cell.play_draw[0]
            << " draw=" << cell.play_draw[1]
            << " max_per_actor_game="
            << cell.max_roots_per_actor_game
            << " collision_groups="
            << cell.collision_groups
            << " collision_pairs="
            << cell.collision_pairs
            << " option_pair_denominator="
            << cell.option_pair_denominator << '\n';
    }
    for (const AliasGroup& group :
         report.manifest.alias_groups) {
        output
            << "alias_group split="
            << split_name(group.split)
            << " deck=" << deck_name(group.owner_deck)
            << " width="
            << stratum_name(group.width_stratum)
            << " root=" << group.stable_root_id
            << " options="
            << group.action_descriptors.size()
            << '\n';
        for (std::size_t left = 0;
             left < group.action_descriptors.size();
             ++left) {
            for (std::size_t right = left + 1;
                 right <
                     group.action_descriptors.size();
                 ++right) {
                output
                    << "alias_pair root="
                    << group.stable_root_id
                    << " left="
                    << group.action_descriptors[left]
                    << " right="
                    << group.action_descriptors[right]
                    << '\n';
            }
        }
    }
}

namespace testing {

SelectionManifest select_population(
    std::string parent_fingerprint,
    std::string source_manifest_hash,
    std::span<const PopulationRoot> population,
    std::size_t train_quota_per_cell,
    std::size_t dev_quota_per_cell) {
    return select_population_impl(
        std::move(parent_fingerprint),
        std::move(source_manifest_hash),
        population, train_quota_per_cell,
        dev_quota_per_cell);
}

bool rows_bit_identical(
    std::span<const double> left,
    std::span<const double> right) {
    return rows_bit_identical_impl(left, right);
}

} // namespace testing

} // namespace old_school::decision_density_priority
