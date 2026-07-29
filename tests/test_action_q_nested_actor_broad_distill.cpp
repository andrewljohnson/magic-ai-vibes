#include "old_school/action_q_nested_actor_broad_distill.hpp"

#include "old_school/learned_iteration.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace aq =
    old_school::action_q_nested_actor_broad_distill;
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
concept HasOpponentHandMember = requires(T value) {
    value.opponent_hand;
};

template <typename T>
concept HasOpponentLibraryMember = requires(T value) {
    value.opponent_library;
};

template <typename T>
concept HasSourceOutcomeMember = requires(T value) {
    value.source_outcome;
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
concept HasLibraryOrderMember = requires(T value) {
    value.library_order;
};

template <typename T>
concept HasSampledWorldMember = requires(T value) {
    value.sampled_world;
};

static_assert(!HasStateMember<aq::ManifestRoot>);
static_assert(!HasGameStateMember<aq::ManifestRoot>);
static_assert(!HasOpponentHandMember<aq::ManifestRoot>);
static_assert(!HasOpponentLibraryMember<aq::ManifestRoot>);
static_assert(!HasSourceOutcomeMember<aq::ManifestRoot>);
static_assert(!HasOutcomeMember<aq::ManifestRoot>);
static_assert(!HasWinnerMember<aq::ManifestRoot>);
static_assert(!HasLibraryOrderMember<aq::ManifestRoot>);
static_assert(!HasSampledWorldMember<aq::ManifestRoot>);
static_assert(!HasStateMember<aq::RootExample>);
static_assert(!HasGameStateMember<aq::RootExample>);
static_assert(!HasOpponentHandMember<aq::RootExample>);
static_assert(!HasOpponentLibraryMember<aq::RootExample>);
static_assert(!HasSourceOutcomeMember<aq::RootExample>);
static_assert(!HasOutcomeMember<aq::RootExample>);
static_assert(!HasWinnerMember<aq::RootExample>);
static_assert(!HasLibraryOrderMember<aq::RootExample>);
static_assert(!HasSampledWorldMember<aq::RootExample>);

std::size_t deck_index(old_school::DeckId deck) {
    return static_cast<std::size_t>(deck);
}

std::string synthetic_identity(
    aq::Split split, std::size_t schedule_index,
    std::size_t actor, char suffix) {
    std::ostringstream output;
    output << (split == aq::Split::Train ? "train" : "dev")
           << '/' << schedule_index << '/' << actor << '/'
           << suffix;
    return output.str();
}

aq::ManifestRoot synthetic_root(
    aq::Split split,
    const iteration::ScheduledGame& scheduled,
    std::size_t actor) {
    std::vector<old_school::PriorityAction> actions{
        old_school::PriorityAction::pass(),
        old_school::PriorityAction::play_land(
            old_school::CardId::Forest),
    };
    std::vector<std::string> descriptors;
    std::vector<std::vector<double>> options;
    descriptors.reserve(actions.size());
    options.reserve(actions.size());
    for (std::size_t option = 0;
         option < actions.size(); ++option) {
        descriptors.push_back(
            old_school::probes::
                stable_priority_action_descriptor(
                    actions[option]));
        std::vector<double> features(
            aq::kPolicyFeatureCount, 0.0);
        features[0] =
            static_cast<double>(
                deck_index(scheduled.seat_decks[actor]) + 1);
        features[1] =
            static_cast<double>(scheduled.schedule_index + 1);
        features[2] = static_cast<double>(actor + 1);
        features[3] = static_cast<double>(option + 1);
        features[4] =
            split == aq::Split::Train ? 1.0 : -1.0;
        options.push_back(std::move(features));
    }
    return {
        .coordinate = {
            .split = split,
            .schedule_index = scheduled.schedule_index,
            .pairing_index = scheduled.pairing_index,
            .game_seed = scheduled.seed,
            .starting_player = scheduled.starting_player,
            .seat_decks = scheduled.seat_decks,
            .actor = actor,
            .trace_ordinal = 0,
            .nontrivial_ordinal = 0,
            .actor_game_nontrivial_roots = 1,
            .retained_position = 0,
            .actor_game_retained_roots = 1,
            .search_seed = aq::root_search_seed(
                split, scheduled.schedule_index, actor, 0),
        },
        .stable_root_id = synthetic_identity(
            split, scheduled.schedule_index, actor, 'r'),
        .information_action_fingerprint =
            synthetic_identity(
                split, scheduled.schedule_index, actor, 'a'),
        .actions = std::move(actions),
        .action_descriptors = std::move(descriptors),
        .options = std::move(options),
    };
}

aq::Census make_valid_census() {
    std::array<aq::SplitCensus, 2> splits{
        aq::SplitCensus{
            .split = aq::Split::Train,
            .games = aq::kGamesPerSplit,
        },
        aq::SplitCensus{
            .split = aq::Split::Dev,
            .games = aq::kGamesPerSplit,
        },
    };
    std::vector<aq::ManifestRoot> roots;
    roots.reserve(
        2 * aq::kActorGamesPerSplit);

    for (const aq::Split split :
         {aq::Split::Train, aq::Split::Dev}) {
        const std::size_t index = aq::split_index(split);
        const std::size_t block =
            split == aq::Split::Train
                ? aq::kTrainBlock
                : aq::kDevBlock;
        const auto schedule =
            iteration::balanced_schedule(
                aq::kCollectionRootSeed,
                aq::kScheduleGeneration, block);
        for (const auto& scheduled : schedule) {
            for (std::size_t actor = 0; actor < 2;
                 ++actor) {
                const old_school::DeckId owner =
                    scheduled.seat_decks[actor];
                splits[index].actor_games.push_back({
                    .split = split,
                    .schedule_index =
                        scheduled.schedule_index,
                    .actor = actor,
                    .owner_deck = owner,
                    .nontrivial_roots = 1,
                    .retained_roots = 1,
                    .retained_options = 2,
                });
                auto& deck =
                    splits[index].decks[deck_index(owner)];
                ++deck.actor_games;
                ++deck.nontrivial_roots;
                ++deck.retained_roots;
                deck.retained_options += 2;
                roots.push_back(
                    synthetic_root(
                        split, scheduled, actor));
            }
        }
    }
    return aq::testing::make_census(
        std::string(aq::kRequiredParentFingerprint),
        std::move(splits), std::move(roots));
}

aq::RootExample synthetic_example(
    const aq::ManifestRoot& manifest) {
    const std::size_t actions = manifest.actions.size();
    std::vector<double> base_scores(actions, 0.35);
    std::vector<double> teacher_scores(actions, 0.40);
    for (std::size_t action = 0;
         action < actions; ++action) {
        base_scores[action] +=
            0.02 * static_cast<double>(action);
        teacher_scores[action] +=
            0.04 * static_cast<double>(action);
    }
    return {
        .manifest = manifest,
        .base_scores = std::move(base_scores),
        .teacher_scores = teacher_scores,
        .target_probabilities =
            old_school::learned_soft_priority_target(
                teacher_scores),
        .accounting = {
            .base_sampled_worlds = aq::kBaseWorlds,
            .base_rollout_evaluations =
                actions * aq::kBaseWorlds,
            .base_terminal_evaluations =
                actions * aq::kBaseWorlds / 2,
            .base_bootstrapped_evaluations =
                actions * aq::kBaseWorlds -
                actions * aq::kBaseWorlds / 2,
            .teacher_sampled_worlds =
                aq::kTeacherWorlds,
            .teacher_rollout_evaluations =
                actions * aq::kTeacherWorlds,
            .teacher_terminal_evaluations =
                actions * aq::kTeacherWorlds / 2,
            .teacher_bootstrapped_evaluations =
                actions * aq::kTeacherWorlds -
                actions * aq::kTeacherWorlds / 2,
            .teacher_inner_rollout_evaluations =
                aq::kInnerWorlds,
            .teacher_inner_search_invocations = 1,
            .teacher_inner_search_max_depth = 1,
        },
        .weight = aq::root_weight(
            manifest.coordinate.actor_game_retained_roots),
    };
}

aq::Corpus make_valid_corpus() {
    aq::Corpus corpus;
    corpus.census = make_valid_census();
    corpus.parent_components = {
        .critic = std::string(64, '1'),
        .priority = std::string(64, '2'),
        .attack = std::string(64, '3'),
        .block = std::string(64, '4'),
        .damage_order = std::string(64, '5'),
    };
    for (const auto& root : corpus.census.roots) {
        auto example = synthetic_example(root);
        if (root.coordinate.split == aq::Split::Train) {
            corpus.train.push_back(std::move(example));
        } else {
            corpus.dev.push_back(std::move(example));
        }
    }
    corpus.digest = aq::canonical_corpus_digest(corpus);
    return corpus;
}

old_school::BotBenchmarkSummary selector_summary(
    const std::array<std::size_t, old_school::kDeckCount>&
        wins) {
    old_school::BotBenchmarkSummary result;
    result.evaluation_seed = aq::kSelectorSeed;
    result.learned_training_seed = 424242;
    result.repetitions_per_deck_pairing =
        aq::g1::kSelectorRepetitions;
    result.total_games = aq::g1::kExpectedSelectorGames;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        auto& challenger = result.challenger_decks[deck];
        auto& baseline = result.baseline_decks[deck];
        challenger.games =
            aq::g1::kExpectedSelectorGamesPerDeck;
        challenger.wins = wins[deck];
        challenger.losses =
            challenger.games - challenger.wins;
        baseline.games = challenger.games;
        baseline.wins = challenger.losses;
        baseline.losses = challenger.wins;
        result.challenger_stats.wins += challenger.wins;
        result.challenger_stats.losses +=
            challenger.losses;
    }
    result.challenger_stats.games =
        aq::g1::kExpectedSelectorGames;
    result.baseline_stats.games =
        aq::g1::kExpectedSelectorGames;
    result.baseline_stats.wins =
        result.challenger_stats.losses;
    result.baseline_stats.losses =
        result.challenger_stats.wins;
    return result;
}

