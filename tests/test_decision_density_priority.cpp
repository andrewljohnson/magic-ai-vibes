#include "old_school/decision_density_priority.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace density =
    old_school::decision_density_census;
namespace priority =
    old_school::decision_density_priority;
namespace iteration = old_school::learned_iteration;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function&& function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

template <typename T>
concept HasStateMember = requires(T value) {
    value.state;
};

template <typename T>
concept HasGameStateMember = requires(T value) {
    value.game_state;
};

template <typename T>
concept HasActionsMember = requires(T value) {
    value.actions;
};

template <typename T>
concept HasOptionRowsMember = requires(T value) {
    value.option_rows;
};

template <typename T>
concept HasOpponentHandMember = requires(T value) {
    value.opponent_hand;
};

template <typename T>
concept HasOpponentLibraryMember = requires(T value) {
    value.opponent_library;
};

static_assert(
    !HasStateMember<priority::SelectedRoot>);
static_assert(
    !HasGameStateMember<priority::SelectedRoot>);
static_assert(
    !HasActionsMember<priority::SelectedRoot>);
static_assert(
    !HasOptionRowsMember<priority::SelectedRoot>);
static_assert(
    !HasOpponentHandMember<priority::SelectedRoot>);
static_assert(
    !HasOpponentLibraryMember<priority::SelectedRoot>);
static_assert(
    !HasStateMember<priority::SelectionManifest>);
static_assert(
    !HasGameStateMember<priority::SelectionManifest>);
static_assert(
    !HasActionsMember<priority::SelectionManifest>);
static_assert(
    !HasOptionRowsMember<priority::SelectionManifest>);
static_assert(
    !HasOpponentHandMember<
        priority::SelectionManifest>);
static_assert(
    !HasOpponentLibraryMember<
        priority::SelectionManifest>);
static_assert(!HasStateMember<priority::RunReport>);
static_assert(
    !HasGameStateMember<priority::RunReport>);
static_assert(!HasActionsMember<priority::RunReport>);
static_assert(
    !HasOptionRowsMember<priority::RunReport>);
static_assert(
    !HasOpponentHandMember<priority::RunReport>);
static_assert(
    !HasOpponentLibraryMember<priority::RunReport>);

std::vector<old_school::PriorityAction>
actions_for_width(std::size_t width) {
    std::vector<old_school::PriorityAction> actions{
        old_school::PriorityAction::pass(),
        old_school::PriorityAction::play_land(
            old_school::CardId::Forest),
        old_school::PriorityAction::cast_creature(
            old_school::CardId::GrizzlyBears),
        old_school::PriorityAction::cast_lightning_bolt(
            old_school::Target::player_target(1)),
    };
    actions.resize(width);
    return actions;
}

std::vector<std::vector<double>> rows_for_root(
    std::size_t width, std::size_t unique_token) {
    std::vector<std::vector<double>> rows;
    rows.reserve(width);
    for (std::size_t option = 0;
         option < width; ++option) {
        std::vector<double> row(
            density::kPolicyFeatureCount, 0.0);
        row[0] =
            static_cast<double>(unique_token + 1);
        row[1] =
            option < 2
                ? 7.0
                : static_cast<double>(option + 7);
        rows.push_back(std::move(row));
    }
    return rows;
}

struct ActorGameSpec {
    std::size_t block = 0;
    iteration::ScheduledGame scheduled;
    std::size_t actor = 0;
};

std::array<std::array<std::vector<ActorGameSpec>,
                      old_school::kDeckCount>,
           2>
selected_actor_games() {
    std::array<
        std::array<std::vector<ActorGameSpec>,
                   old_school::kDeckCount>,
        2>
        result;
    const std::array<std::size_t, 3> blocks{
        density::kTrainBlocks[0],
        density::kTrainBlocks[1],
        density::kDevBlock,
    };
    for (const std::size_t block : blocks) {
        const std::size_t split =
            block == density::kDevBlock ? 1 : 0;
        const auto schedule =
            iteration::balanced_schedule(
                density::kCollectionRootSeed,
                density::kScheduleGeneration, block);
        for (const auto& scheduled : schedule) {
            for (std::size_t actor = 0;
                 actor < 2; ++actor) {
                auto& games =
                    result[split][static_cast<std::size_t>(
                        scheduled.seat_decks[actor])];
                if (games.size() < 2) {
                    games.push_back({
                        .block = block,
                        .scheduled = scheduled,
                        .actor = actor,
                    });
                }
            }
        }
    }
    return result;
}

