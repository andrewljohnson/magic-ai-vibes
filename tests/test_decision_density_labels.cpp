#include "old_school/decision_density_labels.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace labels = old_school::decision_density_labels;
namespace density = old_school::decision_density_census;
namespace priority = old_school::decision_density_priority;
namespace iteration = old_school::learned_iteration;

template <typename T>
concept HasGameStateMember =
    requires(T value) { value.state; };

template <typename T>
concept HasGameSeedMember =
    requires(T value) { value.game_seed; };

template <typename T>
concept HasOpponentHandMember =
    requires(T value) { value.opponent_hand; };

template <typename T>
concept HasOpponentLibraryMember =
    requires(T value) { value.opponent_library; };

template <typename T>
concept HasModelMember =
    requires(T value) { value.model; };

static_assert(!HasGameStateMember<labels::ProjectedRoot>);
static_assert(!HasGameSeedMember<labels::ProjectedRoot>);
static_assert(!HasOpponentHandMember<labels::ProjectedRoot>);
static_assert(!HasOpponentLibraryMember<labels::ProjectedRoot>);
static_assert(!HasGameStateMember<labels::RootLabel>);
static_assert(!HasModelMember<labels::Corpus>);
static_assert(std::is_same_v<
              decltype(labels::RootLabel::option_rows),
              std::vector<std::vector<double>>>);

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function&& function, std::string_view message) {
    bool rejected = false;
    try {
        std::forward<Function>(function)();
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected, message);
}

density::RootCoordinate coordinate(
    density::Split split, old_school::DeckId deck,
    std::size_t ordinal) {
    const std::size_t block =
        split == density::Split::Train ? 0 : 2;
    return {
        .split = split,
        .block_index = block,
        .schedule_index = ordinal % 40,
        .pairing_index = ordinal % 10,
        .game_seed = 1000 + ordinal,
        .starting_player = ordinal % 2,
        .seat_decks = {
            deck,
            deck == old_school::DeckId::Green
                ? old_school::DeckId::Red
                : old_school::DeckId::Green,
        },
        .actor = 0,
        .trace_ordinal = ordinal,
        .nontrivial_ordinal = 0,
        .actor_game_nontrivial_roots = 1,
    };
}

struct RootFixture {
    priority::SelectedRoot selected;
    std::vector<old_school::PriorityAction> actions;
    std::vector<std::vector<double>> rows;
};

RootFixture root_fixture(
    density::Split split, old_school::DeckId deck,
    std::size_t ordinal, bool aliased_rows = false,
    std::size_t action_count = 2) {
    std::vector<old_school::PriorityAction> actions{
        old_school::PriorityAction::pass(),
        old_school::PriorityAction::cast_creature(
            old_school::CardId::GrizzlyBears),
        old_school::PriorityAction::cast_creature(
            old_school::CardId::HillGiant),
        old_school::PriorityAction::cast_creature(
            old_school::CardId::IronclawOrcs),
    };
    actions.resize(action_count);
    std::vector<std::vector<double>> rows(
        actions.size(),
        std::vector<double>(
            density::kPolicyFeatureCount, 0.0));
    rows[0][0] = 0.25;
    rows[1][0] = 0.25;
    if (!aliased_rows) {
        rows[1].back() = 1.0;
    }
    density::ManifestRoot source =
        density::testing::make_manifest_root(
            coordinate(split, deck, ordinal),
            actions, rows);
    priority::SelectedRoot selected{
        .source_root = std::move(source),
        .width_stratum =
            priority::width_stratum(actions.size()),
    };
    selected.selection_key =
        priority::selection_key(
            labels::kRequiredSourceManifest,
            selected.source_root.stable_root_id);
    return {
        .selected = std::move(selected),
        .actions = std::move(actions),
        .rows = std::move(rows),
    };
}