void test_cli_recipe_and_search_constants_are_sealed() {
    const std::vector<std::string_view> preflight{
        "--preflight"};
    const std::vector<std::string_view> census{"--census"};
    const std::vector<std::string_view> run{"--run"};
    const std::vector<std::string_view> empty;
    const std::vector<std::string_view> extra{
        "--census",
        "--run",
    };
    expect(
        aq::parse_command(preflight) ==
                aq::Command::Preflight &&
            aq::parse_command(census) ==
                aq::Command::Census &&
            aq::parse_command(run) == aq::Command::Run &&
            !aq::parse_command(empty).has_value() &&
            !aq::parse_command(extra).has_value(),
        "AQ4-G4B CLI accepted an undeclared shape");
    std::ostringstream usage;
    aq::print_usage(usage);
    expect(
        usage.str() ==
            "Usage: old-school-action-q-broad-distill "
            "(--preflight|--census|--run)\n",
        "AQ4-G4B usage drifted");

    const auto recipe = aq::preflight_recipe();
    const auto g1_recipe = aq::g1::preflight_recipe();
    const auto base = aq::base_search_config(123);
    const auto teacher = aq::teacher_search_config(456);
    const auto optimizer = aq::optimizer_config();
    old_school::action_q_nested_actor_diagnostic::
        PreflightReport empty_preflight;
    empty_preflight.recipe = recipe;
    expect(
        aq::kCollectionRootSeed == 202607282301ULL &&
            aq::kSelectorSeed == 202607282311ULL &&
            aq::kScheduleGeneration == 0 &&
            aq::kTrainBlock == 0 &&
            aq::kDevBlock == 1 &&
            aq::kSourceTurnCap == 128 &&
            aq::kTrainMaximumRootsPerActorGame == 6 &&
            aq::kDevMaximumRootsPerActorGame == 2 &&
            aq::kTrainRootCeiling == 480 &&
            aq::kDevRootCeiling == 160 &&
            aq::kActorGamesPerDeckAndSplit == 16 &&
            aq::kFrozenPreflightDigest ==
                "8a5800dc3ebd7cfad3c8cc893e3aa7e5795f38cb63fcf10442f7bb6588fd950d" &&
            aq::kFrozenCensusManifestHash.empty() &&
            recipe == g1_recipe &&
            base.seed == 123 &&
            base.worlds == 8 &&
            base.rollouts_per_world == 1 &&
            base.horizon_turns == 4 &&
            base.blend_shallow_prior &&
            base.value_priority_residual_weight == 0.0 &&
            base.value_continuation_search_worlds == 0 &&
            teacher.seed == 456 &&
            teacher.worlds == 8 &&
            teacher.rollouts_per_world == 1 &&
            teacher.horizon_turns == 8 &&
            teacher.evaluation_threads == 4 &&
            !teacher.blend_shallow_prior &&
            teacher.value_priority_residual_weight == 0.0 &&
            teacher.value_continuation_search_worlds == 2 &&
            optimizer.batch_size == 64 &&
            optimizer.epochs == 64 &&
            optimizer.learning_rate == 0.003 &&
            optimizer.seed == aq::g1::kFitSeed &&
            optimizer.residual_weight == 0.10 &&
            optimizer.policy_temperature == 0.10 &&
            !aq::preflight_exact(empty_preflight),
        "AQ4-G4B recipe or search constants drifted");

    expect(
        aq::split_index(aq::Split::Train) == 0 &&
            aq::split_index(aq::Split::Dev) == 1 &&
            aq::maximum_roots_per_actor_game(
                aq::Split::Train) == 6 &&
            aq::maximum_roots_per_actor_game(
                aq::Split::Dev) == 2,
        "AQ4-G4B split or retention-cap mapping drifted");
    expect_rejected(
        [] {
            static_cast<void>(
                aq::split_index(
                    static_cast<aq::Split>(2)));
        },
        "AQ4-G4B accepted an invalid split");
}

