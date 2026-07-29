#include "old_school/decision_density_census.hpp"

#include <algorithm>
#include <array>
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

namespace density =
    old_school::decision_density_census;
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
concept HasOutcomeMember = requires(T value) {
    value.outcome;
};

template <typename T>
concept HasWinnerMember = requires(T value) {
    value.winner;
};

template <typename T>
concept HasTeacherScoreMember = requires(T value) {
    value.teacher_scores;
};

template <typename T>
concept HasCandidateScoreMember = requires(T value) {
    value.candidate_scores;
};

template <typename T>
concept HasSampledWorldMember = requires(T value) {
    value.sampled_world;
};

template <typename T>
concept HasOpponentHandMember = requires(T value) {
    value.opponent_hand;
};

template <typename T>
concept HasOpponentLibraryMember = requires(T value) {
    value.opponent_library;
};

template <typename T>
concept HasOptionRowsMember = requires(T value) {
    value.option_rows;
};

template <typename T>
concept HasActionsMember = requires(T value) {
    value.actions;
};

static_assert(!HasStateMember<density::ManifestRoot>);
static_assert(!HasGameStateMember<density::ManifestRoot>);
static_assert(!HasOutcomeMember<density::ManifestRoot>);
static_assert(!HasWinnerMember<density::ManifestRoot>);
static_assert(!HasTeacherScoreMember<density::ManifestRoot>);
static_assert(!HasCandidateScoreMember<density::ManifestRoot>);
static_assert(!HasSampledWorldMember<density::ManifestRoot>);
static_assert(!HasOpponentHandMember<density::ManifestRoot>);
static_assert(!HasOpponentLibraryMember<density::ManifestRoot>);
static_assert(!HasOptionRowsMember<density::ManifestRoot>);
static_assert(!HasActionsMember<density::ManifestRoot>);

density::Split split_for_block(std::size_t block) {
    return block == density::kDevBlock
               ? density::Split::Dev
               : density::Split::Train;
}