std::vector<priority::PopulationRoot>
synthetic_population() {
    const auto selected = selected_actor_games();
    std::vector<priority::PopulationRoot> population;
    std::size_t unique_token = 0;
    const std::array<std::size_t, 3> blocks{
        density::kTrainBlocks[0],
        density::kTrainBlocks[1],
        density::kDevBlock,
    };
    for (const std::size_t block : blocks) {
        const std::size_t split =
            block == density::kDevBlock ? 1 : 0;
        const auto schedule =
            iteration::balanced_schedule(
                density::kCollectionRootSeed,
                density::kScheduleGeneration, block);
        for (const auto& scheduled : schedule) {
            for (std::size_t actor = 0;
                 actor < 2; ++actor) {
                const auto& games =
                    selected[split][
                        static_cast<std::size_t>(
                            scheduled
                                .seat_decks[actor])];
                const auto retained = std::find_if(
                    games.begin(), games.end(),
                    [&](const ActorGameSpec& game) {
                        return
                            game.block == block &&
                            game.scheduled.schedule_index ==
                                scheduled.schedule_index &&
                            game.actor == actor;
                    });
                if (retained == games.end()) {
                    continue;
                }
                constexpr std::size_t kRootsPerGame = 9;
                for (std::size_t ordinal = 0;
                     ordinal < kRootsPerGame; ++ordinal) {
                    const std::size_t width =
                        2 + ordinal / 3;
                    auto actions =
                        actions_for_width(width);
                    auto rows =
                        rows_for_root(
                            width, unique_token++);
                    const density::RootCoordinate coordinate{
                        .split =
                            split == 0
                                ? density::Split::Train
                                : density::Split::Dev,
                        .block_index = block,
                        .schedule_index =
                            scheduled.schedule_index,
                        .pairing_index =
                            scheduled.pairing_index,
                        .game_seed = scheduled.seed,
                        .starting_player =
                            scheduled.starting_player,
                        .seat_decks =
                            scheduled.seat_decks,
                        .actor = actor,
                        .trace_ordinal = ordinal,
                        .nontrivial_ordinal = ordinal,
                        .actor_game_nontrivial_roots =
                            kRootsPerGame,
                    };
                    population.push_back({
                        .source_root =
                            density::testing::
                                make_manifest_root(
                                    coordinate,
                                    actions, rows),
                        .actions = std::move(actions),
                        .option_rows = std::move(rows),
                        .hidden_repartition_witness =
                            true,
                    });
                }
            }
        }
    }
    return population;
}

constexpr std::string_view kSyntheticSourceManifest =
    "1111111111111111111111111111111111111111111111111111111111111111";

priority::SelectionManifest select_synthetic(
    std::span<const priority::PopulationRoot>
        population) {
    return priority::testing::select_population(
        std::string(
            density::kRequiredParentFingerprint),
        std::string(kSyntheticSourceManifest),
        population, 4, 2);
}

void test_fixed_recipe_and_cli() {
    expect(
        priority::kIdentifier ==
                "AQ17-DBC6-S0-DENSITY-SELECT" &&
            priority::kSelectionSchema ==
                "old-school-aq17-dbc6-select-v1" &&
            priority::kSelectionSeed ==
                202607291811ULL,
        "selection identity drifted");
    expect(
        priority::kRequiredCensusManifest ==
                "7de71c44a3d1d1fa20eb1b738bc8c675e83c4336f284e95f7401a4b79ea345cc" &&
            priority::kRequiredTrainRoots == 3597 &&
            priority::kRequiredDevRoots == 1687,
        "AQ16 source identity drifted");
    expect(
        priority::kTrainRootsPerCell == 20 &&
            priority::kDevRootsPerCell == 10 &&
            priority::kExpectedTrainRoots == 300 &&
            priority::kExpectedDevRoots == 150 &&
            priority::kActorGameCellCap == 2 &&
            priority::kActorGameTotalCap == 6,
        "fixed quotas or game caps drifted");
    expect(
        priority::width_stratum(2) ==
                priority::WidthStratum::B2 &&
            priority::width_stratum(3) ==
                priority::WidthStratum::B3 &&
            priority::width_stratum(4) ==
                priority::WidthStratum::B4Plus &&
            priority::width_stratum(100) ==
                priority::WidthStratum::B4Plus,
        "width strata drifted");
    expect_rejected(
        [] {
            static_cast<void>(
                priority::width_stratum(1));
        },
        "trivial action width was accepted");

    const std::array<std::string_view, 1> valid{
        "--select",
    };
    const std::array<std::string_view, 1> invalid{
        "--run",
    };
    expect(
        priority::parse_command(valid).has_value() &&
            !priority::parse_command(invalid).has_value() &&
            !priority::parse_command({}).has_value(),
        "CLI parser accepted an invalid mode");
    expect_rejected(
        [] {
            const density::AuthenticatedRootVisitor
                empty;
            static_cast<void>(
                density::collect_census(
                    nullptr, empty));
        },
        "empty transient visitor was accepted");
}

