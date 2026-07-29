#include "old_school/action_q_on_policy_successor.hpp"

#include "old_school/learned_iteration.hpp"
#include "old_school/probes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace op1 =
    old_school::action_q_on_policy_successor;
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
concept HasOutcomeMember = requires(T value) {
    value.outcome;
};

template <typename T>
concept HasWinnerMember = requires(T value) {
    value.winner;
};

template <typename T>
concept HasSampledWorldMember = requires(T value) {
    value.sampled_world;
};

static_assert(!HasStateMember<op1::ManifestRoot>);
static_assert(!HasGameStateMember<op1::ManifestRoot>);
static_assert(!HasOpponentHandMember<op1::ManifestRoot>);
static_assert(!HasOpponentLibraryMember<op1::ManifestRoot>);
static_assert(!HasOutcomeMember<op1::ManifestRoot>);
static_assert(!HasWinnerMember<op1::ManifestRoot>);
static_assert(!HasSampledWorldMember<op1::ManifestRoot>);
static_assert(!HasStateMember<op1::RootExample>);
static_assert(!HasOpponentHandMember<op1::RootExample>);
static_assert(!HasOpponentLibraryMember<op1::RootExample>);

std::size_t deck_index(old_school::DeckId deck) {
    return static_cast<std::size_t>(deck);
}

op1::ManifestRoot synthetic_root(
    op1::Split split,
    const iteration::ScheduledGame& scheduled,
    std::size_t actor) {
    std::vector<old_school::PriorityAction> actions{
        old_school::PriorityAction::pass(),
        old_school::PriorityAction::play_land(
            old_school::CardId::Forest),
    };
    std::vector<std::string> descriptors;
    std::vector<std::vector<double>> options;
    for (std::size_t option = 0;
         option < actions.size(); ++option) {
        descriptors.push_back(
            old_school::probes::
                stable_priority_action_descriptor(
                    actions[option]));
        std::vector<double> features(
            op1::kPolicyFeatureCount, 0.0);
        features[0] =
            static_cast<double>(
                deck_index(scheduled.seat_decks[actor]) + 1);
        features[1] =
            static_cast<double>(
                scheduled.schedule_index + 1);
        features[2] = static_cast<double>(actor + 1);
        features[3] = static_cast<double>(option + 1);
        features[4] =
            split == op1::Split::Train ? 1.0 : -1.0;
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
            .search_seed = op1::root_search_seed(
                split, scheduled.schedule_index,
                actor, 0),
        },
        .actions = std::move(actions),
        .action_descriptors = std::move(descriptors),
        .options = std::move(options),
    };
}

