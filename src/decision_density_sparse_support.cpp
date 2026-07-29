#include "old_school/decision_density_sparse_support.hpp"

#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::decision_density_sparse_support {
namespace {

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        throw std::invalid_argument(
            "AQ20 support deck is invalid");
    }
    return result;
}

std::size_t width_index(
    aq19::priority::WidthStratum width) {
    const std::size_t result =
        static_cast<std::size_t>(width);
    if (result >= aq19::priority::kWidthStrata) {
        throw std::invalid_argument(
            "AQ20 support width is invalid");
    }
    return result;
}

std::size_t cell_index(
    DeckId deck,
    aq19::priority::WidthStratum width) {
    return deck_index(deck) *
               aq19::priority::kWidthStrata +
           width_index(width);
}

bool bit_equal(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

std::vector<std::size_t> canonical_option_order(
    const aq19::Root& root) {
    std::vector<std::size_t> result(
        root.options.size(),
        std::numeric_limits<std::size_t>::max());
    for (std::size_t row = 0;
         row < root.options.size(); ++row) {
        const std::size_t ordinal =
            root.options[row].canonical_ordinal;
        if (ordinal >= result.size() ||
            result[ordinal] !=
                std::numeric_limits<std::size_t>::max()) {
            throw std::invalid_argument(
                "AQ20 canonical option order is invalid");
        }
        result[ordinal] = row;
    }
    return result;
}

template <typename Range>
bool finite_range(const Range& values) {
    return std::all_of(
        values.begin(), values.end(),
        [](double value) {
            return std::isfinite(value);
        });
}

void validate_feature_root(const aq19::Root& root) {
    if (root.stable_root_id.empty() ||
        root.physical_game_group.empty() ||
        root.options.size() < 2 ||
        aq19::priority::width_stratum(
            root.options.size()) != root.width ||
        !finite_range(root.state)) {
        throw std::invalid_argument(
            "AQ20 root identity or feature shape is invalid");
    }
    (void)deck_index(root.deck);
    (void)width_index(root.width);
    (void)canonical_option_order(root);
    for (const aq19::Option& option : root.options) {
        if (!finite_range(option.action_features)) {
            throw std::invalid_argument(
                "AQ20 action features are nonfinite");
        }
    }
}

void validate_feature_dataset(
    const aq19::Dataset& dataset) {
    if (dataset.roots.empty()) {
        throw std::invalid_argument(
            "AQ20 feature dataset is empty");
    }
    std::array<std::size_t, aq19::kCellCount> cells{};
    std::array<std::size_t, kDeckCount> decks{};
    std::set<std::string> stable_ids;
    for (const aq19::Root& root : dataset.roots) {
        validate_feature_root(root);
        ++cells[cell_index(root.deck, root.width)];
        ++decks[deck_index(root.deck)];
        if (!stable_ids.insert(root.stable_root_id).second) {
            throw std::invalid_argument(
                "AQ20 stable root id is duplicated");
        }
    }
    if (cells != dataset.roots_by_cell ||
        decks != dataset.roots_by_deck ||
        std::any_of(
            cells.begin(), cells.end(),
            [](std::size_t count) {
                return count == 0;
            })) {
        throw std::invalid_argument(
            "AQ20 feature dataset census drifted");
    }
}

void append_u64(
    std::string& output, std::uint64_t value) {
    for (std::size_t byte = 0; byte < 8; ++byte) {
        output.push_back(
            static_cast<char>(
                (value >> (byte * 8)) & UINT64_C(0xff)));
    }
}

void append_bool(std::string& output, bool value) {
    output.push_back(value ? '\1' : '\0');
}

void append_string(
    std::string& output, std::string_view value) {
    append_u64(
        output, static_cast<std::uint64_t>(value.size()));
    output.append(value);
}

void append_double(
    std::string& output, double value) {
    append_u64(
        output, std::bit_cast<std::uint64_t>(value));
}

std::size_t fold_for_group(
    const aq19::FoldAssignment& folds,
    std::string_view group) {
    const auto found = std::lower_bound(
        folds.group_folds.begin(),
        folds.group_folds.end(), group,
        [](const auto& item, std::string_view key) {
            return item.first < key;
        });
    if (found == folds.group_folds.end() ||
        found->first != group ||
        found->second >= aq19::kFoldCount) {
        throw std::invalid_argument(
            "AQ20 physical group has no valid fold");
    }
    return found->second;
}

using RootPointers = std::vector<const aq19::Root*>;

PartitionReport census_roots(
    std::string name, const RootPointers& roots) {
    if (roots.empty()) {
        throw std::invalid_argument(
            "AQ20 census partition is empty");
    }

    std::array<std::size_t, aq19::kCellCount> cells{};
    std::map<std::string, std::size_t> group_indices;
    std::set<std::string> stable_ids;
    for (const aq19::Root* root : roots) {
        if (root == nullptr) {
            throw std::invalid_argument(
                "AQ20 census root pointer is null");
        }
        validate_feature_root(*root);
        ++cells[cell_index(root->deck, root->width)];
        group_indices.try_emplace(
            root->physical_game_group,
            group_indices.size());
        if (!stable_ids.insert(root->stable_root_id).second) {
            throw std::invalid_argument(
                "AQ20 partition repeats a root");
        }
    }
    if (std::any_of(
            cells.begin(), cells.end(),
            [](std::size_t count) {
                return count == 0;
            })) {
        throw std::invalid_argument(
            "AQ20 partition omits a feature cell");
    }

    // std::map insertion order does not define the stored ordinal. Reassign
    // canonical group ordinals in lexical order before any reduction.
    std::size_t group_ordinal = 0;
    for (auto& [group, ordinal] : group_indices) {
        (void)group;
        ordinal = group_ordinal++;
    }

    const std::size_t groups = group_indices.size();
    if (groups >
        std::numeric_limits<std::size_t>::max() /
            kCoordinateCount) {
        throw std::overflow_error(
            "AQ20 group-energy matrix is too large");
    }

    std::vector<double> total_energy(
        kCoordinateCount, 0.0);
    std::vector<double> group_energy(
        groups * kCoordinateCount, 0.0);
    std::vector<std::size_t> root_support(
        kCoordinateCount, 0);
    std::vector<std::uint8_t> finite(
        kCoordinateCount, UINT8_C(1));

    // The reduction order is sealed: dataset root order, canonical action
    // ordinal, state feature p, action feature q.
    for (const aq19::Root* root : roots) {
        const std::vector<std::size_t> order =
            canonical_option_order(*root);
        std::vector<std::size_t> active_state_features;
        active_state_features.reserve(
            aq19::kStateFeatureCount);
        for (std::size_t p = 0;
             p < aq19::kStateFeatureCount; ++p) {
            if (root->state[p] != 0.0) {
                active_state_features.push_back(p);
            }
        }
        aq19::ActionFeatures action_mean{};
        for (const std::size_t row : order) {
            for (std::size_t q = 0;
                 q < aq19::kActionFeatureCount; ++q) {
                action_mean[q] +=
                    root->options[row].action_features[q];
            }
        }
        const double action_count =
            static_cast<double>(order.size());
        for (double& value : action_mean) {
            value /= action_count;
        }

        std::vector<std::uint8_t> root_has_support(
            kCoordinateCount, UINT8_C(0));
        const std::size_t group =
            group_indices.at(root->physical_game_group);
        const double root_weight =
            1.0 /
            (static_cast<double>(aq19::kCellCount) *
             static_cast<double>(
                 cells[cell_index(
                     root->deck, root->width)]));
        const double contribution_scale =
            root_weight / action_count;

        for (const std::size_t row : order) {
            const aq19::Option& option =
                root->options[row];
            std::vector<std::size_t>
                active_action_differences;
            active_action_differences.reserve(
                aq19::kActionFeatureCount);
            for (std::size_t q = 0;
                 q < aq19::kActionFeatureCount; ++q) {
                if (option.action_features[q] !=
                    action_mean[q]) {
                    active_action_differences.push_back(q);
                }
            }
            for (const std::size_t p :
                 active_state_features) {
                const double state = root->state[p];
                const std::size_t offset =
                    p * aq19::kActionFeatureCount;
                for (const std::size_t q :
                     active_action_differences) {
                    const double difference =
                        option.action_features[q] -
                        action_mean[q];
                    const std::size_t coordinate =
                        offset + q;
                    const double x = state * difference;
                    const double contribution =
                        x * x * contribution_scale;
                    if (!std::isfinite(contribution)) {
                        finite[coordinate] = UINT8_C(0);
                        continue;
                    }
                    if (contribution > 0.0) {
                        if (root_has_support[
                                coordinate] == 0) {
                            ++root_support[coordinate];
                        }
                        root_has_support[coordinate] =
                            UINT8_C(1);
                    }
                    total_energy[coordinate] +=
                        contribution;
                    group_energy[
                        group * kCoordinateCount +
                        coordinate] += contribution;
                    if (!std::isfinite(
                            total_energy[coordinate]) ||
                        !std::isfinite(
                            group_energy[
                                group *
                                    kCoordinateCount +
                                coordinate])) {
                        finite[coordinate] = UINT8_C(0);
                    }
                }
            }
        }
    }

    PartitionReport result{
        .name = std::move(name),
        .roots = roots.size(),
        .physical_groups = groups,
    };
    result.coordinates.resize(kCoordinateCount);
    result.minimum_eligible_root_support =
        std::numeric_limits<std::size_t>::max();
    result.minimum_eligible_group_support =
        std::numeric_limits<std::size_t>::max();

    for (std::size_t coordinate = 0;
         coordinate < kCoordinateCount;
         ++coordinate) {
        CoordinateRow& row =
            result.coordinates[coordinate];
        row.root_support = root_support[coordinate];
        row.weighted_energy = total_energy[coordinate];
        row.finite = finite[coordinate] != 0;
        for (std::size_t group = 0;
             group < groups; ++group) {
            const double energy =
                group_energy[
                    group * kCoordinateCount +
                    coordinate];
            if (!std::isfinite(energy)) {
                row.finite = false;
            }
            if (energy > 0.0) {
                ++row.group_support;
                row.maximum_group_weighted_energy =
                    std::max(
                        row.maximum_group_weighted_energy,
                        energy);
            }
        }
        if (!row.finite) {
            row.weighted_energy =
                std::numeric_limits<double>::infinity();
            row.maximum_group_weighted_energy =
                std::numeric_limits<double>::infinity();
            row.maximum_group_leverage =
                std::numeric_limits<double>::infinity();
        } else if (row.weighted_energy > 0.0) {
            row.maximum_group_leverage =
                row.maximum_group_weighted_energy /
                row.weighted_energy;
        }
        row.active =
            row.finite && row.weighted_energy > 0.0;
        row.eligible =
            row.active &&
            row.root_support >= kMinimumRootSupport &&
            row.group_support >= kMinimumGroupSupport &&
            row.maximum_group_leverage <=
                kMaximumGroupLeverage;
        result.active_coordinates +=
            row.active ? 1U : 0U;
        result.eligible_coordinates +=
            row.eligible ? 1U : 0U;
        if (row.eligible) {
            result.minimum_eligible_root_support =
                std::min(
                    result.minimum_eligible_root_support,
                    row.root_support);
            result.minimum_eligible_group_support =
                std::min(
                    result.minimum_eligible_group_support,
                    row.group_support);
            result.maximum_eligible_group_leverage =
                std::max(
                    result.maximum_eligible_group_leverage,
                    row.maximum_group_leverage);
        }
    }
    if (result.eligible_coordinates == 0) {
        result.minimum_eligible_root_support = 0;
        result.minimum_eligible_group_support = 0;
    }
    result.canonical_table_sha256 =
        artifact_integrity::sha256_string(
            canonical_partition_table(result));
    return result;
}

std::string cross_partition_payload(
    const CensusReport& report) {
    std::string bytes;
    append_string(bytes, kSchema);
    append_string(bytes, report.fold_manifest);
    append_u64(bytes, report.partitions.size());
    for (const PartitionReport& partition :
         report.partitions) {
        append_string(bytes, partition.name);
        append_string(
            bytes, partition.canonical_table_sha256);
    }
    return bytes;
}

} // namespace