old_school::LearnedActionSamples samples(
    std::span<const double> values, bool teacher) {
    old_school::LearnedActionSamples result;
    result.sampled_worlds = labels::kWorlds;
    result.rollout_evaluations =
        values.size() * labels::kWorlds;
    result.bootstrapped_evaluations =
        result.rollout_evaluations;
    result.q_samples.resize(values.size());
    result.terminal_evaluation_flags.resize(
        values.size());
    result.priority_shallow_prior_samples.resize(
        values.size());
    result.priority_continuation_samples.resize(
        values.size());
    result.exact_priority_aggregate_scores.resize(
        values.size());
    for (std::size_t action = 0;
         action < values.size(); ++action) {
        result.terminal_evaluation_flags[action].assign(
            labels::kWorlds, 0);
        result.priority_shallow_prior_samples[action].assign(
            labels::kWorlds, values[action]);
        result.priority_continuation_samples[action].assign(
            labels::kWorlds, values[action]);
        const double q =
            teacher
                ? values[action]
                : (values[action] +
                   static_cast<double>(labels::kWorlds) *
                       values[action]) /
                      static_cast<double>(
                          labels::kWorlds + 1);
        result.q_samples[action].assign(
            labels::kWorlds, q);
        double aggregate = 0.0;
        if (!teacher) {
            for (const double shallow :
                 result.priority_shallow_prior_samples[action]) {
                aggregate += shallow;
            }
            aggregate /=
                static_cast<double>(labels::kWorlds);
        }
        for (const double continuation :
             result.priority_continuation_samples[action]) {
            aggregate += continuation;
        }
        aggregate /=
            static_cast<double>(
                labels::kWorlds + (teacher ? 0 : 1));
        result.exact_priority_aggregate_scores[action] =
            aggregate;
    }
    if (teacher) {
        result.priority_inner_rollout_evaluations.assign(
            values.size(),
            std::vector<std::size_t>(
                labels::kWorlds, 0));
        result.priority_inner_search_invocations.assign(
            values.size(),
            std::vector<std::size_t>(
                labels::kWorlds, 0));
        result.priority_inner_search_max_depth.assign(
            values.size(),
            std::vector<std::size_t>(
                labels::kWorlds, 0));
    }
    return result;
}

labels::RootLabel root_label(
    density::Split split, old_school::DeckId deck,
    std::size_t ordinal, bool aliased_rows = false,
    std::array<double, 2> base_values = {0.45, 0.55},
    std::array<double, 2> teacher_values = {0.35, 0.65}) {
    RootFixture fixture =
        root_fixture(
            split, deck, ordinal, aliased_rows);
    return labels::testing::make_root_label(
        labels::testing::project_root(
            fixture.selected),
        fixture.actions,
        fixture.selected.source_root.action_descriptors,
        fixture.rows,
        samples(base_values, false),
        samples(teacher_values, true));
}

labels::RootLabel root_label_for_cell(
    density::Split split, old_school::DeckId deck,
    std::size_t ordinal, std::size_t action_count) {
    RootFixture fixture =
        root_fixture(
            split, deck, ordinal, false,
            action_count);
    std::vector<double> base_values(
        action_count, 0.5);
    std::vector<double> teacher_values(
        action_count, 0.5);
    teacher_values.back() = 0.75;
    return labels::testing::make_root_label(
        labels::testing::project_root(
            fixture.selected),
        fixture.actions,
        fixture.selected.source_root.action_descriptors,
        fixture.rows,
        samples(base_values, false),
        samples(teacher_values, true));
}

std::vector<labels::RootLabel> roots_for_all_cells(
    density::Split split, std::size_t first_ordinal) {
    std::vector<labels::RootLabel> roots;
    std::size_t ordinal = first_ordinal;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (const std::size_t action_count :
             {2U, 3U, 4U}) {
            roots.push_back(
                root_label_for_cell(
                    split,
                    static_cast<old_school::DeckId>(
                        deck),
                    ordinal++, action_count));
        }
    }
    return roots;
}