op1::Census make_valid_census() {
    std::array<op1::SplitCensus, 2> splits{
        op1::SplitCensus{
            .split = op1::Split::Train,
            .games = op1::kGamesPerSplit,
        },
        op1::SplitCensus{
            .split = op1::Split::Dev,
            .games = op1::kGamesPerSplit,
        },
    };
    std::vector<op1::ManifestRoot> roots;
    roots.reserve(2 * op1::kActorGamesPerSplit);

    for (const op1::Split split :
         {op1::Split::Train, op1::Split::Dev}) {
        const std::size_t index = op1::split_index(split);
        const std::size_t block =
            split == op1::Split::Train
                ? op1::kTrainBlock
                : op1::kDevBlock;
        const auto schedule =
            iteration::balanced_schedule(
                op1::kCollectionRootSeed,
                op1::kScheduleGeneration, block);
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
    return op1::testing::make_census(
        std::string(
            op1::kRequiredWarmParentFingerprint),
        std::move(splits), std::move(roots));
}

op1::RootExample synthetic_example(
    const op1::ManifestRoot& manifest) {
    const std::vector<double> base_scores{0.42, 0.47};
    const std::vector<double> teacher_scores{0.40, 0.61};
    return {
        .manifest = manifest,
        .base_scores = base_scores,
        .teacher_scores = teacher_scores,
        .target_probabilities =
            old_school::learned_soft_priority_target(
                teacher_scores),
        .accounting = {
            .base_sampled_worlds = op1::kBaseWorlds,
            .base_rollout_evaluations =
                2 * op1::kBaseWorlds,
            .base_terminal_evaluations = 0,
            .base_bootstrapped_evaluations =
                2 * op1::kBaseWorlds,
            .teacher_sampled_worlds =
                op1::kTeacherWorlds,
            .teacher_rollout_evaluations =
                2 * op1::kTeacherWorlds,
            .teacher_terminal_evaluations = 0,
            .teacher_bootstrapped_evaluations =
                2 * op1::kTeacherWorlds,
            .teacher_inner_rollout_evaluations =
                op1::kInnerWorlds,
            .teacher_inner_search_invocations = 1,
            .teacher_inner_search_max_depth = 1,
        },
        .weight = op1::root_weight(
            manifest.coordinate
                .actor_game_retained_roots),
    };
}

old_school::LearnedModelComponentFingerprints
synthetic_components(char priority = 'p') {
    return {
        .critic = std::string(64, 'c'),
        .priority = std::string(64, priority),
        .attack = std::string(64, 'a'),
        .block = std::string(64, 'b'),
        .damage_order = std::string(64, 'd'),
    };
}

op1::Corpus make_valid_corpus() {
    op1::Census census = make_valid_census();
    std::vector<op1::RootExample> train;
    std::vector<op1::RootExample> dev;
    for (const auto& root : census.roots) {
        auto example = synthetic_example(root);
        if (root.coordinate.split ==
            op1::Split::Train) {
            train.push_back(std::move(example));
        } else {
            dev.push_back(std::move(example));
        }
    }
    return op1::testing::make_corpus(
        std::move(census), synthetic_components(),
        std::move(train), std::move(dev));
}

op1::Metrics synthetic_metrics(double regret) {
    op1::Metrics result;
    result.equal_deck_mean_regret = regret;
    result.equal_deck_top_one_expected_agreement =
        1.0 - regret;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        result.decks[deck] = {
            .deck = static_cast<old_school::DeckId>(deck),
            .roots = 16,
            .options = 32,
            .weight_mass = 1.0,
            .top_one_expected_agreement =
                1.0 - regret,
            .mean_regret = regret,
        };
        result.roots += 16;
        result.options += 32;
    }
    return result;
}

std::pair<op1::FitReport, op1::OfflineReport>
passing_reports(const op1::Corpus& corpus) {
    const std::string candidate(64, 'e');
    op1::FitReport fit;
    fit.corpus_digest = corpus.digest;
    fit.parent_fingerprint_before =
        std::string(
            op1::kRequiredWarmParentFingerprint);
    fit.parent_fingerprint_after =
        fit.parent_fingerprint_before;
    fit.candidate_fingerprint = candidate;
    fit.parent_components = corpus.parent_components;
    fit.candidate_components =
        synthetic_components('q');
    fit.optimizer = op1::optimizer_config();
    fit.fit_examples = corpus.train.size();
    for (const auto& example : corpus.train) {
        fit.fit_options +=
            example.manifest.actions.size();
    }
    fit.parent_immutable = true;
    fit.repeated_fit_bit_identical = true;
    fit.only_priority_component_changed = true;
    fit.parent_train = synthetic_metrics(0.20);
    fit.candidate_train = synthetic_metrics(0.10);
    fit.parent_dev = synthetic_metrics(0.20);
    fit.candidate_dev = synthetic_metrics(0.19);

    op1::OfflineReport offline;
    offline.corpus_digest = corpus.digest;
    offline.parent_fingerprint =
        fit.parent_fingerprint_before;
    offline.candidate_fingerprint = candidate;
    offline.census_frozen = true;
    offline.corpus_digest_exact = true;
    offline.preflight_exact = true;
    offline.parent_immutable = true;
    offline.repeated_fit_bit_identical = true;
    offline.only_priority_component_changed = true;
    offline.train_regret_strictly_improved = true;
    offline.dev_regret_strictly_improved = true;
    offline.dev_deck_regret_guard.fill(true);
    offline.parent_train_signal_nonzero.fill(true);
    offline.parent_dev_signal_nonzero.fill(true);
    offline.targets_finite_and_normalized = true;
    offline.descriptor_order_identity = true;
    offline.redundant_counter_pass = true;
    offline.braingeyser_productive = true;
    offline.sick_bear_growth_pass = true;
    offline.live_force_spike = true;
    offline.ancestral_pass = true;
    offline.parent_train = fit.parent_train;
    offline.candidate_train = fit.candidate_train;
    offline.parent_dev = fit.parent_dev;
    offline.candidate_dev = fit.candidate_dev;
    return {std::move(fit), std::move(offline)};
}

