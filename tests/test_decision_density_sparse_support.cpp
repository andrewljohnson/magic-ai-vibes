#include "old_school/decision_density_sparse_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace aq20 =
    old_school::decision_density_sparse_support;
namespace aq19 =
    old_school::decision_density_bilinear;

class TestRunner {
  public:
    template <typename Function>
    void run(std::string_view name, Function function) {
        try {
            function();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cout << "[FAIL] " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " passed, "
                  << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::uint64_t read_u64(
    std::string_view bytes, std::size_t& offset) {
    if (bytes.size() - std::min(bytes.size(), offset) < 8) {
        throw std::runtime_error(
            "canonical table header is truncated");
    }
    std::uint64_t result = 0;
    for (std::size_t byte = 0; byte < 8; ++byte) {
        result |=
            static_cast<std::uint64_t>(
                static_cast<unsigned char>(
                    bytes[offset + byte]))
            << (byte * 8);
    }
    offset += 8;
    return result;
}

std::string_view read_string(
    std::string_view bytes, std::size_t& offset) {
    const std::uint64_t size = read_u64(bytes, offset);
    if (size >
        static_cast<std::uint64_t>(
            bytes.size() -
            std::min(bytes.size(), offset))) {
        throw std::runtime_error(
            "canonical table string is truncated");
    }
    const std::string_view result =
        bytes.substr(
            offset, static_cast<std::size_t>(size));
    offset += static_cast<std::size_t>(size);
    return result;
}

std::size_t action_count(
    aq19::priority::WidthStratum width) {
    switch (width) {
    case aq19::priority::WidthStratum::B2:
        return 2;
    case aq19::priority::WidthStratum::B3:
        return 3;
    case aq19::priority::WidthStratum::B4Plus:
        return 4;
    }
    throw std::runtime_error("invalid synthetic width");
}

std::size_t cell_index(
    old_school::DeckId deck,
    aq19::priority::WidthStratum width) {
    return static_cast<std::size_t>(deck) *
               aq19::priority::kWidthStrata +
           static_cast<std::size_t>(width);
}

aq19::Root make_root(
    std::size_t cell, std::size_t repetition) {
    const auto deck =
        static_cast<old_school::DeckId>(
            cell / aq19::priority::kWidthStrata);
    const auto width =
        static_cast<aq19::priority::WidthStratum>(
            cell % aq19::priority::kWidthStrata);
    aq19::Root root{
        .stable_root_id =
            "root-" + std::to_string(cell) + "-" +
            std::to_string(repetition),
        .physical_game_group =
            "group-" + std::to_string(repetition) +
            "-" + std::to_string(cell / 2),
        .deck = deck,
        .width = width,
    };
    root.state.fill(1.0);
    const std::size_t actions = action_count(width);
    root.options.reserve(actions);
    for (std::size_t action = 0;
         action < actions; ++action) {
        aq19::Option option{
            .canonical_ordinal = action,
            .base_aggregate_score = 0.25,
            .teacher_aggregate_score = 0.75,
        };
        option.action_features.fill(
            static_cast<double>(action));
        option.common_world_teacher_samples.fill(0.75);
        root.options.push_back(std::move(option));
    }
    return root;
}

aq19::Dataset make_dataset(
    std::size_t repetitions) {
    aq19::Dataset result;
    for (std::size_t cell = 0;
         cell < aq19::kCellCount; ++cell) {
        for (std::size_t repetition = 0;
             repetition < repetitions;
             ++repetition) {
            result.roots.push_back(
                make_root(cell, repetition));
            ++result.roots_by_cell[cell];
            ++result.roots_by_deck[
                cell /
                aq19::priority::kWidthStrata];
        }
    }
    return result;
}

aq19::FoldAssignment make_folds(
    const aq19::Dataset& dataset) {
    aq19::FoldAssignment result{
        .manifest = "synthetic-fold-manifest",
    };
    for (const aq19::Root& root : dataset.roots) {
        const std::size_t first =
            root.physical_game_group.find('-');
        const std::size_t second =
            root.physical_game_group.find(
                '-', first + 1);
        const std::size_t fold =
            static_cast<std::size_t>(
                std::stoul(
                    root.physical_game_group.substr(
                        first + 1,
                        second - first - 1)));
        const auto item = std::make_pair(
            root.physical_game_group, fold);
        if (std::find(
                result.group_folds.begin(),
                result.group_folds.end(),
                item) == result.group_folds.end()) {
            result.group_folds.push_back(item);
        }
    }
    std::sort(
        result.group_folds.begin(),
        result.group_folds.end());
    for (const auto& [group, fold] :
         result.group_folds) {
        (void)group;
        ++result.folds[fold].physical_groups;
    }
    for (const aq19::Root& root : dataset.roots) {
        const auto found = std::lower_bound(
            result.group_folds.begin(),
            result.group_folds.end(),
            root.physical_game_group,
            [](const auto& item,
               std::string_view group) {
                return item.first < group;
            });
        const std::size_t fold = found->second;
        ++result.folds[fold].roots;
        ++result.folds[fold].roots_by_cell[
            cell_index(root.deck, root.width)];
    }
    return result;
}

aq20::labels::Corpus make_label_corpus(
    const aq19::Dataset& dataset) {
    aq20::labels::Corpus result;
    for (const aq19::Root& root : dataset.roots) {
        aq20::labels::RootLabel label;
        label.identity.owner_deck = root.deck;
        label.identity.width_stratum = root.width;
        label.identity.stable_root_id =
            root.stable_root_id;
        label.identity.physical_game_group =
            root.physical_game_group;
        for (const aq19::Option& option :
             root.options) {
            label.actions.push_back(option.action);
            std::vector<double> row;
            row.reserve(aq19::kPolicyFeatureCount);
            row.insert(
                row.end(),
                root.state.begin(), root.state.end());
            row.insert(
                row.end(),
                option.action_features.begin(),
                option.action_features.end());
            label.option_rows.push_back(std::move(row));
        }
        result.train.push_back(std::move(label));
    }
    return result;
}

void mutate_all_teacher_fields(
    aq20::labels::Corpus& corpus) {
    for (std::size_t root_index = 0;
         root_index < corpus.train.size();
         ++root_index) {
        auto& root = corpus.train[root_index];
        const std::size_t actions = root.actions.size();
        root.teacher_q_samples.assign(
            actions,
            std::vector<double>(
                aq20::labels::kWorlds,
                std::numeric_limits<double>::
                    quiet_NaN()));
        root.teacher_terminal_flags.assign(
            actions,
            std::vector<std::uint8_t>(
                aq20::labels::kWorlds,
                static_cast<std::uint8_t>(
                    root_index % 251)));
        root.teacher_shallow_prior_samples.assign(
            actions,
            std::vector<double>(
                aq20::labels::kWorlds,
                std::numeric_limits<double>::
                    infinity()));
        root.teacher_continuation_samples.assign(
            actions,
            std::vector<double>(
                aq20::labels::kWorlds,
                -std::numeric_limits<double>::
                    infinity()));
        root.teacher_aggregate_scores.assign(
            actions,
            std::bit_cast<double>(
                UINT64_C(0x7ff8000000000001) +
                root_index));
        root.teacher_accounting.sampled_worlds =
            91 + root_index;
        root.teacher_accounting.rollout_evaluations =
            101 + root_index;
        root.teacher_accounting.terminal_evaluations =
            111 + root_index;
        root.teacher_accounting.bootstrapped_evaluations =
            121 + root_index;
        root.teacher_accounting.inner_rollout_evaluations =
            131 + root_index;
        root.teacher_accounting.inner_search_invocations =
            141 + root_index;
        root.teacher_accounting.inner_search_max_depth =
            151 + root_index;
        root.teacher_accounting
            .inner_rollout_evaluations_by_cell = {
                {root_index, root_index + 1}};
        root.teacher_accounting
            .inner_search_invocations_by_cell = {
                {root_index + 2}};
        root.teacher_accounting
            .inner_search_max_depth_by_cell = {
                {root_index + 3}};
        root.target_probabilities.assign(
            actions,
            -1000.0 - static_cast<double>(root_index));
        root.weight =
            std::numeric_limits<double>::quiet_NaN();
    }
}

void test_command_contract() {
    const std::array<std::string_view, 1> command{
        "--census"};
    expect(
        aq20::parse_command(command),
        "--census was not accepted");
    const std::array<std::string_view, 0> empty{};
    const std::array<std::string_view, 2> knob{
        "--census", "--roots=1"};
    expect(
        !aq20::parse_command(empty) &&
            !aq20::parse_command(knob),
        "an undeclared AQ20 command was accepted");
    std::ostringstream usage;
    aq20::print_usage(usage);
    expect(
        usage.str().find("--census") !=
            std::string::npos,
        "usage omitted the only mode");
}

void test_partition_support_and_hash() {
    const aq20::PartitionReport report =
        aq20::census_partition(
            "TRAIN", make_dataset(4));
    expect(
        report.roots == 60 &&
            report.physical_groups == 32,
        "partition root/group census drifted");
    expect(
        report.active_coordinates ==
                aq20::kCoordinateCount &&
            report.eligible_coordinates ==
                aq20::kCoordinateCount,
        "fully supported coordinates were not eligible");
    expect(
        report.minimum_eligible_root_support == 60 &&
            report.minimum_eligible_group_support == 32,
        "minimum eligible support drifted");
    expect(
        report.maximum_eligible_group_leverage > 0.0 &&
            report.maximum_eligible_group_leverage <=
                aq20::kMaximumGroupLeverage,
        "eligible leverage summary drifted");
    expect(
        report.canonical_table_sha256.size() == 64,
        "canonical table SHA shape drifted");

    const std::string bytes =
        aq20::canonical_partition_table(report);
    std::size_t offset = 0;
    expect(
        read_string(bytes, offset) == aq20::kSchema &&
            read_string(bytes, offset) == "TRAIN",
        "canonical table identity header drifted");
    expect(
        read_u64(bytes, offset) ==
                aq19::kStateFeatureCount &&
            read_u64(bytes, offset) ==
                aq19::kActionFeatureCount &&
            read_u64(bytes, offset) == report.roots &&
            read_u64(bytes, offset) ==
                report.physical_groups,
        "canonical table dimension header drifted");
    expect(
        offset ==
            2 * sizeof(std::uint64_t) +
                aq20::kSchema.size() +
                std::string_view("TRAIN").size() +
                4 * sizeof(std::uint64_t),
        "canonical table header encoded an extra field");
}

void test_teacher_fields_are_byte_inert() {
    const aq19::Dataset features = make_dataset(1);
    const aq20::labels::Corpus clean_source =
        make_label_corpus(features);
    aq20::labels::Corpus mutated_source = clean_source;
    mutate_all_teacher_fields(mutated_source);
    const aq19::Dataset clean =
        aq20::project_train_label_blind(clean_source);
    const aq19::Dataset mutated =
        aq20::project_train_label_blind(mutated_source);
    const aq20::PartitionReport clean_report =
        aq20::census_partition("TRAIN", clean);
    const aq20::PartitionReport mutated_report =
        aq20::census_partition("TRAIN", mutated);
    expect(
        aq20::canonical_partition_table(clean_report) ==
            aq20::canonical_partition_table(
                mutated_report) &&
            clean_report.canonical_table_sha256 ==
                mutated_report.canonical_table_sha256,
        "teacher mutation changed a census byte");
}

void test_option_permutation_is_equivariant() {
    const aq19::Dataset original = make_dataset(1);
    aq19::Dataset permuted = original;
    for (aq19::Root& root : permuted.roots) {
        std::reverse(
            root.options.begin(), root.options.end());
    }
    const aq20::PartitionReport left =
        aq20::census_partition("TRAIN", original);
    const aq20::PartitionReport right =
        aq20::census_partition("TRAIN", permuted);
    expect(
        aq20::canonical_partition_table(left) ==
            aq20::canonical_partition_table(right) &&
            left.canonical_table_sha256 ==
                right.canonical_table_sha256,
        "legal-option permutation changed the census");
}

void test_fold_isolation_and_repeat_identity() {
    const aq19::Dataset dataset = make_dataset(4);
    const aq19::FoldAssignment folds =
        make_folds(dataset);
    const aq20::CensusReport first =
        aq20::census(dataset, folds);
    const aq20::CensusReport repeated =
        aq20::census(dataset, folds);
    expect(
        aq20::canonical_report_bytes(first) ==
            aq20::canonical_report_bytes(repeated),
        "repeated census bytes drifted");
    expect(
        first.every_partition_has_16_eligible &&
            first.partitions[0].roots == 60,
        "five-partition eligibility gate drifted");
    for (std::size_t fold = 0;
         fold < aq19::kFoldCount; ++fold) {
        expect(
            first.partitions[fold + 1].roots == 45 &&
                first.partitions[fold + 1]
                        .physical_groups ==
                    24,
            "fold-training complement census drifted");
    }

    aq19::Dataset changed = dataset;
    for (aq19::Root& root : changed.roots) {
        const auto found = std::lower_bound(
            folds.group_folds.begin(),
            folds.group_folds.end(),
            root.physical_game_group,
            [](const auto& item,
               std::string_view group) {
                return item.first < group;
            });
        if (found->second == 0) {
            root.state[0] = 2.0;
        }
    }
    const aq20::CensusReport perturbed =
        aq20::census(changed, folds);
    expect(
        first.partitions[1].canonical_table_sha256 ==
            perturbed.partitions[1]
                .canonical_table_sha256,
        "held-out group leaked into its complement");
    expect(
        first.partitions[0].canonical_table_sha256 !=
                perturbed.partitions[0]
                    .canonical_table_sha256 &&
            first.partitions[2].canonical_table_sha256 !=
                perturbed.partitions[2]
                    .canonical_table_sha256,
        "in-scope group perturbation was not observed");
}

void test_sealed_zero_accounting() {
    const aq19::Dataset dataset = make_dataset(4);
    const aq20::CensusReport report =
        aq20::census(dataset, make_folds(dataset));
    expect(
        report.teacher_fields_read == 0 &&
            report.candidate_scores == 0 &&
            report.selected_terms == 0 &&
            report.optimizer_steps == 0 &&
            report.model_created == 0 &&
            !report.tactical_seed_opened &&
            !report.selector_seed_opened &&
            report.gameplay_games == 0,
        "label-blind census opened a sealed resource");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "sealed command contract",
        test_command_contract);
    runner.run(
        "partition support and canonical hash",
        test_partition_support_and_hash);
    runner.run(
        "teacher fields are byte inert",
        test_teacher_fields_are_byte_inert);
    runner.run(
        "option permutation equivariance",
        test_option_permutation_is_equivariant);
    runner.run(
        "whole-group folds and repeat identity",
        test_fold_isolation_and_repeat_identity);
    runner.run(
        "sealed zero accounting",
        test_sealed_zero_accounting);
    return runner.finish();
}