void test_fixed_recipe_cli_and_seeds() {
    expect(
        labels::kIdentifier ==
                "AQ18-DBC6-L1-DEEP-LABEL-CACHE" &&
            labels::kCacheSchema ==
                "old-school-aq18-dbc6-label-cache-v1" &&
            labels::kLabelSeed == 202607291802ULL &&
            labels::kRequiredSelectionManifest ==
                priority::kRequiredSelectionManifest &&
            labels::kExpectedTrainRoots == 300 &&
            labels::kExpectedDevRoots == 150 &&
            labels::kExpectedTrainOptions == 1088 &&
            labels::kExpectedDevOptions == 513 &&
            labels::kExpectedAliasPairs == 91 &&
            labels::kExpectedActionWorldCellsPerArm ==
                12808 &&
            density::kPolicyFeatureCount == 893,
        "AQ18 frozen identity or census drifted");
    const std::array<std::string_view, 1> publish{
        "--publish",
    };
    const std::array<std::string_view, 1> wrong{
        "--run",
    };
    expect(
        labels::parse_command(publish) ==
                labels::Command::Publish &&
            !labels::parse_command(wrong).has_value() &&
            !labels::parse_command({}).has_value(),
        "AQ18 command parser accepted an arbitrary mode");

    const auto base = labels::base_search_config(17);
    const auto teacher =
        labels::teacher_search_config(17);
    expect(
        base.seed == 17 &&
            base.worlds == 8 &&
            base.rollouts_per_world == 1 &&
            base.horizon_turns == 4 &&
            base.blend_shallow_prior &&
            base.value_continuation_search_worlds == 0 &&
            base.terminal_utility_mode ==
                old_school::LearnedTerminalUtilityMode::
                    ExactOutcome &&
            teacher.seed == 17 &&
            teacher.worlds == 8 &&
            teacher.rollouts_per_world == 1 &&
            teacher.horizon_turns == 8 &&
            !teacher.blend_shallow_prior &&
            teacher.value_continuation_search_worlds == 2 &&
            teacher.terminal_utility_mode ==
                old_school::LearnedTerminalUtilityMode::
                    ExactOutcome,
        "AQ18 base or teacher search recipe drifted");

    auto first = coordinate(
        density::Split::Train,
        old_school::DeckId::Green, 1);
    auto second = first;
    first.actor_game_nontrivial_roots = 3;
    second.actor_game_nontrivial_roots = 3;
    second.nontrivial_ordinal = 2;
    expect(
        labels::root_label_seed(first) ==
                labels::root_label_seed(first) &&
            labels::root_label_seed(first) !=
                labels::root_label_seed(second),
        "AQ18 semantic root seeds are not deterministic/distinct");
}

void test_safe_projection_and_root_label() {
    RootFixture fixture =
        root_fixture(
            density::Split::Train,
            old_school::DeckId::Blue, 3);
    const labels::ProjectedRoot projected =
        labels::testing::project_root(
            fixture.selected);
    expect(
        projected.split == density::Split::Train &&
            projected.owner_deck ==
                old_school::DeckId::Blue &&
            projected.width_stratum ==
                priority::WidthStratum::B2 &&
            projected.stable_root_id ==
                fixture.selected.source_root.stable_root_id &&
            projected.physical_game_group.size() == 64 &&
            projected.actor_game_group.size() == 64 &&
            projected.label_seed ==
                labels::root_label_seed(
                    fixture.selected.source_root.coordinate),
        "AQ18 safe root projection drifted");

    labels::RootLabel label = root_label(
        density::Split::Train,
        old_school::DeckId::Blue, 3);
    labels::validate_root_label(label);
    expect(
        label.actions.size() == 2 &&
            label.option_rows.size() == 2 &&
            label.option_rows.front().size() == 893 &&
            label.base_q_samples.front().size() == 8 &&
            label.teacher_q_samples.front().size() == 8 &&
            label.weight == labels::kTrainRootWeight &&
            std::abs(
                label.target_probabilities[0] +
                    label.target_probabilities[1] -
                    1.0) <
                1.0e-12,
        "AQ18 root label shape or target drifted");

    auto corrupted = label;
    corrupted.base_aggregate_scores[0] += 0.01;
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted a corrupted aggregate");
    corrupted = label;
    corrupted.teacher_terminal_flags[0][0] = 2;
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted an invalid terminal flag");
    corrupted = label;
    corrupted.option_rows[0].push_back(0.0);
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted a wrong-width feature row");
    corrupted = label;
    corrupted.actions[1] =
        old_school::PriorityAction::cast_creature(
            old_school::CardId::HillGiant);
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted a same-shape action mutation");
    corrupted = label;
    corrupted.option_rows[1].back() += 0.25;
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted a same-shape row mutation");
    corrupted = label;
    corrupted.identity.information_action_fingerprint =
        std::string(64, 'a');
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted a fingerprint mutation");
    corrupted = label;
    corrupted.target_probabilities[0] += 0.001;
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted a target mutation");
    corrupted = label;
    corrupted.weight += 0.001;
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted a root-weight mutation");
    corrupted = label;
    ++corrupted.base_accounting.rollout_evaluations;
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted an accounting-total mutation");
    corrupted = label;
    corrupted.teacher_accounting
        .inner_rollout_evaluations_by_cell[0][0] = 2;
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted an accounting-matrix mutation");
    corrupted = label;
    corrupted.identity.owner_deck =
        static_cast<old_school::DeckId>(255);
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted a noncanonical deck enum");
    corrupted = label;
    corrupted.option_rows[0][0] =
        std::numeric_limits<double>::quiet_NaN();
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted a NaN feature");
    corrupted = label;
    std::swap(
        corrupted.actions[0], corrupted.actions[1]);
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted noncanonical action order");
    corrupted = label;
    corrupted.action_descriptors.pop_back();
    expect_rejected(
        [&] {
            labels::validate_root_label(corrupted);
        },
        "AQ18 accepted an action-count mismatch");
}