std::vector<old_school::PriorityAction> actions_for_width(
    std::size_t width) {
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

std::vector<std::vector<double>> option_rows(
    std::size_t width, std::size_t block,
    std::size_t schedule, std::size_t actor) {
    std::vector<std::vector<double>> rows;
    rows.reserve(width);
    for (std::size_t option = 0;
         option < width; ++option) {
        std::vector<double> row(
            density::kPolicyFeatureCount, 0.0);
        row[0] = static_cast<double>(block + 1);
        row[1] = static_cast<double>(schedule + 1);
        row[2] = static_cast<double>(actor + 1);
        row[3] = static_cast<double>(option + 1);
        rows.push_back(std::move(row));
    }
    return rows;
}

density::RootCoordinate coordinate_for(
    std::size_t block,
    const iteration::ScheduledGame& scheduled,
    std::size_t actor) {
    return {
        .split = split_for_block(block),
        .block_index = block,
        .schedule_index = scheduled.schedule_index,
        .pairing_index = scheduled.pairing_index,
        .game_seed = scheduled.seed,
        .starting_player = scheduled.starting_player,
        .seat_decks = scheduled.seat_decks,
        .actor = actor,
        .trace_ordinal = actor,
        .nontrivial_ordinal = 0,
        .actor_game_nontrivial_roots = 1,
    };
}

std::vector<density::ManifestRoot> synthetic_roots() {
    std::vector<density::ManifestRoot> roots;
    roots.reserve(
        density::kTrainActorGames +
        density::kDevActorGames);
    const std::array<std::size_t, 3> blocks{
        density::kTrainBlocks[0],
        density::kTrainBlocks[1],
        density::kDevBlock,
    };
    for (const std::size_t block : blocks) {
        const auto schedule =
            iteration::balanced_schedule(
                density::kCollectionRootSeed,
                density::kScheduleGeneration,
                block);
        for (const auto& scheduled : schedule) {
            for (std::size_t actor = 0;
                 actor < 2; ++actor) {
                const std::size_t width =
                    2 +
                    (block + scheduled.schedule_index +
                     actor) %
                        3;
                roots.push_back(
                    density::testing::make_manifest_root(
                        coordinate_for(
                            block, scheduled, actor),
                        actions_for_width(width),
                        option_rows(
                            width, block,
                            scheduled.schedule_index,
                            actor)));
            }
        }
    }
    return roots;
}

density::Census synthetic_census() {
    return density::testing::make_census(
        std::string(
            density::kRequiredParentFingerprint),
        synthetic_roots());
}

void test_fixed_recipe_and_owner_safe_shape() {
    expect(
        density::kCollectionRootSeed ==
            202607291801ULL,
        "wrong collection root seed");
    expect(
        density::kScheduleGeneration == 15 &&
            density::kTrainBlocks ==
                std::array<std::size_t, 2>{0, 1} &&
            density::kDevBlock == 2,
        "wrong schedule coordinates");
    expect(
        density::kSourceTurnCap == 128 &&
            density::kSourceWorlds == 8 &&
            density::kSourceRolloutsPerWorld == 1 &&
            density::kSourceHorizonTurns == 4 &&
            density::kPolicyFeatureCount == 893,
        "wrong source or feature recipe");
    expect(
        density::kTrainGames == 80 &&
            density::kDevGames == 40 &&
            density::kTrainActorGamesPerDeck == 32 &&
            density::kDevActorGamesPerDeck == 16,
        "wrong balanced source size");
    expect(
        density::potential_pair_count(2) == 1 &&
            density::potential_pair_count(3) == 3 &&
            density::potential_pair_count(4) == 6,
        "potential-pair arithmetic drifted");
    expect_rejected(
        [] {
            static_cast<void>(
                density::potential_pair_count(
                    std::numeric_limits<
                        std::size_t>::max()));
        },
        "overflowing potential-pair count was accepted");

    const std::array<std::string_view, 1> valid{
        "--census",
    };
    const std::array<std::string_view, 1> invalid{
        "--run",
    };
    expect(
        density::parse_command(valid).has_value() &&
            !density::parse_command(invalid).has_value() &&
            !density::parse_command({}).has_value(),
        "command parser accepted an invalid mode");
}

void test_full_schedule_aggregation_and_widths() {
    auto schedule =
        iteration::balanced_schedule(
            density::kCollectionRootSeed,
            density::kScheduleGeneration,
            density::kTrainBlocks[0]);
    density::testing::validate_schedule_block(
        density::kTrainBlocks[0], schedule);
    auto imbalanced = schedule;
    imbalanced.front().starting_player = 1;
    expect_rejected(
        [&] {
            density::testing::validate_schedule_block(
                density::kTrainBlocks[0], imbalanced);
        },
        "play/draw-imbalanced schedule was accepted");
    imbalanced = schedule;
    std::swap(
        imbalanced.front().seat_decks[0],
        imbalanced.front().seat_decks[1]);
    expect_rejected(
        [&] {
            density::testing::validate_schedule_block(
                density::kTrainBlocks[0], imbalanced);
        },
        "seat-imbalanced schedule was accepted");
    imbalanced = schedule;
    imbalanced.front().pairing_index = 1;
    expect_rejected(
        [&] {
            density::testing::validate_schedule_block(
                density::kTrainBlocks[0], imbalanced);
        },
        "pairing-drifted schedule was accepted");

    const density::Census empty =
        density::testing::make_census(
            std::string(
                density::kRequiredParentFingerprint),
            {});
    expect(
        empty.splits[0].actor_game_rows.size() ==
                density::kTrainActorGames &&
            empty.splits[1].actor_game_rows.size() ==
                density::kDevActorGames &&
            empty.splits[0].roots == 0 &&
            empty.splits[1].roots == 0,
        "zero-root actor-games disappeared from the census");

    const density::Census census = synthetic_census();
    density::validate_census(census);
    expect(
        census.roots.size() ==
            density::kTrainActorGames +
                density::kDevActorGames,
        "synthetic root total drifted");
    expect(
        census.splits[0].games ==
                density::kTrainGames &&
            census.splits[0].actor_games ==
                density::kTrainActorGames &&
            census.splits[0].roots ==
                density::kTrainActorGames &&
            census.splits[1].games ==
                density::kDevGames &&
            census.splits[1].actor_games ==
                density::kDevActorGames &&
            census.splits[1].roots ==
                density::kDevActorGames,
        "split totals did not cross-sum");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect(
            census.splits[0].decks[deck].actor_games ==
                    density::kTrainActorGamesPerDeck &&
                census.splits[1].decks[deck].actor_games ==
                    density::kDevActorGamesPerDeck,
            "deck schedule balance drifted");
    }
    for (const density::SplitCensus& split :
         census.splits) {
        std::size_t width_roots = 0;
        std::size_t width_options = 0;
        std::size_t width_pairs = 0;
        for (const density::WidthCensus& width :
             split.widths) {
            expect(
                width.legal_action_count >= 2 &&
                    width.options ==
                        width.roots *
                            width.legal_action_count &&
                    width.potential_pairs ==
                        width.roots *
                            density::potential_pair_count(
                                width.legal_action_count),
                "width stratum arithmetic drifted");
            width_roots += width.roots;
            width_options += width.options;
            width_pairs += width.potential_pairs;
        }
        expect(
            width_roots == split.roots &&
                width_options == split.options &&
                width_pairs == split.potential_pairs,
            "width strata did not cross-sum");
    }
    expect(
        census.manifest_hash ==
            density::canonical_manifest_hash(census),
        "manifest hash was not canonical");
    const auto block_zero =
        std::find_if(
            census.roots.begin(), census.roots.end(),
            [](const density::ManifestRoot& root) {
                return root.coordinate.block_index == 0 &&
                       root.coordinate.schedule_index == 0 &&
                       root.coordinate.actor == 0;
            });
    const auto block_one =
        std::find_if(
            census.roots.begin(), census.roots.end(),
            [](const density::ManifestRoot& root) {
                return root.coordinate.block_index == 1 &&
                       root.coordinate.schedule_index == 0 &&
                       root.coordinate.actor == 0;
            });
    expect(
        block_zero != census.roots.end() &&
            block_one != census.roots.end() &&
            block_zero->coordinate.game_seed !=
                block_one->coordinate.game_seed &&
            block_zero->stable_root_id !=
                block_one->stable_root_id,
        "TRAIN block identity was omitted from root provenance");
}