void test_preflight_digest_is_canonical_and_fail_closed() {
    old_school::action_q_nested_actor_diagnostic::
        PreflightReport report;
    report.recipe = aq::preflight_recipe();
    report.evidence.parent_fingerprint =
        std::string(aq::kRequiredParentFingerprint);

    old_school::action_q_nested_actor_diagnostic::
        FixtureReport fixture;
    fixture.seed = 123;
    fixture.spec.stable_id = "synthetic.fixture";
    fixture.score.accounting = {
        .sampled_worlds = 1,
        .rollout_evaluations = 1,
        .terminal_evaluations = 0,
        .bootstrapped_evaluations = 1,
        .inner_rollout_evaluations = 2,
        .inner_search_invocations = 1,
        .inner_search_max_depth = 1,
    };
    old_school::action_q_nested_actor_diagnostic::
        ActionScore action;
    action.probe_key = "synthetic.action";
    action.typed_descriptor = "Pass";
    action.action =
        old_school::PriorityAction::pass();
    action.samples = {0.25};
    action.inner_rollout_evaluations = {2};
    action.inner_search_invocations = {1};
    action.inner_search_max_depth = {1};
    action.mean = 0.25;
    action.exact_max = true;
    fixture.score.actions.push_back(std::move(action));
    fixture.score.selected_probe_key =
        "synthetic.action";
    fixture.direction.passed = true;
    fixture.direction.required_margin = 0.01;
    fixture.direction.positive_value = 0.25;
    fixture.direction.negative_value = 0.20;
    fixture.hidden_repartition_nonvacuous = true;
    fixture.hidden_repartition_bit_identical = true;
    fixture.reversed_action_bit_identical = true;
    report.evidence.fixtures.push_back(
        std::move(fixture));
    report.evidence.direction_passed.fill(true);
    report.evidence.actor_local.seed = 456;
    report.evidence.actor_local
        .hidden_repartition_nonvacuous = true;
    report.evidence.actor_local
        .observation_bit_identical = true;
    report.evidence.actor_local
        .legal_actions_bit_identical = true;
    report.evidence.actor_local
        .score_bit_identical = true;
    report.evidence.actor_local
        .one_level_nesting_bounded = true;
    report.evidence.hypothesis_passed = true;

    const std::string digest =
        aq::canonical_preflight_digest(report);
    expect(
        digest.size() == 64 &&
            digest ==
                aq::canonical_preflight_digest(report) &&
            !aq::preflight_exact(report),
        "AQ4-G4B unrelated preflight digest authorized later stages");

    auto changed_recipe = report;
    ++changed_recipe.recipe.root_seed;
    expect(
        aq::canonical_preflight_digest(changed_recipe) !=
            digest,
        "AQ4-G4B preflight digest omitted its recipe");

    auto changed_parent = report;
    changed_parent.evidence.parent_fingerprint +=
        "/changed";
    expect(
        aq::canonical_preflight_digest(changed_parent) !=
            digest,
        "AQ4-G4B preflight digest omitted its parent");

    auto changed_sample = report;
    changed_sample.evidence.fixtures.front()
        .score.actions.front().samples.front() =
        std::nextafter(0.25, 1.0);
    expect(
        aq::canonical_preflight_digest(changed_sample) !=
            digest,
        "AQ4-G4B preflight digest omitted sample bits");

    auto changed_hidden = report;
    changed_hidden.evidence.fixtures.front()
        .hidden_repartition_bit_identical = false;
    expect(
        aq::canonical_preflight_digest(changed_hidden) !=
            digest,
        "AQ4-G4B preflight digest omitted hidden-safety "
        "evidence");
}