void test_alias_diagnostic_and_action_order() {
    labels::RootLabel train = root_label(
        density::Split::Train,
        old_school::DeckId::Green, 4, true,
        {0.50, 0.50}, {0.20, 0.80});
    labels::RootLabel dev = root_label(
        density::Split::Dev,
        old_school::DeckId::Green, 5, true,
        {0.50, 0.50}, {0.20, 0.80});
    priority::AliasGroup train_alias{
        .split = density::Split::Train,
        .owner_deck = old_school::DeckId::Green,
        .width_stratum = priority::WidthStratum::B2,
        .stable_root_id = train.identity.stable_root_id,
        .action_descriptors =
            train.action_descriptors,
    };
    priority::AliasGroup dev_alias = train_alias;
    dev_alias.split = density::Split::Dev;
    dev_alias.stable_root_id =
        dev.identity.stable_root_id;
    dev_alias.action_descriptors =
        dev.action_descriptors;
    const std::array<priority::AliasGroup, 2> aliases{
        train_alias, dev_alias,
    };
    std::vector<labels::RootLabel> train_roots;
    std::vector<labels::RootLabel> dev_roots;
    train_roots.push_back(train);
    dev_roots.push_back(dev);
    std::size_t ordinal = 20;
    for (std::size_t deck = 0;
         deck < 5; ++deck) {
        for (const std::size_t action_count :
             {2U, 3U, 4U}) {
            if (deck == 0 && action_count == 2) {
                continue;
            }
            train_roots.push_back(
                root_label_for_cell(
                    density::Split::Train,
                    static_cast<old_school::DeckId>(
                        deck),
                    ordinal++, action_count));
            dev_roots.push_back(
                root_label_for_cell(
                    density::Split::Dev,
                    static_cast<old_school::DeckId>(
                        deck),
                    ordinal++, action_count));
        }
    }
    const labels::Diagnostics diagnostics =
        labels::evaluate_diagnostics(
            train_roots, dev_roots, aliases);
    expect(
        diagnostics.aliases.size() == 2 &&
            diagnostics.material_alias_conflicts == 2 &&
            diagnostics.maximum_absolute_alias_correction_gap >
                labels::kMaterialCorrectionGap,
        "AQ18 material alias conflict was not diagnosed");

    labels::RootLabel permuted =
        root_label_for_cell(
            density::Split::Train,
            old_school::DeckId::Blue, 90, 3);
    const auto original_second_action =
        permuted.actions[1];
    const double original_second_target =
        permuted.target_probabilities[1];
    const auto swap_nonpass = [](auto& values) {
        std::swap(values[1], values[2]);
    };
    swap_nonpass(permuted.actions);
    swap_nonpass(permuted.action_descriptors);
    swap_nonpass(permuted.option_rows);
    swap_nonpass(permuted.base_q_samples);
    swap_nonpass(permuted.base_terminal_flags);
    swap_nonpass(
        permuted.base_shallow_prior_samples);
    swap_nonpass(
        permuted.base_continuation_samples);
    swap_nonpass(permuted.base_aggregate_scores);
    swap_nonpass(permuted.teacher_q_samples);
    swap_nonpass(permuted.teacher_terminal_flags);
    swap_nonpass(
        permuted.teacher_shallow_prior_samples);
    swap_nonpass(
        permuted.teacher_continuation_samples);
    swap_nonpass(
        permuted.teacher_aggregate_scores);
    swap_nonpass(permuted.target_probabilities);
    swap_nonpass(
        permuted.teacher_accounting
            .inner_rollout_evaluations_by_cell);
    swap_nonpass(
        permuted.teacher_accounting
            .inner_search_invocations_by_cell);
    swap_nonpass(
        permuted.teacher_accounting
            .inner_search_max_depth_by_cell);
    permuted.identity.information_action_fingerprint =
        density::canonical_information_action_fingerprint(
            permuted.actions,
            permuted.action_descriptors,
            permuted.option_rows);
    permuted.target_probabilities =
        old_school::learned_soft_priority_target(
            permuted.teacher_aggregate_scores);
    labels::validate_root_label(permuted);
    expect(
        permuted.actions[2] ==
                original_second_action &&
            std::abs(
                permuted.target_probabilities[2] -
                original_second_target) <
                1.0e-15,
        "AQ18 legal-action permutation lost its label");
}