void test_action_feature_hash_is_mutation_sensitive() {
    const auto schedule =
        iteration::balanced_schedule(
            density::kCollectionRootSeed,
            density::kScheduleGeneration, 0);
    const density::RootCoordinate coordinate =
        coordinate_for(0, schedule.front(), 0);
    auto actions = actions_for_width(3);
    auto baseline_rows = option_rows(3, 0, 0, 0);
    const density::ManifestRoot baseline =
        density::testing::make_manifest_root(
            coordinate, actions, baseline_rows);
    baseline_rows[0][17] = 1.0;
    const density::ManifestRoot public_mutation =
        density::testing::make_manifest_root(
            coordinate, actions, baseline_rows);
    expect(
        baseline.information_action_fingerprint !=
                public_mutation
                    .information_action_fingerprint &&
            baseline.stable_root_id !=
                public_mutation.stable_root_id,
        "public feature mutation was not detected");

    std::swap(actions[1], actions[2]);
    const density::ManifestRoot action_mutation =
        density::testing::make_manifest_root(
            coordinate, actions,
            option_rows(3, 0, 0, 0));
    expect(
        baseline.information_action_fingerprint !=
                action_mutation
                    .information_action_fingerprint &&
            baseline.stable_root_id !=
                action_mutation.stable_root_id,
        "ordered action mutation was not detected");
}