void test_command_and_recipe() {
    const std::array<std::string_view, 1> census{
        "--census"};
    const std::array<std::string_view, 1> run{"--run"};
    const std::array<std::string_view, 1> bad{"--preflight"};
    expect(
        op1::parse_command(census) ==
            op1::Command::Census,
        "census command must parse");
    expect(
        op1::parse_command(run) == op1::Command::Run,
        "run command must parse");
    expect(
        !op1::parse_command(bad).has_value(),
        "undeclared command must fail");
    expect(
        !op1::parse_command({}).has_value(),
        "missing command must fail");

    std::ostringstream usage;
    op1::print_usage(usage);
    expect(
        usage.str().find("--census|--run") !=
            std::string::npos,
        "usage must expose only sealed commands");

    const auto source =
        op1::source_game_config({}, 1);
    expect(
        source.max_turns == op1::kSourceTurnCap &&
            source.starting_player == 1 &&
            source.learned_search_depth == 1,
        "source game recipe must be exact");
    for (const auto& bot : source.bots) {
        expect(
            bot.kind == old_school::BotKind::Learned &&
                bot.learned_variant ==
                    old_school::LearnedVariant::
                        ValueSearchChampion &&
                bot.rollouts_per_action ==
                    op1::kBaseWorlds &&
                bot.value_priority_residual_weight ==
                    op1::kResidualWeight &&
                !bot.value_pass_dominance &&
                !bot.value_adversarial_blocks &&
                !bot.value_actor_local_search &&
                bot.value_continuation_controller ==
                    old_school::
                        LearnedContinuationController::
                            Legacy,
            "source bot must be the warm deployed mirror");
    }

    const auto base = op1::base_search_config(17);
    const auto teacher =
        op1::teacher_search_config(17);
    expect(
        base.seed == 17 &&
            base.worlds == op1::kBaseWorlds &&
            base.horizon_turns ==
                op1::g1::kBaseHorizonTurns &&
            base.blend_shallow_prior &&
            base.value_priority_residual_weight == 0.0 &&
            base.value_continuation_search_worlds == 0,
        "base scorer must remain residual-free K8/H4");
    expect(
        teacher.seed == 17 &&
            teacher.worlds == op1::kTeacherWorlds &&
            teacher.horizon_turns ==
                op1::g1::kTeacherHorizonTurns &&
            !teacher.blend_shallow_prior &&
            teacher.value_priority_residual_weight ==
                op1::kResidualWeight &&
            teacher.value_continuation_search_worlds ==
                op1::kInnerWorlds,
        "teacher must be exact residual-active AQ4");
    expect(
        op1::optimizer_config() ==
            op1::g4b::optimizer_config(),
        "optimizer must be unchanged from G4B");
    expect_rejected(
        [] {
            static_cast<void>(
                op1::source_game_config({}, 2));
        },
        "invalid starting player must fail");
}

void test_seed_and_weight_contract() {
    const auto expected =
        iteration::derive_seed(
            op1::kCollectionRootSeed,
            iteration::SeedDomain::PrioritySearch,
            op1::kScheduleGeneration,
            7, (std::uint64_t{1} << 32) | 11);
    expect(
        op1::root_search_seed(
            op1::Split::Train, 7, 1, 11) ==
            expected,
        "TRAIN label seed must use exact semantic coordinate");
    const auto expected_dev =
        iteration::derive_seed(
            op1::kCollectionRootSeed,
            iteration::SeedDomain::PrioritySearch,
            op1::kScheduleGeneration,
            op1::kGamesPerSplit + 7,
            (std::uint64_t{1} << 32) | 11);
    expect(
        op1::root_search_seed(
            op1::Split::Dev, 7, 1, 11) ==
            expected_dev,
        "DEV label seed must offset by forty games");
    expect(
        expected != expected_dev,
        "TRAIN and DEV label seeds must be disjoint");
    expect(
        op1::root_weight(1) == 1.0 / 16.0 &&
            op1::root_weight(4) == 1.0 / 64.0 &&
            op1::root_weight(6) == 1.0 / 96.0,
        "each actor-game must have mass 1/16");
    expect_rejected(
        [] { static_cast<void>(op1::root_weight(0)); },
        "zero retained roots must fail");
    expect_rejected(
        [] {
            static_cast<void>(
                op1::root_search_seed(
                    op1::Split::Train,
                    op1::kGamesPerSplit, 0, 0));
        },
        "out-of-range schedule coordinate must fail");
}