void test_codec_and_atomic_no_replace() {
    const std::vector<labels::RootLabel> train =
        roots_for_all_cells(
            density::Split::Train, 100);
    const std::vector<labels::RootLabel> dev =
        roots_for_all_cells(
            density::Split::Dev, 200);
    labels::Corpus corpus =
        labels::testing::make_unfrozen_corpus(
            train, dev, {});
    expect(
        corpus.diagnostics.aliases.empty() &&
            corpus.diagnostics
                    .material_alias_conflicts == 0 &&
            corpus.diagnostics
                    .maximum_absolute_alias_correction_gap ==
                0.0 &&
            std::all_of(
                corpus.diagnostics.cells.begin(),
                corpus.diagnostics.cells.end(),
                [](const labels::CellMetrics& cell) {
                    return cell.alias_pairs == 0 &&
                           cell.material_alias_conflicts ==
                               0 &&
                           cell.maximum_absolute_alias_correction_gap ==
                               0.0;
                }),
        "AQ18 zero-alias diagnostics were not exact");
    const std::string bytes =
        labels::testing::encode_unfrozen_cache(
            corpus);
    expect(
        !bytes.empty() &&
            labels::testing::decode_unfrozen_cache(
                bytes) == corpus,
        "AQ18 unfrozen cache roundtrip drifted");
    labels::Corpus structurally_corrupted = corpus;
    structurally_corrupted.train.front()
        .target_probabilities.front() += 0.001;
    structurally_corrupted.digest =
        labels::canonical_corpus_digest(
            structurally_corrupted);
    expect_rejected(
        [&] {
            static_cast<void>(
                labels::testing::encode_unfrozen_cache(
                    structurally_corrupted));
        },
        "AQ18 encoded a structurally invalid corpus");

    expect_rejected(
        [&] {
            static_cast<void>(
                labels::testing::decode_unfrozen_cache(
                    std::string_view(
                        bytes.data(),
                        bytes.size() - 1)));
        },
        "AQ18 accepted a truncated cache");
    std::string trailing = bytes;
    trailing.push_back('\0');
    expect_rejected(
        [&] {
            static_cast<void>(
                labels::testing::decode_unfrozen_cache(
                    trailing));
        },
        "AQ18 accepted trailing cache bytes");
    std::string flipped = bytes;
    flipped[flipped.size() / 2] ^= 0x01;
    expect_rejected(
        [&] {
            static_cast<void>(
                labels::testing::decode_unfrozen_cache(
                    flipped));
        },
        "AQ18 accepted a cache bit flip");
    const std::string oversized(
        labels::kMaximumCacheBytes + 4097U, '\0');
    expect_rejected(
        [&] {
            static_cast<void>(
                labels::testing::decode_unfrozen_cache(
                    oversized));
        },
        "AQ18 accepted an oversized cache");

    const auto directory =
        std::filesystem::temp_directory_path() /
        ("old-school-aq18-test-" +
         std::to_string(
             static_cast<unsigned long long>(
                 labels::root_label_seed(
                     coordinate(
                         density::Split::Dev,
                         old_school::DeckId::White,
                         8)))));
    std::error_code cleanup_error;
    std::filesystem::remove_all(
        directory, cleanup_error);
    std::filesystem::create_directories(directory);
    const auto target = directory / "cache.bin";
    labels::testing::write_bytes_atomic_no_replace(
        target, bytes);
    expect(
        std::filesystem::file_size(target) ==
            bytes.size(),
        "AQ18 atomic writer produced the wrong byte count");
    expect_rejected(
        [&] {
            labels::testing::
                write_bytes_atomic_no_replace(
                    target, bytes);
        },
        "AQ18 atomic writer replaced an existing target");
    std::size_t entries = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(directory)) {
        ++entries;
        expect(
            entry.path() == target,
            "AQ18 atomic writer left a temporary file");
    }
    expect(
        entries == 1,
        "AQ18 atomic writer left extra directory entries");
    std::filesystem::remove_all(
        directory, cleanup_error);
}

void run(
    std::string_view name, void (*test)()) {
    test();
    std::cout << "[PASS] " << name << '\n';
}

} // namespace

int main() {
    try {
        run(
            "fixed recipe, CLI, and semantic seeds",
            test_fixed_recipe_cli_and_seeds);
        run(
            "safe projection and root label",
            test_safe_projection_and_root_label);
        run(
            "alias diagnostic and action order",
            test_alias_diagnostic_and_action_order);
        run(
            "codec and atomic no-replace",
            test_codec_and_atomic_no_replace);
        std::cout
            << "4 decision-density label tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