void test_all_priority_descriptor_kinds_round_trip() {
    const auto schedule =
        iteration::balanced_schedule(
            density::kCollectionRootSeed,
            density::kScheduleGeneration, 0);
    const density::RootCoordinate coordinate =
        coordinate_for(0, schedule.front(), 0);
    const std::vector<old_school::PriorityAction> actions{
        old_school::PriorityAction::pass(),
        old_school::PriorityAction::play_land(
            old_school::CardId::Forest),
        old_school::PriorityAction::cast_creature(
            old_school::CardId::GrizzlyBears),
        old_school::PriorityAction::cast_sorcery(
            old_school::CardId::TimeWalk),
        old_school::PriorityAction::cast_artifact(
            old_school::CardId::SolRing),
        old_school::PriorityAction::cast_enchantment(
            old_school::CardId::Moat),
        old_school::PriorityAction::cast_lightning_bolt(
            old_school::Target::player_target(1)),
        old_school::PriorityAction::cast_disintegrate(
            0, old_school::Target::player_target(1)),
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 1)),
        old_school::PriorityAction::cast_counterspell(1),
        old_school::PriorityAction::cast_ancestral_recall(
            old_school::Target::player_target(0)),
        old_school::PriorityAction::cast_braingeyser(
            0, old_school::Target::player_target(0)),
        old_school::PriorityAction::cast_force_spike(1),
        old_school::PriorityAction::activate_millstone(
            1, old_school::Target::player_target(1)),
    };
    expect(
        actions.size() ==
            static_cast<std::size_t>(
                old_school::PriorityActionKind::
                    ActivateMillstone) +
                1,
        "all-kind descriptor fixture is incomplete");
    for (std::size_t index = 0;
         index < actions.size(); ++index) {
        expect(
            static_cast<std::size_t>(
                actions[index].kind) == index,
            "all-kind descriptor fixture is out of order");
    }
    const density::ManifestRoot root =
        density::testing::make_manifest_root(
            coordinate, actions,
            option_rows(actions.size(), 0, 0, 0));
    density::validate_manifest_root(root);
    expect(
        root.action_descriptors.size() ==
            actions.size(),
        "all-kind descriptors did not round trip");
}

void test_live_hidden_repartition_and_owner_mutations() {
    old_school::GameState state;
    state.active_player = 0;
    state.players[0].hand = {
        old_school::CardId::Forest,
    };
    state.players[0].library = {
        old_school::CardId::GrizzlyBears,
        old_school::CardId::GiantGrowth,
    };
    state.players[1].hand = {
        old_school::CardId::LightningBolt,
        old_school::CardId::Mountain,
    };
    state.players[1].library = {
        old_school::CardId::IronclawOrcs,
        old_school::CardId::HillGiant,
    };
    old_school::LearnedDecisionTracePoint point{
        .state = state,
        .context = {
            .valid = true,
            .phase = old_school::TurnPhase::FirstMain,
            .decision_player = 0,
            .consecutive_passes = 0,
            .sorcery_actions = true,
        },
    };
    const auto schedule =
        iteration::balanced_schedule(
            density::kCollectionRootSeed,
            density::kScheduleGeneration, 0);
    const density::RootCoordinate coordinate =
        coordinate_for(0, schedule.front(), 0);
    const density::ManifestRoot baseline =
        density::testing::make_live_manifest_root(
            point, coordinate);
    point.state.players[0].life = 19;
    const density::ManifestRoot public_mutation =
        density::testing::make_live_manifest_root(
            point, coordinate);
    expect(
        public_mutation.information_action_fingerprint !=
                baseline.information_action_fingerprint &&
            public_mutation.stable_root_id !=
                baseline.stable_root_id,
        "public-state mutation was not detected");
    point.state = state;
    const auto hidden =
        density::testing::hidden_repartition(
            point.state, 0);
    expect(
        hidden.has_value() &&
            *hidden != point.state &&
            old_school::observe_game_state(
                point.state, 0) ==
                old_school::observe_game_state(
                    *hidden, 0),
        "hidden repartition was vacuous or owner-visible");
    point.state = *hidden;
    const density::ManifestRoot hidden_root =
        density::testing::make_live_manifest_root(
            point, coordinate);
    expect(
        hidden_root == baseline,
        "opponent hidden repartition changed root material");

    point.state.players[0].hand = {
        old_school::CardId::Mountain,
    };
    const density::ManifestRoot own_mutation =
        density::testing::make_live_manifest_root(
            point, coordinate);
    expect(
        own_mutation.information_action_fingerprint !=
                baseline.information_action_fingerprint &&
            own_mutation.stable_root_id !=
                baseline.stable_root_id,
        "own-hand mutation was not detected");
}