void test_schedules_are_balanced_and_game_disjoint() {
    const auto train =
        iteration::balanced_schedule(
            aq::kCollectionRootSeed,
            aq::kScheduleGeneration, aq::kTrainBlock);
    const auto dev =
        iteration::balanced_schedule(
            aq::kCollectionRootSeed,
            aq::kScheduleGeneration, aq::kDevBlock);
    expect(
        train.size() == aq::kGamesPerSplit &&
            dev.size() == aq::kGamesPerSplit &&
            train.size() == 40 &&
            dev.size() == 40,
        "AQ4-G4B schedule size drifted");

    const auto validate_split =
        [](const auto& schedule,
           std::string_view label) {
            std::array<std::size_t, old_school::kDeckCount>
                actor_games{};
            std::array<std::size_t, old_school::kDeckCount>
                seat_zero_games{};
            std::array<std::size_t, old_school::kDeckCount>
                starting_games{};
            std::array<std::size_t,
                       iteration::kBalancedPairings>
                pairing_games{};
            std::set<std::uint64_t> seeds;
            for (std::size_t index = 0;
                 index < schedule.size(); ++index) {
                const auto& game = schedule[index];
                expect(
                    game.schedule_index == index &&
                        game.pairing_index <
                            pairing_games.size() &&
                        game.seat_decks[0] !=
                            game.seat_decks[1] &&
                        game.starting_player < 2 &&
                        seeds.insert(game.seed).second,
                    std::string(label) +
                        " schedule is malformed");
                ++pairing_games[game.pairing_index];
                for (std::size_t actor = 0; actor < 2;
                     ++actor) {
                    const std::size_t deck =
                        deck_index(
                            game.seat_decks[actor]);
                    ++actor_games[deck];
                    if (actor == 0) {
                        ++seat_zero_games[deck];
                    }
                    if (game.starting_player == actor) {
                        ++starting_games[deck];
                    }
                }
            }
            for (const std::size_t games :
                 pairing_games) {
                expect(
                    games ==
                        iteration::
                            kBalancedGamesPerPairing,
                    std::string(label) +
                        " pairing count drifted");
            }
            for (std::size_t deck = 0;
                 deck < old_school::kDeckCount; ++deck) {
                expect(
                    actor_games[deck] == 16 &&
                        seat_zero_games[deck] == 8 &&
                        starting_games[deck] == 8,
                    std::string(label) +
                        " five-deck balance drifted");
            }
            return seeds;
        };
    const auto train_seeds =
        validate_split(train, "TRAIN");
    const auto dev_seeds =
        validate_split(dev, "DEV");
    std::vector<std::uint64_t> collisions;
    std::set_intersection(
        train_seeds.begin(), train_seeds.end(),
        dev_seeds.begin(), dev_seeds.end(),
        std::back_inserter(collisions));
    expect(
        collisions.empty(),
        "AQ4-G4B TRAIN and DEV share a physical game seed");

    for (const aq::Split split :
         {aq::Split::Train, aq::Split::Dev}) {
        for (const std::size_t schedule_index :
             {0U, 17U, 39U}) {
            for (const std::size_t actor : {0U, 1U}) {
                for (const std::size_t ordinal :
                     {0U, 5U, 19U}) {
                    expect(
                        aq::root_search_seed(
                            split, schedule_index,
                            actor, ordinal) ==
                            iteration::derive_seed(
                                aq::kCollectionRootSeed,
                                iteration::SeedDomain::
                                    PrioritySearch,
                                aq::split_index(split),
                                schedule_index,
                                (actor << 32) | ordinal),
                        "AQ4-G4B root-search seed left "
                        "its declared domain");
                }
            }
        }
    }
}