bool parse_command(
    std::span<const std::string_view> arguments) {
    return arguments.size() == 1 &&
           arguments.front() == "--census";
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-decision-density-sparse-support "
           "--census\n";
}

aq19::Dataset project_train_label_blind(
    const labels::Corpus& source) {
    aq19::Dataset result;
    result.roots.reserve(source.train.size());
    for (const labels::RootLabel& input : source.train) {
        if (input.actions.size() < 2 ||
            input.option_rows.size() !=
                input.actions.size()) {
            throw std::invalid_argument(
                "AQ20 source action shape drifted");
        }
        aq19::Root root{
            .stable_root_id =
                input.identity.stable_root_id,
            .physical_game_group =
                input.identity.physical_game_group,
            .deck = input.identity.owner_deck,
            .width = input.identity.width_stratum,
        };
        if (input.option_rows.front().size() !=
            aq19::kPolicyFeatureCount) {
            throw std::invalid_argument(
                "AQ20 source feature width drifted");
        }
        std::copy_n(
            input.option_rows.front().begin(),
            aq19::kStateFeatureCount,
            root.state.begin());
        root.options.reserve(input.actions.size());
        for (std::size_t action = 0;
             action < input.actions.size(); ++action) {
            const std::vector<double>& row =
                input.option_rows[action];
            if (row.size() != aq19::kPolicyFeatureCount) {
                throw std::invalid_argument(
                    "AQ20 source feature width drifted");
            }
            for (std::size_t p = 0;
                 p < aq19::kStateFeatureCount; ++p) {
                if (!bit_equal(
                        row[p], root.state[p])) {
                    throw std::invalid_argument(
                        "AQ20 actor-state prefix drifted");
                }
            }
            aq19::Option option{
                .canonical_ordinal = action,
                .action = input.actions[action],
            };
            std::copy(
                row.begin() +
                    static_cast<std::ptrdiff_t>(
                        aq19::kStateFeatureCount),
                row.end(),
                option.action_features.begin());
            root.options.push_back(std::move(option));
        }
        validate_feature_root(root);
        ++result.roots_by_cell[
            cell_index(root.deck, root.width)];
        ++result.roots_by_deck[
            deck_index(root.deck)];
        result.roots.push_back(std::move(root));
    }
    validate_feature_dataset(result);
    return result;
}