void test_manifest_and_census_mutations_fail_closed() {
    const density::Census baseline = synthetic_census();

    density::Census changed = baseline;
    changed.roots.front().potential_pairs += 1;
    expect_rejected(
        [&] { density::validate_census(changed); },
        "potential-pair mutation was accepted");

    changed = baseline;
    changed.roots.front().action_descriptors[1] =
        "kind-1.card-00.x-0";
    changed.roots.front().stable_root_id =
        density::stable_root_id(
            changed.roots.front().coordinate,
            changed.roots.front().action_descriptors,
            changed.roots.front()
                .information_action_fingerprint,
            changed.roots.front().legal_action_count,
            changed.roots.front().potential_pairs);
    changed.manifest_hash =
        density::canonical_manifest_hash(changed);
    expect_rejected(
        [&] { density::validate_census(changed); },
        "fully rehashed noncanonical descriptor was accepted");

    const auto reject_fully_rehashed_descriptor =
        [&](std::string descriptor) {
            density::Census mutation = baseline;
            mutation.roots.front().action_descriptors[1] =
                std::move(descriptor);
            mutation.roots.front().stable_root_id =
                density::stable_root_id(
                    mutation.roots.front().coordinate,
                    mutation.roots.front()
                        .action_descriptors,
                    mutation.roots.front()
                        .information_action_fingerprint,
                    mutation.roots.front()
                        .legal_action_count,
                    mutation.roots.front()
                        .potential_pairs);
            mutation.manifest_hash =
                density::canonical_manifest_hash(
                    mutation);
            expect_rejected(
                [&] {
                    density::validate_census(
                        mutation);
                },
                "fully rehashed impossible action was accepted");
        };
    for (const std::string_view descriptor : {
             "kind-7.card-17.x--1.target-player-1",
             "kind-11.card-23.x--1.target-player-0",
             "kind-6.card-3.x-0.target-player-1.creature-0",
             "kind-8.card-18.x-0.target-player-0.creature-0",
             "kind-9.card-7.x-0.spell-0",
             "kind-12.card-24.x-0.spell-0",
             "kind-13.card-11.x-0.target-player-1.source-0",
         }) {
        reject_fully_rehashed_descriptor(
            std::string(descriptor));
    }

    changed = baseline;
    changed.roots.front().information_action_fingerprint =
        "not-a-hash";
    expect_rejected(
        [&] { density::validate_census(changed); },
        "malformed fingerprint was accepted");

    changed = baseline;
    changed.roots.push_back(changed.roots.front());
    expect_rejected(
        [&] { density::validate_census(changed); },
        "duplicate root was accepted");

    changed = baseline;
    ++changed.splits[0].roots;
    expect_rejected(
        [&] { density::validate_census(changed); },
        "aggregate count mutation was accepted");

    changed = baseline;
    changed.manifest_hash[0] =
        changed.manifest_hash[0] == '0' ? '1' : '0';
    expect_rejected(
        [&] { density::validate_census(changed); },
        "manifest hash mutation was accepted");

    std::vector<density::ManifestRoot> roots =
        baseline.roots;
    density::RootCoordinate wrong_coordinate =
        roots.front().coordinate;
    ++wrong_coordinate.game_seed;
    roots.front() =
        density::testing::make_manifest_root(
            wrong_coordinate,
            actions_for_width(
                roots.front().legal_action_count),
            option_rows(
                roots.front().legal_action_count,
                wrong_coordinate.block_index,
                wrong_coordinate.schedule_index,
                wrong_coordinate.actor));
    expect_rejected(
        [&] {
            static_cast<void>(
                density::testing::make_census(
                    std::string(
                        density::
                            kRequiredParentFingerprint),
                    roots));
        },
        "off-schedule game seed was accepted");
}