void test_census_is_owner_safe_balanced_and_collision_closed() {
    const aq::Census census = make_valid_census();
    aq::validate_census(census);
    expect(
        census.games() == 80 &&
            census.roots.size() == 160 &&
            census.manifest_hash ==
                aq::canonical_manifest_hash(census) &&
            census == make_valid_census(),
        "AQ4-G4B synthetic census is not canonical");
    expect_rejected(
        [&] {
            aq::require_frozen_census(census);
        },
        "AQ4-G4B run authorized an unfrozen census");

    for (const aq::Split split :
         {aq::Split::Train, aq::Split::Dev}) {
        const auto& split_census =
            census.splits[aq::split_index(split)];
        expect(
            split_census.split == split &&
                split_census.games == 40 &&
                split_census.actor_games.size() == 80 &&
                split_census.retained_roots() == 80 &&
                split_census.retained_options() == 160,
            "AQ4-G4B split census drifted");
        for (const auto& deck : split_census.decks) {
            expect(
                deck.actor_games == 16 &&
                    deck.nontrivial_roots == 16 &&
                    deck.retained_roots == 16 &&
                    deck.retained_options == 32,
                "AQ4-G4B deck census is not balanced");
        }
    }

    for (const std::size_t retained :
         {1U, 2U, 3U, 6U}) {
        const double weight = aq::root_weight(retained);
        expect(
            weight ==
                    1.0 /
                        (16.0 *
                         static_cast<double>(retained)) &&
                static_cast<double>(retained) *
                        weight ==
                    1.0 / 16.0,
            "AQ4-G4B actor-game root mass drifted");
    }
    expect_rejected(
        [] {
            static_cast<void>(aq::root_weight(0));
        },
        "AQ4-G4B weighted a rootless actor-game");

    std::array<
        std::array<double, old_school::kDeckCount>, 2>
        deck_mass{};
    for (const auto& root : census.roots) {
        deck_mass[aq::split_index(
                      root.coordinate.split)]
                 [deck_index(
                     root.coordinate.owner_deck())] +=
            aq::root_weight(
                root.coordinate
                    .actor_game_retained_roots);
    }
    for (const auto& split_mass : deck_mass) {
        for (const double mass : split_mass) {
            expect(
                mass == 1.0,
                "AQ4-G4B deck loss mass is not one");
        }
    }

    aq::Census collision = census;
    const auto dev_root = std::find_if(
        collision.roots.begin(), collision.roots.end(),
        [](const aq::ManifestRoot& root) {
            return root.coordinate.split ==
                aq::Split::Dev;
        });
    expect(
        dev_root != collision.roots.end(),
        "AQ4-G4B synthetic DEV root is missing");
    dev_root->stable_root_id =
        collision.roots.front().stable_root_id;
    collision.manifest_hash =
        aq::canonical_manifest_hash(collision);
    expect_rejected(
        [&] {
            aq::validate_census(collision);
        },
        "AQ4-G4B accepted a TRAIN/DEV root collision");

    aq::Census wrong_game_census = census;
    auto wrong_game = std::find_if(
        wrong_game_census.roots.begin(),
        wrong_game_census.roots.end(),
        [](const aq::ManifestRoot& root) {
            return root.coordinate.split ==
                aq::Split::Dev;
        });
    wrong_game->coordinate.game_seed =
        census.roots.front().coordinate.game_seed;
    wrong_game_census.manifest_hash =
        aq::canonical_manifest_hash(
            wrong_game_census);
    expect_rejected(
        [&] {
            aq::validate_census(wrong_game_census);
        },
        "AQ4-G4B accepted a cross-split game collision");

    aq::Census stale_hash = census;
    stale_hash.roots.front()
        .information_action_fingerprint += "/changed";
    expect_rejected(
        [&] {
            aq::validate_census(stale_hash);
        },
        "AQ4-G4B accepted a stale manifest hash");
}