void test_census_validation_and_mutations() {
    const op1::Census census = make_valid_census();
    op1::validate_census(census);
    expect(
        census.games() == 80 &&
            census.splits[0].actor_games.size() == 80 &&
            census.splits[1].actor_games.size() == 80 &&
            census.splits[0].retained_roots() == 80 &&
            census.splits[1].retained_roots() == 80,
        "synthetic census must cover exact balanced splits");
    for (const auto& split : census.splits) {
        for (const auto& deck : split.decks) {
            expect(
                deck.actor_games == 16 &&
                    deck.retained_roots == 16,
                "every split/deck must contain sixteen actor-games");
        }
    }
    expect(
        op1::canonical_manifest_hash(census) ==
            census.manifest_hash,
        "manifest hash must be canonical");
    const op1::CensusCountSeal measured =
        op1::census_count_seal(census);
    expect(
        measured.splits[0].games == 40 &&
            measured.splits[1].games == 40 &&
            measured.splits[0].actor_games == 80 &&
            measured.splits[1].actor_games == 80 &&
            measured.splits[0].nontrivial_roots == 80 &&
            measured.splits[1].nontrivial_roots == 80 &&
            measured.splits[0].retained_roots == 80 &&
            measured.splits[1].retained_roots == 80 &&
            measured.splits[0].retained_options == 160 &&
            measured.splits[1].retained_options == 160,
        "count seal must bind every split aggregate");
    for (const auto& split : measured.splits) {
        for (const auto& deck : split.decks) {
            expect(
                deck.actor_games == 16 &&
                    deck.nontrivial_roots == 16 &&
                    deck.retained_roots == 16 &&
                    deck.retained_options == 32,
                "count seal must bind every split/deck row");
        }
    }
    expect(
        op1::frozen_census_seal_populated(),
        "measured hash/count source seals must be populated");
    expect(
        op1::kFrozenCensusManifestHash ==
                "2900062d0df381463663de6d7f25ce562197bfac0d859949ca0803e48b14aef7" &&
            op1::kFrozenCensusCounts.splits[0].games == 40 &&
            op1::kFrozenCensusCounts.splits[0].actor_games == 80 &&
            op1::kFrozenCensusCounts.splits[0].nontrivial_roots == 2079 &&
            op1::kFrozenCensusCounts.splits[0].retained_roots == 480 &&
            op1::kFrozenCensusCounts.splits[0].retained_options == 1568 &&
            op1::kFrozenCensusCounts.splits[1].games == 40 &&
            op1::kFrozenCensusCounts.splits[1].actor_games == 80 &&
            op1::kFrozenCensusCounts.splits[1].nontrivial_roots == 2442 &&
            op1::kFrozenCensusCounts.splits[1].retained_roots == 160 &&
            op1::kFrozenCensusCounts.splits[1].retained_options == 436,
        "source seals must bind the measured split census exactly");
    const std::array<
        std::array<op1::DeckCensus, old_school::kDeckCount>, 2>
        expected_frozen_decks{{
            {{
                {16, 330, 96, 241},
                {16, 393, 96, 323},
                {16, 386, 96, 223},
                {16, 583, 96, 307},
                {16, 387, 96, 474},
            }},
            {{
                {16, 425, 32, 77},
                {16, 418, 32, 101},
                {16, 368, 32, 73},
                {16, 811, 32, 93},
                {16, 420, 32, 92},
            }},
        }};
    expect(
        op1::kFrozenCensusCounts.splits[0].decks ==
                expected_frozen_decks[0] &&
            op1::kFrozenCensusCounts.splits[1].decks ==
                expected_frozen_decks[1],
        "source seals must bind all ten measured split/deck rows");
    expect_rejected(
        [&] { op1::require_frozen_census(census); },
        "a synthetic census must differ from the frozen source seal");

    std::ostringstream printed;
    op1::print_census(printed, census, 12, 4);
    expect(
        printed.str().find(
            "train_nontrivial_roots=80") !=
                std::string::npos &&
            printed.str().find(
                "dev_nontrivial_roots=80") !=
                std::string::npos &&
            printed.str().find(
                "replayed_g4b_total_labels=16") !=
                std::string::npos &&
            printed.str().find(
                "op1_label_coordinates_opened=0") !=
                std::string::npos,
        "census output must separate all measured counts and old labels");

    auto wrong_parent = census;
    wrong_parent.parent_fingerprint =
        std::string(64, 'x');
    wrong_parent.manifest_hash =
        op1::canonical_manifest_hash(wrong_parent);
    expect_rejected(
        [&] { op1::validate_census(wrong_parent); },
        "wrong warm-parent identity must fail");

    auto wrong_seed = census;
    wrong_seed.roots.front().coordinate.search_seed ^= 1;
    wrong_seed =
        op1::testing::make_census(
            wrong_seed.parent_fingerprint,
            wrong_seed.splits, wrong_seed.roots);
    expect_rejected(
        [&] { op1::validate_census(wrong_seed); },
        "mutated semantic label seed must fail");

    auto wrong_descriptor = census;
    wrong_descriptor.roots.front()
        .action_descriptors.front() += "-mutated";
    wrong_descriptor =
        op1::testing::make_census(
            wrong_descriptor.parent_fingerprint,
            wrong_descriptor.splits,
            wrong_descriptor.roots);
    expect_rejected(
        [&] { op1::validate_census(wrong_descriptor); },
        "mutated typed descriptor must fail");

    auto hidden_feature = census;
    hidden_feature.roots.front().options.front().pop_back();
    hidden_feature =
        op1::testing::make_census(
            hidden_feature.parent_fingerprint,
            hidden_feature.splits,
            hidden_feature.roots);
    expect_rejected(
        [&] { op1::validate_census(hidden_feature); },
        "feature-width drift must fail");
}