void test_bad_rows_and_duplicate_actions_fail_closed() {
    const auto schedule =
        iteration::balanced_schedule(
            density::kCollectionRootSeed,
            density::kScheduleGeneration, 0);
    const density::RootCoordinate coordinate =
        coordinate_for(0, schedule.front(), 0);

    auto rows = option_rows(2, 0, 0, 0);
    rows.front().pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                density::testing::make_manifest_root(
                    coordinate,
                    actions_for_width(2), rows));
        },
        "short feature row was accepted");

    rows = option_rows(2, 0, 0, 0);
    rows.front().front() =
        std::numeric_limits<double>::infinity();
    expect_rejected(
        [&] {
            static_cast<void>(
                density::testing::make_manifest_root(
                    coordinate,
                    actions_for_width(2), rows));
        },
        "nonfinite feature was accepted");

    auto actions = actions_for_width(2);
    actions[1] = actions[0];
    expect_rejected(
        [&] {
            static_cast<void>(
                density::testing::make_manifest_root(
                    coordinate, actions,
                    option_rows(2, 0, 0, 0)));
        },
        "duplicate action was accepted");
}

void test_report_is_census_only() {
    const density::Census census = synthetic_census();
    const density::RunReport report{
        .census = census,
        .repeated_collection_bit_identical = true,
        .hidden_repartition_witness = true,
        .hidden_witness_root_id =
            census.roots.front().stable_root_id,
        .source_collections = 2,
    };
    std::ostringstream output;
    density::print_report(output, report);
    const std::string text = output.str();
    expect(
        text.find("result=PASS") !=
                std::string::npos &&
            text.find("disposition=CENSUS_ONLY") !=
                std::string::npos &&
            text.find("teacher_labels=0") !=
                std::string::npos &&
            text.find("candidate_scores=0") !=
                std::string::npos &&
            text.find("model_created=0") !=
                std::string::npos &&
            text.find("artifact_published=0") !=
                std::string::npos,
        "report implied work beyond the census");

    density::RunReport invalid = report;
    invalid.source_collections = 1;
    expect_rejected(
        [&] {
            std::ostringstream sink;
            density::print_report(sink, invalid);
        },
        "single-collection report was accepted");

    invalid = report;
    invalid.hidden_witness_root_id =
        std::string(64, '0');
    expect_rejected(
        [&] {
            std::ostringstream sink;
            density::print_report(sink, invalid);
        },
        "absent canonical-looking hidden witness was accepted");
}

} // namespace

int main() {
    std::size_t passed = 0;
    const auto run =
        [&](std::string_view name, auto&& test) {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        };

    run(
        "fixed recipe and owner-safe shape",
        test_fixed_recipe_and_owner_safe_shape);
    run(
        "full schedule aggregation and widths",
        test_full_schedule_aggregation_and_widths);
    run(
        "action-feature hash is mutation sensitive",
        test_action_feature_hash_is_mutation_sensitive);
    run(
        "all priority descriptor kinds round trip",
        test_all_priority_descriptor_kinds_round_trip);
    run(
        "live hidden repartition and owner mutations",
        test_live_hidden_repartition_and_owner_mutations);
    run(
        "manifest and census mutations fail closed",
        test_manifest_and_census_mutations_fail_closed);
    run(
        "bad rows and duplicate actions fail closed",
        test_bad_rows_and_duplicate_actions_fail_closed);
    run(
        "report is census only",
        test_report_is_census_only);

    std::cout << passed
              << " decision-density census tests passed\n";
}