void test_corpus_targets_accounting_and_train_dev_boundary() {
    const aq::Corpus corpus = make_valid_corpus();
    aq::validate_corpus(corpus);
    expect(
        corpus.train.size() == 80 &&
            corpus.dev.size() == 80 &&
            corpus.digest ==
                aq::canonical_corpus_digest(corpus),
        "AQ4-G4B synthetic corpus is not canonical");
    aq::Corpus changed_parent_component = corpus;
    changed_parent_component.parent_components.priority[0] =
        '9';
    expect(
        aq::canonical_corpus_digest(
            changed_parent_component) != corpus.digest,
        "AQ4-G4B corpus digest omitted parent "
        "components");

    const auto validate_examples =
        [](const std::vector<aq::RootExample>& examples,
           aq::Split split) {
            std::array<double, old_school::kDeckCount>
                deck_mass{};
            for (const auto& example : examples) {
                const std::size_t actions =
                    example.manifest.actions.size();
                expect(
                    example.manifest.coordinate.split ==
                            split &&
                        example.base_scores.size() ==
                            actions &&
                        example.teacher_scores.size() ==
                            actions &&
                        example.target_probabilities ==
                            old_school::
                                learned_soft_priority_target(
                                    example
                                        .teacher_scores) &&
                        example.accounting
                                .base_sampled_worlds ==
                            aq::kBaseWorlds &&
                        example.accounting
                                .base_rollout_evaluations ==
                            actions * aq::kBaseWorlds &&
                        example.accounting
                                    .base_terminal_evaluations +
                                example.accounting
                                    .base_bootstrapped_evaluations ==
                            example.accounting
                                .base_rollout_evaluations &&
                        example.accounting
                                .teacher_sampled_worlds ==
                            aq::kTeacherWorlds &&
                        example.accounting
                                .teacher_rollout_evaluations ==
                            actions * aq::kTeacherWorlds &&
                        example.accounting
                                    .teacher_terminal_evaluations +
                                example.accounting
                                    .teacher_bootstrapped_evaluations ==
                            example.accounting
                                .teacher_rollout_evaluations &&
                        example.accounting
                                .teacher_inner_search_invocations >
                            0 &&
                        example.accounting
                                .teacher_inner_rollout_evaluations %
                                aq::kInnerWorlds ==
                            0 &&
                        example.accounting
                                .teacher_inner_search_max_depth ==
                            1,
                    "AQ4-G4B root target/accounting shape "
                    "drifted");
                const double target_sum =
                    std::accumulate(
                        example.target_probabilities.begin(),
                        example.target_probabilities.end(),
                        0.0);
                expect(
                    std::abs(target_sum - 1.0) < 1.0e-12 &&
                        std::all_of(
                            example.target_probabilities
                                .begin(),
                            example.target_probabilities.end(),
                            [](double value) {
                                return std::isfinite(value) &&
                                    value > 0.0;
                            }),
                    "AQ4-G4B target is not finite, positive, "
                    "and normalized");
                deck_mass[deck_index(
                    example.manifest.coordinate
                        .owner_deck())] +=
                    example.weight;
            }
            for (const double mass : deck_mass) {
                expect(
                    mass == 1.0,
                    "AQ4-G4B corpus deck mass drifted");
            }
        };
    validate_examples(corpus.train, aq::Split::Train);
    validate_examples(corpus.dev, aq::Split::Dev);

    const auto same_training_examples =
        [](const auto& first, const auto& second) {
            if (first.size() != second.size()) {
                return false;
            }
            for (std::size_t index = 0;
                 index < first.size(); ++index) {
                if (first[index].options !=
                        second[index].options ||
                    first[index].base_scores !=
                        second[index].base_scores ||
                    first[index].target_probabilities !=
                        second[index].target_probabilities ||
                    first[index].weight !=
                        second[index].weight) {
                    return false;
                }
            }
            return true;
        };
    const auto projected =
        aq::testing::training_examples(corpus);
    expect(
        projected.size() == corpus.train.size(),
        "AQ4-G4B fit projection count is not TRAIN-only");
    for (std::size_t index = 0;
         index < projected.size(); ++index) {
        expect(
            projected[index].options ==
                    corpus.train[index].manifest.options &&
                projected[index].base_scores ==
                    corpus.train[index].base_scores &&
                projected[index].target_probabilities ==
                    corpus.train[index]
                        .target_probabilities &&
                projected[index].weight ==
                    corpus.train[index].weight,
            "AQ4-G4B fit projection changed a TRAIN row");
    }

    aq::Corpus changed_dev = corpus;
    changed_dev.dev.front().teacher_scores.front() += 0.01;
    changed_dev.dev.front().target_probabilities =
        old_school::learned_soft_priority_target(
            changed_dev.dev.front().teacher_scores);
    changed_dev.digest =
        aq::canonical_corpus_digest(changed_dev);
    const auto projected_after_dev_change =
        aq::testing::training_examples(changed_dev);
    expect(
        same_training_examples(
            projected, projected_after_dev_change),
        "AQ4-G4B DEV data entered the fit projection");

    aq::Corpus changed_train = corpus;
    changed_train.train.front().teacher_scores.front() +=
        0.01;
    changed_train.train.front().target_probabilities =
        old_school::learned_soft_priority_target(
            changed_train.train.front().teacher_scores);
    changed_train.digest =
        aq::canonical_corpus_digest(changed_train);
    const auto projected_after_train_change =
        aq::testing::training_examples(changed_train);
    expect(
        !same_training_examples(
            projected, projected_after_train_change),
        "AQ4-G4B TRAIN data did not enter the fit "
        "projection");

    aq::Corpus bad_target = corpus;
    bad_target.train.front().target_probabilities = {
        0.5, 0.5};
    bad_target.digest =
        aq::canonical_corpus_digest(bad_target);
    expect_rejected(
        [&] {
            aq::validate_corpus(bad_target);
        },
        "AQ4-G4B accepted a target detached from teacher "
        "scores");

    aq::Corpus bad_accounting = corpus;
    --bad_accounting.train.front()
          .accounting.base_rollout_evaluations;
    bad_accounting.digest =
        aq::canonical_corpus_digest(bad_accounting);
    expect_rejected(
        [&] {
            aq::validate_corpus(bad_accounting);
        },
        "AQ4-G4B accepted invalid rollout accounting");

    aq::Corpus bad_weight = corpus;
    bad_weight.train.front().weight *= 2.0;
    bad_weight.digest =
        aq::canonical_corpus_digest(bad_weight);
    expect_rejected(
        [&] {
            aq::validate_corpus(bad_weight);
        },
        "AQ4-G4B accepted actor-game/deck mass drift");

    aq::Corpus dev_in_train = corpus;
    dev_in_train.train.push_back(dev_in_train.dev.front());
    dev_in_train.digest =
        aq::canonical_corpus_digest(dev_in_train);
    expect_rejected(
        [&] {
            aq::validate_corpus(dev_in_train);
        },
        "AQ4-G4B admitted a DEV row at the TRAIN boundary");

    aq::Corpus stale_digest = corpus;
    stale_digest.dev.front().teacher_scores.front() += 0.01;
    expect_rejected(
        [&] {
            aq::validate_corpus(stale_digest);
        },
        "AQ4-G4B accepted a stale corpus digest");
}

aq::OfflineReport passing_offline_report() {
    aq::OfflineReport report;
    report.corpus_digest = "synthetic-corpus";
    report.parent_fingerprint =
        std::string(aq::kRequiredParentFingerprint);
    report.candidate_fingerprint = std::string(64, 'c');
    report.census_frozen = true;
    report.corpus_digest_exact = true;
    report.preflight_exact = true;
    report.parent_immutable = true;
    report.repeated_fit_bit_identical = true;
    report.only_priority_component_changed = true;
    report.train_regret_strictly_improved = true;
    report.dev_regret_strictly_improved = true;
    report.dev_deck_regret_guard.fill(true);
    report.parent_train_signal_nonzero.fill(true);
    report.parent_dev_signal_nonzero.fill(true);
    report.targets_finite_and_normalized = true;
    report.descriptor_order_identity = true;
    report.redundant_counter_pass = true;
    report.braingeyser_productive = true;
    report.sick_bear_growth_pass = true;
    report.live_force_spike = true;
    report.ancestral_pass = true;
    return report;
}