PartitionReport census_partition(
    std::string name,
    const aq19::Dataset& dataset) {
    validate_feature_dataset(dataset);
    RootPointers roots;
    roots.reserve(dataset.roots.size());
    for (const aq19::Root& root : dataset.roots) {
        roots.push_back(&root);
    }
    return census_roots(std::move(name), roots);
}

CensusReport census(
    const aq19::Dataset& train,
    const aq19::FoldAssignment& folds) {
    validate_feature_dataset(train);
    if (folds.manifest.empty() ||
        !std::is_sorted(
            folds.group_folds.begin(),
            folds.group_folds.end()) ||
        std::adjacent_find(
            folds.group_folds.begin(),
            folds.group_folds.end(),
            [](const auto& left, const auto& right) {
                return left.first == right.first;
            }) != folds.group_folds.end()) {
        throw std::invalid_argument(
            "AQ20 fold assignment is invalid");
    }
    std::set<std::string> dataset_groups;
    for (const aq19::Root& root : train.roots) {
        dataset_groups.insert(root.physical_game_group);
        (void)fold_for_group(
            folds, root.physical_game_group);
    }
    if (dataset_groups.size() !=
            folds.group_folds.size() ||
        !std::equal(
            dataset_groups.begin(),
            dataset_groups.end(),
            folds.group_folds.begin(),
            [](const std::string& group,
               const auto& assignment) {
                return group == assignment.first;
            })) {
        throw std::invalid_argument(
            "AQ20 fold groups do not match dataset");
    }

    CensusReport result{.fold_manifest = folds.manifest};
    RootPointers all;
    all.reserve(train.roots.size());
    for (const aq19::Root& root : train.roots) {
        all.push_back(&root);
    }
    result.partitions[0] =
        census_roots("TRAIN", all);
    for (std::size_t held_out = 0;
         held_out < aq19::kFoldCount; ++held_out) {
        RootPointers complement;
        complement.reserve(train.roots.size());
        for (const aq19::Root& root : train.roots) {
            if (fold_for_group(
                    folds,
                    root.physical_game_group) !=
                held_out) {
                complement.push_back(&root);
            }
        }
        result.partitions[held_out + 1] =
            census_roots(
                "TRAIN_EXCEPT_FOLD_" +
                    std::to_string(held_out),
                complement);
    }
    result.every_partition_has_16_eligible =
        std::all_of(
            result.partitions.begin(),
            result.partitions.end(),
            [](const PartitionReport& partition) {
                return partition.eligible_coordinates >=
                       kRequiredEligibleCoordinates;
            });
    result.cross_partition_digest =
        artifact_integrity::sha256_string(
            cross_partition_payload(result));
    return result;
}