void test_key_and_bitwise_row_identity() {
    const auto population = synthetic_population();
    const std::string first =
        priority::selection_key(
            kSyntheticSourceManifest,
            population.front()
                .source_root.stable_root_id);
    const std::string repeated =
        priority::selection_key(
            kSyntheticSourceManifest,
            population.front()
                .source_root.stable_root_id);
    const std::string other =
        priority::selection_key(
            kSyntheticSourceManifest,
            population.back()
                .source_root.stable_root_id);
    expect(
        first == repeated && first != other &&
            first.size() == 64,
        "selection key was not stable and sensitive");

    const std::array<double, 2> positive{
        0.0, 1.0,
    };
    const std::array<double, 2> identical{
        0.0, 1.0,
    };
    const std::array<double, 2> negative_zero{
        -0.0, 1.0,
    };
    expect(
        priority::testing::rows_bit_identical(
            positive, identical),
        "identical rows were not bit-identical");
    expect(
        !priority::testing::rows_bit_identical(
            positive, negative_zero),
        "positive and negative zero were merged");
}

void test_balanced_two_round_selection_and_aliases() {
    const auto population = synthetic_population();
    const priority::SelectionManifest first =
        select_synthetic(population);
    const priority::SelectionManifest repeated =
        select_synthetic(population);
    expect(
        first == repeated,
        "pure selector was not bit-identical");
    priority::validate_manifest(first);
    expect(
        first.train_roots == 60 &&
            first.dev_roots == 30 &&
            first.selected_roots.size() == 90 &&
            first.selected_options == 270 &&
            first.selected_potential_pairs == 300,
        "synthetic selection totals drifted");
    expect(
        first.alias_groups.size() == 90 &&
            first.collision_pairs == 90,
        "bit-identical alias census drifted");
    expect(
        first.max_roots_per_actor_game == 6,
        "total actor-game cap was not exercised");

    for (const priority::CellCensus& cell :
         first.cells) {
        const std::size_t expected_quota =
            cell.split == density::Split::Train
                ? 4
                : 2;
        const std::size_t width =
            cell.width_stratum ==
                    priority::WidthStratum::B2
                ? 2
                : cell.width_stratum ==
                          priority::WidthStratum::B3
                      ? 3
                      : 4;
        expect(
            cell.quota == expected_quota &&
                cell.selected_roots ==
                    expected_quota &&
                cell.source_roots == 6 &&
                cell.source_distinct_actor_games ==
                    2 &&
                cell.source_two_round_capacity == 4 &&
                cell.selected_distinct_actor_games ==
                    2 &&
                cell.max_roots_per_actor_game ==
                    (cell.split ==
                             density::Split::Train
                         ? 2
                         : 1) &&
                cell.selected_options ==
                    expected_quota * width &&
                cell.selected_potential_pairs ==
                    expected_quota *
                        density::potential_pair_count(
                            width) &&
                cell.option_pair_denominator ==
                    cell.selected_potential_pairs &&
                cell.collision_groups ==
                    expected_quota &&
                cell.collision_pairs ==
                    expected_quota,
            "cell balance, cap, or alias census drifted");
    }
    for (std::size_t index = 1;
         index < first.selected_roots.size(); ++index) {
        const auto& previous =
            first.selected_roots[index - 1]
                .source_root.coordinate;
        const auto& current =
            first.selected_roots[index]
                .source_root.coordinate;
        const auto key = [](const auto& coordinate) {
            return std::tuple{
                density::split_index(
                    coordinate.split),
                coordinate.block_index,
                coordinate.schedule_index,
                coordinate.actor,
                coordinate.nontrivial_ordinal,
                coordinate.trace_ordinal,
            };
        };
        expect(
            key(previous) < key(current),
            "selected roots were not restored to source order");
    }
}