void test_corpus_projection_and_mutations() {
    const op1::Corpus corpus = make_valid_corpus();
    op1::validate_corpus(corpus);
    const auto examples =
        op1::testing::training_examples(corpus);
    expect(
        examples.size() == corpus.train.size() &&
            examples.size() == 80,
        "fit projection must contain TRAIN only");
    expect(
        examples.front().target_probabilities ==
            corpus.train.front().target_probabilities,
        "fit projection must preserve soft all-action targets");

    auto changed_dev = corpus;
    std::swap(
        changed_dev.dev.front().teacher_scores[0],
        changed_dev.dev.front().teacher_scores[1]);
    changed_dev.dev.front().target_probabilities =
        old_school::learned_soft_priority_target(
            changed_dev.dev.front().teacher_scores);
    changed_dev =
        op1::testing::make_corpus(
            changed_dev.census,
            changed_dev.parent_components,
            changed_dev.train, changed_dev.dev);
    op1::validate_corpus(changed_dev);
    const auto changed_examples =
        op1::testing::training_examples(changed_dev);
    bool projection_identical =
        changed_examples.size() == examples.size();
    for (std::size_t index = 0;
         projection_identical &&
         index < examples.size(); ++index) {
        projection_identical =
            changed_examples[index].options ==
                examples[index].options &&
            changed_examples[index].base_scores ==
                examples[index].base_scores &&
            changed_examples[index].target_probabilities ==
                examples[index].target_probabilities &&
            changed_examples[index].weight ==
                examples[index].weight;
    }
    expect(
        projection_identical,
        "DEV changes must not enter fit projection");

    auto bad_target = corpus;
    bad_target.train.front().target_probabilities[0] =
        0.99;
    bad_target =
        op1::testing::make_corpus(
            bad_target.census,
            bad_target.parent_components,
            bad_target.train, bad_target.dev);
    expect_rejected(
        [&] { op1::validate_corpus(bad_target); },
        "unnormalized target must fail");

    auto bad_weight = corpus;
    bad_weight.train.front().weight *= 2.0;
    bad_weight =
        op1::testing::make_corpus(
            bad_weight.census,
            bad_weight.parent_components,
            bad_weight.train, bad_weight.dev);
    expect_rejected(
        [&] { op1::validate_corpus(bad_weight); },
        "actor-game weight mutation must fail");

    auto bad_components = corpus;
    bad_components.parent_components.priority.pop_back();
    bad_components =
        op1::testing::make_corpus(
            bad_components.census,
            bad_components.parent_components,
            bad_components.train, bad_components.dev);
    expect_rejected(
        [&] { op1::validate_corpus(bad_components); },
        "component identity width mutation must fail");
}