std::string canonical_partition_table(
    const PartitionReport& report) {
    if (report.coordinates.size() !=
        kCoordinateCount) {
        throw std::invalid_argument(
            "AQ20 coordinate table width drifted");
    }
    std::string bytes;
    bytes.reserve(
        128 +
        report.coordinates.size() *
            (5 * sizeof(std::uint64_t) + 3));
    append_string(bytes, kSchema);
    append_string(bytes, report.name);
    append_u64(bytes, aq19::kStateFeatureCount);
    append_u64(bytes, aq19::kActionFeatureCount);
    append_u64(bytes, report.roots);
    append_u64(bytes, report.physical_groups);
    for (const CoordinateRow& row :
         report.coordinates) {
        append_u64(bytes, row.root_support);
        append_u64(bytes, row.group_support);
        append_double(bytes, row.weighted_energy);
        append_double(
            bytes, row.maximum_group_weighted_energy);
        append_double(
            bytes, row.maximum_group_leverage);
        append_bool(bytes, row.finite);
        append_bool(bytes, row.active);
        append_bool(bytes, row.eligible);
    }
    return bytes;
}

std::string canonical_report_bytes(
    const CensusReport& report) {
    std::string bytes =
        cross_partition_payload(report);
    append_string(
        bytes, report.cross_partition_digest);
    append_bool(
        bytes, report.every_partition_has_16_eligible);
    for (const PartitionReport& partition :
         report.partitions) {
        append_u64(bytes, partition.roots);
        append_u64(bytes, partition.physical_groups);
        append_u64(
            bytes, partition.active_coordinates);
        append_u64(
            bytes, partition.eligible_coordinates);
        append_u64(
            bytes,
            partition.minimum_eligible_root_support);
        append_u64(
            bytes,
            partition.minimum_eligible_group_support);
        append_double(
            bytes,
            partition.maximum_eligible_group_leverage);
    }
    append_u64(bytes, report.teacher_fields_read);
    append_u64(bytes, report.candidate_scores);
    append_u64(bytes, report.selected_terms);
    append_u64(bytes, report.optimizer_steps);
    append_u64(bytes, report.model_created);
    append_bool(bytes, report.tactical_seed_opened);
    append_bool(bytes, report.selector_seed_opened);
    append_u64(bytes, report.gameplay_games);
    return bytes;
}