void test_selector_rejects_capacity_and_authentication_drift() {
    const auto population = synthetic_population();
    auto insufficient = population;
    const auto first_green_train_b2 =
        std::find_if(
            insufficient.begin(), insufficient.end(),
            [](const priority::PopulationRoot& root) {
                return
                    root.source_root.coordinate.split ==
                        density::Split::Train &&
                    root.source_root.coordinate
                            .owner_deck() ==
                        old_school::DeckId::Green &&
                    root.source_root.legal_action_count ==
                        2;
            });
    expect(
        first_green_train_b2 != insufficient.end(),
        "synthetic capacity fixture is empty");
    const auto actor =
        first_green_train_b2->source_root.coordinate.actor;
    const auto block =
        first_green_train_b2->source_root.coordinate
            .block_index;
    const auto schedule =
        first_green_train_b2->source_root.coordinate
            .schedule_index;
    std::erase_if(
        insufficient,
        [&](const priority::PopulationRoot& root) {
            const auto& coordinate =
                root.source_root.coordinate;
            return
                coordinate.split ==
                    density::Split::Train &&
                coordinate.owner_deck() ==
                    old_school::DeckId::Green &&
                root.source_root.legal_action_count == 2 &&
                coordinate.actor == actor &&
                coordinate.block_index == block &&
                coordinate.schedule_index == schedule;
        });
    expect_rejected(
        [&] {
            static_cast<void>(
                select_synthetic(insufficient));
        },
        "insufficient two-round capacity was accepted");

    auto unauthenticated = population;
    unauthenticated.front().option_rows[0][42] = 1.0;
    expect_rejected(
        [&] {
            static_cast<void>(
                select_synthetic(unauthenticated));
        },
        "feature mutation survived authentication");

    auto noncanonical = population;
    std::swap(noncanonical[0], noncanonical[1]);
    expect_rejected(
        [&] {
            static_cast<void>(
                select_synthetic(noncanonical));
        },
        "noncanonical source order was accepted");
}

void test_manifest_mutation_sensitivity() {
    const auto population = synthetic_population();
    const priority::SelectionManifest baseline =
        select_synthetic(population);

    auto mutated = baseline;
    ++mutated.cells.front().actor_seats[0];
    expect_rejected(
        [&] {
            priority::validate_manifest(mutated);
        },
        "cell marginal mutation was accepted");

    mutated = baseline;
    mutated.selected_roots.front().selection_key[0] =
        mutated.selected_roots.front()
                    .selection_key[0] == '0'
            ? '1'
            : '0';
    mutated.manifest_hash =
        priority::canonical_manifest_hash(mutated);
    expect_rejected(
        [&] {
            priority::validate_manifest(mutated);
        },
        "rehashed selection-key mutation was accepted");

    mutated = baseline;
    mutated.alias_groups.front()
        .action_descriptors.pop_back();
    mutated.manifest_hash =
        priority::canonical_manifest_hash(mutated);
    expect_rejected(
        [&] {
            priority::validate_manifest(mutated);
        },
        "rehashed singleton alias group was accepted");
}

} // namespace

int main() {
    try {
        test_fixed_recipe_and_cli();
        std::cout
            << "ok - fixed AQ17 recipe and CLI\n";
        test_key_and_bitwise_row_identity();
        std::cout
            << "ok - key and bitwise row identity\n";
        test_balanced_two_round_selection_and_aliases();
        std::cout
            << "ok - balanced two-round selector and aliases\n";
        test_selector_rejects_capacity_and_authentication_drift();
        std::cout
            << "ok - capacity and authentication rejection\n";
        test_manifest_mutation_sensitivity();
        std::cout
            << "ok - selected manifest mutation sensitivity\n";
        std::cout
            << "All decision-density priority tests passed "
               "(5/5).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