void test_offline_gate_and_selector_are_fail_closed() {
    const aq::OfflineReport passing =
        passing_offline_report();
    expect(
        passing.gate_passed(),
        "AQ4-G4B rejected the exact offline conjunction");

    const auto rejects =
        [&](auto mutate, std::string_view message) {
            aq::OfflineReport report = passing;
            mutate(report);
            expect(!report.gate_passed(), message);
        };
    rejects(
        [](aq::OfflineReport& report) {
            report.census_frozen = false;
        },
        "AQ4-G4B omitted the frozen-census gate");
    rejects(
        [](aq::OfflineReport& report) {
            report.corpus_digest_exact = false;
        },
        "AQ4-G4B omitted corpus authentication");
    rejects(
        [](aq::OfflineReport& report) {
            report.preflight_exact = false;
        },
        "AQ4-G4B omitted exact preflight evidence");
    rejects(
        [](aq::OfflineReport& report) {
            report.parent_immutable = false;
        },
        "AQ4-G4B omitted parent immutability");
    rejects(
        [](aq::OfflineReport& report) {
            report.repeated_fit_bit_identical = false;
        },
        "AQ4-G4B omitted repeat-fit identity");
    rejects(
        [](aq::OfflineReport& report) {
            report.only_priority_component_changed = false;
        },
        "AQ4-G4B omitted component isolation");
    rejects(
        [](aq::OfflineReport& report) {
            report.train_regret_strictly_improved = false;
        },
        "AQ4-G4B omitted TRAIN improvement");
    rejects(
        [](aq::OfflineReport& report) {
            report.dev_regret_strictly_improved = false;
        },
        "AQ4-G4B omitted DEV improvement");
    rejects(
        [](aq::OfflineReport& report) {
            report.dev_deck_regret_guard[4] = false;
        },
        "AQ4-G4B omitted a DEV deck guard");
    rejects(
        [](aq::OfflineReport& report) {
            report.parent_train_signal_nonzero[0] = false;
        },
        "AQ4-G4B accepted a signal-free TRAIN deck");
    rejects(
        [](aq::OfflineReport& report) {
            report.parent_dev_signal_nonzero[1] = false;
        },
        "AQ4-G4B accepted a signal-free DEV deck");
    rejects(
        [](aq::OfflineReport& report) {
            report.targets_finite_and_normalized = false;
        },
        "AQ4-G4B omitted target validity");
    rejects(
        [](aq::OfflineReport& report) {
            report.descriptor_order_identity = false;
        },
        "AQ4-G4B omitted descriptor/order identity");
    rejects(
        [](aq::OfflineReport& report) {
            report.redundant_counter_pass = false;
        },
        "AQ4-G4B omitted redundant Counterspell");
    rejects(
        [](aq::OfflineReport& report) {
            report.braingeyser_productive = false;
        },
        "AQ4-G4B omitted productive Braingeyser");
    rejects(
        [](aq::OfflineReport& report) {
            report.sick_bear_growth_pass = false;
        },
        "AQ4-G4B omitted sick-Bear Growth");
    rejects(
        [](aq::OfflineReport& report) {
            report.live_force_spike = false;
        },
        "AQ4-G4B omitted live Force Spike");
    rejects(
        [](aq::OfflineReport& report) {
            report.ancestral_pass = false;
        },
        "AQ4-G4B omitted complete Ancestral");
    rejects(
        [](aq::OfflineReport& report) {
            report.failures.emplace_back("forged");
        },
        "AQ4-G4B ignored an explicit failure");

    expect(
        aq::classify_selector(
            selector_summary({8, 8, 7, 7, 7})) ==
                aq::SelectorDisposition::FastGo &&
            aq::classify_selector(
                selector_summary({7, 6, 6, 6, 6})) ==
                aq::SelectorDisposition::ManualOnly &&
            aq::classify_selector(
                selector_summary({6, 6, 6, 6, 6})) ==
                aq::SelectorDisposition::Reject &&
            aq::classify_selector(
                selector_summary({2, 9, 9, 9, 8})) ==
                aq::SelectorDisposition::Reject,
        "AQ4-G4B selector thresholds are not conjunctive");

    aq::FitReport forged_fit;
    forged_fit.corpus_digest =
        passing.corpus_digest;
    forged_fit.parent_immutable = true;
    forged_fit.repeated_fit_bit_identical = true;
    forged_fit.only_priority_component_changed = true;
    const aq::Corpus corpus = make_valid_corpus();
    old_school::action_q_nested_actor_diagnostic::
        PreflightReport preflight;
    preflight.recipe = aq::preflight_recipe();
    preflight.evidence.parent_fingerprint =
        std::string(aq::kRequiredParentFingerprint);
    expect_rejected(
        [&] {
            static_cast<void>(
                aq::run_selector(
                    corpus, preflight,
                    std::shared_ptr<
                        const old_school::LearnedModel>{},
                    std::shared_ptr<
                        const old_school::LearnedModel>{},
                    forged_fit, passing));
        },
        "AQ4-G4B selector trusted forged passing flags "
        "without a bound candidate");

    aq::OfflineReport recomputed = passing;
    recomputed.corpus_digest = corpus.digest;
    recomputed.parent_fingerprint =
        std::string(aq::kRequiredParentFingerprint);
    recomputed.candidate_fingerprint =
        std::string(64, 'c');
    aq::FitReport bound_fit;
    bound_fit.corpus_digest = corpus.digest;
    bound_fit.parent_fingerprint_before =
        recomputed.parent_fingerprint;
    bound_fit.parent_fingerprint_after =
        recomputed.parent_fingerprint;
    bound_fit.candidate_fingerprint =
        recomputed.candidate_fingerprint;
    bound_fit.parent_components =
        corpus.parent_components;
    bound_fit.optimizer = aq::optimizer_config();
    bound_fit.fit_examples = corpus.train.size();
    bound_fit.fit_options =
        std::accumulate(
            corpus.train.begin(), corpus.train.end(),
            std::size_t{0},
            [](std::size_t total,
               const aq::RootExample& example) {
                return total +
                    example.manifest.actions.size();
            });
    bound_fit.parent_train =
        recomputed.parent_train;
    bound_fit.candidate_train =
        recomputed.candidate_train;
    bound_fit.parent_dev = recomputed.parent_dev;
    bound_fit.candidate_dev =
        recomputed.candidate_dev;
    const std::string preflight_digest =
        aq::canonical_preflight_digest(preflight);
    const auto binding_exact =
        [&](const aq::Corpus& checked_corpus,
            const auto& checked_preflight,
            const aq::FitReport& checked_fit,
            const aq::OfflineReport& supplied,
            const aq::OfflineReport& checked_recomputed,
            std::string_view parent_fingerprint,
            std::string_view candidate_fingerprint,
            std::string_view required_preflight,
            std::string_view required_census) {
            return aq::testing::selector_binding_exact(
                checked_corpus, checked_preflight,
                checked_fit, supplied,
                checked_recomputed, parent_fingerprint,
                candidate_fingerprint,
                required_preflight, required_census);
        };
    expect(
        binding_exact(
            corpus, preflight, bound_fit, recomputed,
            recomputed, recomputed.parent_fingerprint,
            recomputed.candidate_fingerprint,
            preflight_digest,
            corpus.census.manifest_hash),
        "AQ4-G4B rejected an exact synthetic selector "
        "binding");

    aq::OfflineReport forged_report = recomputed;
    forged_report.braingeyser_productive = false;
    expect(
        !binding_exact(
            corpus, preflight, bound_fit,
            forged_report, recomputed,
            recomputed.parent_fingerprint,
            recomputed.candidate_fingerprint,
            preflight_digest,
            corpus.census.manifest_hash),
        "AQ4-G4B selector trusted a forged offline "
        "report");

    aq::Corpus stale_corpus = corpus;
    stale_corpus.digest += "/stale";
    expect(
        !binding_exact(
            stale_corpus, preflight, bound_fit,
            recomputed, recomputed,
            recomputed.parent_fingerprint,
            recomputed.candidate_fingerprint,
            preflight_digest,
            corpus.census.manifest_hash),
        "AQ4-G4B selector trusted a stale corpus");

    auto stale_preflight = preflight;
    ++stale_preflight.recipe.root_seed;
    expect(
        !binding_exact(
            corpus, stale_preflight, bound_fit,
            recomputed, recomputed,
            recomputed.parent_fingerprint,
            recomputed.candidate_fingerprint,
            preflight_digest,
            corpus.census.manifest_hash),
        "AQ4-G4B selector trusted a stale preflight");

    aq::FitReport stale_fit = bound_fit;
    stale_fit.optimizer.epochs += 1;
    expect(
        !binding_exact(
            corpus, preflight, stale_fit,
            recomputed, recomputed,
            recomputed.parent_fingerprint,
            recomputed.candidate_fingerprint,
            preflight_digest,
            corpus.census.manifest_hash),
        "AQ4-G4B selector trusted optimizer drift");

    aq::OfflineReport stale_recomputed = recomputed;
    stale_recomputed.candidate_fingerprint =
        std::string(64, 'd');
    expect(
        !binding_exact(
            corpus, preflight, bound_fit,
            recomputed, stale_recomputed,
            recomputed.parent_fingerprint,
            recomputed.candidate_fingerprint,
            preflight_digest,
            corpus.census.manifest_hash),
        "AQ4-G4B selector trusted stale recomputed "
        "evidence");
    expect(
        !binding_exact(
            corpus, preflight, bound_fit,
            recomputed, recomputed,
            recomputed.parent_fingerprint,
            recomputed.candidate_fingerprint,
            std::string_view{},
            corpus.census.manifest_hash) &&
            !binding_exact(
                corpus, preflight, bound_fit,
                recomputed, recomputed,
                recomputed.parent_fingerprint,
                recomputed.candidate_fingerprint,
                preflight_digest, std::string_view{}),
        "AQ4-G4B selector opened without both frozen "
        "digests");
}

} // namespace

int main() {
    std::size_t passed = 0;
    const auto run =
        [&passed](std::string_view name, auto test) {
            try {
                test();
                ++passed;
            } catch (const std::exception& error) {
                std::cerr
                    << "FAIL " << name << ": "
                    << error.what() << '\n';
                throw;
            }
        };
    run(
        "cli_recipe_and_search_constants_are_sealed",
        test_cli_recipe_and_search_constants_are_sealed);
    run(
        "preflight_digest_is_canonical_and_fail_closed",
        test_preflight_digest_is_canonical_and_fail_closed);
    run(
        "schedules_are_balanced_and_game_disjoint",
        test_schedules_are_balanced_and_game_disjoint);
    run(
        "census_is_owner_safe_balanced_and_collision_closed",
        test_census_is_owner_safe_balanced_and_collision_closed);
    run(
        "corpus_targets_accounting_and_train_dev_boundary",
        test_corpus_targets_accounting_and_train_dev_boundary);
    run(
        "offline_gate_and_selector_are_fail_closed",
        test_offline_gate_and_selector_are_fail_closed);
    std::cout << passed
              << " AQ4-G4B broad-distillation tests passed\n";
}