RunReport run() {
    RunReport report{
        .cache_path =
            std::filesystem::path(
                labels::kProductionCachePath),
    };
    const auto snapshot =
        artifact_integrity::snapshot_regular_file(
            report.cache_path);
    report.cache_bytes = snapshot.byte_size;
    report.cache_sha256 = snapshot.sha256;
    report.cache_identity_exact =
        report.cache_bytes == aq19::kRequiredCacheBytes &&
        report.cache_sha256 ==
            aq19::kRequiredCacheSha256;
    if (!report.cache_identity_exact) {
        throw std::runtime_error(
            "AQ20 cache byte identity drifted");
    }
    const labels::Corpus source =
        labels::load_cache(report.cache_path);
    report.corpus_digest = source.digest;
    report.corpus_identity_exact =
        source.digest == aq19::kRequiredCorpusDigest &&
        source.train.size() ==
            labels::kExpectedTrainRoots &&
        source.dev.size() ==
            labels::kExpectedDevRoots;
    if (!report.corpus_identity_exact) {
        throw std::runtime_error(
            "AQ20 corpus identity drifted");
    }
    const aq19::Dataset train =
        project_train_label_blind(source);
    const aq19::FoldAssignment folds =
        aq19::assign_grouped_folds(
            train, source.digest);
    report.fold_manifest_exact =
        folds.manifest == aq19::kRequiredFoldManifest;
    if (!report.fold_manifest_exact) {
        throw std::runtime_error(
            "AQ20 fold manifest drifted");
    }
    report.census = census(train, folds);
    return report;
}