void test_selector_binding_and_classification() {
    const op1::Corpus corpus = make_valid_corpus();
    auto [fit, offline] = passing_reports(corpus);
    const std::string candidate(64, 'e');
    expect(
        op1::testing::selector_binding_exact(
            corpus, fit, offline,
            op1::g4b::kRequiredParentFingerprint,
            op1::kRequiredWarmParentFingerprint,
            candidate, corpus.census.manifest_hash,
            op1::census_count_seal(corpus.census)),
        "complete pure selector binding must pass");

    auto bad_fit = fit;
    bad_fit.fit_examples += 1;
    expect(
        !op1::testing::selector_binding_exact(
            corpus, bad_fit, offline,
            op1::g4b::kRequiredParentFingerprint,
            op1::kRequiredWarmParentFingerprint,
            candidate, corpus.census.manifest_hash,
            op1::census_count_seal(corpus.census)),
        "fit count mutation must close selector");

    auto bad_offline = offline;
    bad_offline.dev_regret_strictly_improved = false;
    expect(
        !op1::testing::selector_binding_exact(
            corpus, fit, bad_offline,
            op1::g4b::kRequiredParentFingerprint,
            op1::kRequiredWarmParentFingerprint,
            candidate, corpus.census.manifest_hash,
            op1::census_count_seal(corpus.census)),
        "DEV failure must close selector");
    expect(
        !op1::testing::selector_binding_exact(
            corpus, fit, offline,
            op1::g4b::kRequiredParentFingerprint,
            op1::kRequiredWarmParentFingerprint,
            candidate, "",
            op1::census_count_seal(corpus.census)),
        "empty source seal must close selector");
    auto wrong_counts =
        op1::census_count_seal(corpus.census);
    ++wrong_counts.splits[0].nontrivial_roots;
    expect(
        !op1::testing::selector_binding_exact(
            corpus, fit, offline,
            op1::g4b::kRequiredParentFingerprint,
            op1::kRequiredWarmParentFingerprint,
            candidate, corpus.census.manifest_hash,
            wrong_counts),
        "count-seal mutation must close selector");

    old_school::BotBenchmarkSummary summary;
    summary.challenger_stats.wins = 31;
    for (auto& deck : summary.challenger_decks) {
        deck.wins = 3;
    }
    expect(
        op1::classify_selector(summary) ==
            op1::SelectorDisposition::ManualOnly,
        "31/60 with all deck floors must license manual only");
    summary.challenger_stats.wins = 37;
    expect(
        op1::classify_selector(summary) ==
            op1::SelectorDisposition::ManualOnly,
        "even 37/60 can license only an OP1 manual pilot");
    summary.challenger_decks[0].wins = 2;
    expect(
        op1::classify_selector(summary) ==
            op1::SelectorDisposition::Reject,
        "any deck below 3/12 must reject");
}

} // namespace

int main() {
    try {
        test_command_and_recipe();
        test_seed_and_weight_contract();
        test_census_validation_and_mutations();
        test_corpus_projection_and_mutations();
        test_selector_binding_and_classification();
        std::cout
            << "5 action-Q on-policy successor tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "action-Q on-policy successor test failed: "
            << error.what() << '\n';
        return 1;
    }
}