void print_report(
    std::ostream& output, const RunReport& report) {
    output << std::setprecision(17);
    output
        << "identifier=" << kIdentifier << '\n'
        << "schema=" << kSchema << '\n'
        << "cache_path="
        << report.cache_path.string() << '\n'
        << "cache_bytes=" << report.cache_bytes << '\n'
        << "cache_sha256="
        << report.cache_sha256 << '\n'
        << "corpus_digest="
        << report.corpus_digest << '\n'
        << "fold_manifest="
        << report.census.fold_manifest << '\n';
    for (const PartitionReport& partition :
         report.census.partitions) {
        output
            << "partition=" << partition.name
            << " roots=" << partition.roots
            << " physical_groups="
            << partition.physical_groups
            << " active_coordinates="
            << partition.active_coordinates
            << " eligible_coordinates="
            << partition.eligible_coordinates
            << " minimum_eligible_root_support="
            << partition.minimum_eligible_root_support
            << " minimum_eligible_group_support="
            << partition.minimum_eligible_group_support
            << " maximum_eligible_group_leverage="
            << partition.maximum_eligible_group_leverage
            << " canonical_table_sha256="
            << partition.canonical_table_sha256
            << '\n';
    }
    output
        << "cross_partition_digest="
        << report.census.cross_partition_digest << '\n'
        << "every_partition_has_16_eligible="
        << (report.census.every_partition_has_16_eligible
                ? "yes"
                : "no")
        << '\n'
        << "teacher_fields_read="
        << report.census.teacher_fields_read << '\n'
        << "candidate_scores="
        << report.census.candidate_scores << '\n'
        << "selected_terms="
        << report.census.selected_terms << '\n'
        << "optimizer_steps="
        << report.census.optimizer_steps << '\n'
        << "model_created="
        << report.census.model_created << '\n'
        << "tactical_seed_opened="
        << (report.census.tactical_seed_opened
                ? "yes"
                : "no")
        << '\n'
        << "selector_seed_opened="
        << (report.census.selector_seed_opened
                ? "yes"
                : "no")
        << '\n'
        << "gameplay_games="
        << report.census.gameplay_games << '\n'
        << "cache_identity_exact="
        << (report.cache_identity_exact ? "pass" : "fail")
        << '\n'
        << "corpus_identity_exact="
        << (report.corpus_identity_exact ? "pass" : "fail")
        << '\n'
        << "fold_manifest_exact="
        << (report.fold_manifest_exact ? "pass" : "fail")
        << '\n';
}

} // namespace old_school::decision_density_sparse_support
