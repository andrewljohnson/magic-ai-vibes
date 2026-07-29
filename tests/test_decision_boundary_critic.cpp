#include "old_school/decision_boundary_critic.hpp"

#include "old_school/probes.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace dbc =
    old_school::decision_boundary_critic;

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
concept HasOpponentHandMember = requires(T value) {
    value.opponent_hand;
};

template <typename T>
concept HasSourceOutcomeMember = requires(T value) {
    value.source_outcome;
};

template <typename T>
concept HasTeacherLabelMember = requires(T value) {
    value.teacher_label;
};

static_assert(!HasStateMember<dbc::Census>);
static_assert(!HasOpponentHandMember<dbc::Census>);
static_assert(!HasSourceOutcomeMember<dbc::Census>);
static_assert(!HasTeacherLabelMember<dbc::Census>);
static_assert(!HasStateMember<dbc::ManifestRoot>);
static_assert(!HasOpponentHandMember<dbc::ManifestRoot>);
static_assert(!HasSourceOutcomeMember<dbc::ManifestRoot>);
static_assert(!HasTeacherLabelMember<dbc::ManifestRoot>);
static_assert(dbc::kPolicyFeatureCount == 893);

std::string hex_identity(std::size_t value) {
    std::ostringstream output;
    output << std::hex << std::setw(64)
           << std::setfill('0') << value;
    return output.str();
}

dbc::ManifestRoot make_root(
    dbc::Split split, std::size_t schedule,
    std::size_t actor, std::size_t identity) {
    const std::size_t owner_index =
        (schedule * 2 + actor) %
        old_school::kDeckCount;
    const auto owner =
        static_cast<old_school::DeckId>(owner_index);
    const auto other =
        static_cast<old_school::DeckId>(
            (owner_index + 1) %
            old_school::kDeckCount);
    std::array<old_school::DeckId, 2> seats{
        owner, other};
    if (actor == 1) {
        seats = {other, owner};
    }

    std::vector<old_school::PriorityAction> actions{
        old_school::PriorityAction::pass(),
        old_school::PriorityAction::play_land(
            old_school::CardId::Forest),
    };
    std::vector<std::string> descriptors;
    std::vector<std::vector<double>> options;
    for (std::size_t action = 0;
         action < actions.size(); ++action) {
        descriptors.push_back(
            old_school::probes::
                stable_priority_action_descriptor(
                    actions[action]));
        std::vector<double> features(
            dbc::kPolicyFeatureCount, 0.0);
        features[0] =
            static_cast<double>(owner_index);
        features[1] =
            static_cast<double>(schedule);
        features[2] = static_cast<double>(actor);
        features[3] = static_cast<double>(action);
        options.push_back(std::move(features));
    }
    return {
        .coordinate = {
            .split = split,
            .schedule_index = schedule,
            .pairing_index = schedule / 2,
            .game_seed =
                static_cast<std::uint64_t>(1000 + identity),
            .starting_player = schedule % 2,
            .seat_decks = seats,
            .actor = actor,
            .trace_ordinal = identity + 1,
            .nontrivial_ordinal = 0,
            .actor_game_nontrivial_roots = 2,
            .retained_position = 0,
            .actor_game_retained_roots = 2,
            .search_seed =
                static_cast<std::uint64_t>(2000 + identity),
        },
        .stable_root_id = hex_identity(identity + 1),
        .information_action_fingerprint =
            hex_identity(identity + 1001),
        .actions = std::move(actions),
        .action_descriptors = std::move(descriptors),
        .options = std::move(options),
    };
}

dbc::Census make_valid_census() {
    std::array<dbc::SplitCensus, 2> splits{
        dbc::SplitCensus{
            .split = dbc::Split::Train,
            .games =
                dbc::source::kGamesPerSplit,
            .actor_games =
                dbc::source::kActorGamesPerSplit,
        },
        dbc::SplitCensus{
            .split = dbc::Split::Dev,
            .games =
                dbc::source::kGamesPerSplit,
            .actor_games =
                dbc::source::kActorGamesPerSplit,
        },
    };
    std::vector<dbc::ManifestRoot> roots;
    std::size_t identity = 0;
    for (const dbc::Split split :
         {dbc::Split::Train, dbc::Split::Dev}) {
        const std::size_t split_value =
            dbc::source::split_index(split);
        for (std::size_t schedule = 0;
             schedule < dbc::source::kGamesPerSplit;
             ++schedule) {
            for (std::size_t actor = 0;
                 actor < 2; ++actor) {
                auto root =
                    make_root(
                        split, schedule, actor, identity++);
                const std::size_t deck =
                    static_cast<std::size_t>(
                        root.coordinate.owner_deck());
                ++splits[split_value].roots;
                splits[split_value].legal_options +=
                    root.actions.size();
                ++splits[split_value].decks[deck].roots;
                splits[split_value]
                    .decks[deck]
                    .legal_options += root.actions.size();
                roots.push_back(std::move(root));
            }
        }
        for (auto& deck :
             splits[split_value].decks) {
            deck.actor_games =
                dbc::kExpectedRootsPerDeckAndSplit;
        }
    }
    return dbc::testing::make_census(
        std::move(splits), std::move(roots));
}

old_school::LearnedModelComponentFingerprints
make_components() {
    return {
        .critic = hex_identity(4001),
        .priority = hex_identity(4002),
        .attack = hex_identity(4003),
        .block = hex_identity(4004),
        .damage_order = hex_identity(4005),
    };
}

dbc::RootExample make_example(
    const dbc::ManifestRoot& manifest,
    bool terminal_first_cell = false) {
    constexpr std::size_t action_count = 2;
    expect(
        manifest.actions.size() == action_count,
        "synthetic manifest action count drifted");
    dbc::RootExample result{
        .manifest = manifest,
        .teacher_samples = {
            std::vector<double>(
                dbc::kTeacherWorlds, 0.8),
            std::vector<double>(
                dbc::kTeacherWorlds, 0.2),
        },
        .accounting = {
            .sampled_worlds = dbc::kTeacherWorlds,
            .rollout_evaluations =
                action_count * dbc::kTeacherWorlds,
            .terminal_evaluations =
                terminal_first_cell ? 1U : 0U,
            .bootstrapped_evaluations =
                action_count * dbc::kTeacherWorlds -
                (terminal_first_cell ? 1U : 0U),
            .eligible_cells =
                action_count * dbc::kTeacherWorlds -
                (terminal_first_cell ? 1U : 0U),
            .terminal_before_boundary_cells =
                terminal_first_cell ? 1U : 0U,
            .inner_rollout_evaluations = 32,
            .inner_search_invocations = 4,
            .inner_search_max_depth = 1,
        },
    };
    const double cell_weight =
        1.0 /
        static_cast<double>(
            old_school::kDeckCount *
            dbc::kExpectedRootsPerDeckAndSplit *
            result.accounting.eligible_cells);
    for (std::size_t action = 0;
         action < action_count; ++action) {
        for (std::size_t world = 0;
             world < dbc::kTeacherWorlds; ++world) {
            const bool terminal =
                terminal_first_cell &&
                action == 0 && world == 0;
            const double target =
                result.teacher_samples[action][world];
            result.cells.push_back({
                .action_index = action,
                .world_index = world,
                .teacher_target = target,
                .parent_prediction =
                    terminal ? target : 0.5,
                .weight =
                    terminal ? 0.0 : cell_weight,
                .terminal_before_boundary = terminal,
                .observation =
                    terminal
                        ? std::vector<double>{}
                        : std::vector<double>(
                              dbc::kCriticFeatureCount,
                              static_cast<double>(
                                  action + world) /
                                  100.0),
                .boundary_state =
                    terminal
                        ? std::optional<
                              old_school::GameState>{}
                        : std::optional<
                              old_school::GameState>{
                              old_school::GameState{}},
            });
        }
    }
    return result;
}

dbc::Corpus make_valid_corpus(
    bool terminal_first_cell = false) {
    dbc::Census census = make_valid_census();
    std::vector<dbc::RootExample> train;
    std::vector<dbc::RootExample> dev;
    for (const auto& root : census.roots) {
        auto example =
            make_example(root, terminal_first_cell);
        if (root.coordinate.split ==
            dbc::Split::Train) {
            train.push_back(std::move(example));
        } else {
            dev.push_back(std::move(example));
        }
    }
    return dbc::testing::make_corpus(
        std::move(census), make_components(),
        std::move(train), std::move(dev));
}

std::vector<dbc::RootPrediction> make_predictions(
    const std::vector<dbc::RootExample>& examples,
    bool rank_teacher) {
    std::vector<dbc::RootPrediction> result;
    result.reserve(examples.size());
    for (const auto& root : examples) {
        dbc::RootPrediction prediction{
            .stable_root_id =
                root.manifest.stable_root_id,
            .action_samples =
                root.teacher_samples,
        };
        for (const auto& cell : root.cells) {
            if (cell.terminal_before_boundary) {
                continue;
            }
            prediction.action_samples
                [cell.action_index][cell.world_index] =
                    rank_teacher
                        ? cell.teacher_target
                        : (cell.action_index == 0
                               ? 0.3
                               : 0.7);
        }
        result.push_back(std::move(prediction));
    }
    return result;
}

} // namespace

int main() {
    std::size_t passed = 0;
    const auto test =
        [&](std::string_view name, auto&& function) {
            function();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        };

    test("command surface accepts only --census", [] {
        const std::array<std::string_view, 1> census{
            "--census"};
        const std::array<std::string_view, 1> run{"--run"};
        const std::array<std::string_view, 2> extra{
            "--census", "--run"};
        expect(
            dbc::parse_census_command(census),
            "--census was rejected");
        expect(
            !dbc::parse_census_command(run),
            "--run was accepted");
        expect(
            !dbc::parse_census_command(extra),
            "extra arguments were accepted");
        expect(
            !dbc::parse_census_command({}),
            "empty arguments were accepted");
    });

    test("position-zero selection is blind and stable", [] {
        auto first =
            make_root(dbc::Split::Train, 0, 0, 0);
        auto skipped =
            make_root(dbc::Split::Train, 0, 0, 1);
        auto second =
            make_root(dbc::Split::Train, 0, 1, 2);
        skipped.coordinate.retained_position = 1;
        std::vector<dbc::ManifestRoot> source{
            first, skipped, second};
        const auto selected =
            dbc::testing::select_position_zero(source);
        expect(
            selected ==
                std::vector<dbc::ManifestRoot>{first, second},
            "selection did not preserve only position-zero roots");
    });

    test("balanced census validates and hashes deterministically", [] {
        const dbc::Census first = make_valid_census();
        const dbc::Census second = make_valid_census();
        dbc::validate_census(first);
        dbc::validate_census(second);
        expect(first == second, "repeated census assembly drifted");
        expect(
            first.subset_hash == second.subset_hash &&
                first.subset_hash.size() == 64,
            "repeated subset hashes drifted");
        for (const auto& split : first.splits) {
            expect(
                split.roots ==
                    dbc::kExpectedRootsPerSplit,
                "split root count drifted");
            for (const auto& deck : split.decks) {
                expect(
                    deck.roots ==
                        dbc::kExpectedRootsPerDeckAndSplit,
                    "per-deck root count drifted");
            }
        }
    });

    test("validation fails closed on coordinate drift", [] {
        dbc::Census census = make_valid_census();
        census.roots.front().coordinate.retained_position = 1;
        census = dbc::testing::make_census(
            census.splits, census.roots);
        expect_rejected(
            [&] { dbc::validate_census(census); },
            "nonzero retained position was accepted");
    });

    test("validation fails closed on feature width drift", [] {
        dbc::Census census = make_valid_census();
        census.roots.front().options.front().pop_back();
        census = dbc::testing::make_census(
            census.splits, census.roots);
        expect_rejected(
            [&] { dbc::validate_census(census); },
            "short policy feature row was accepted");
    });

    test("validation fails closed on action identity drift", [] {
        dbc::Census census = make_valid_census();
        census.roots.front()
            .action_descriptors.front()
            .clear();
        census = dbc::testing::make_census(
            census.splits, census.roots);
        expect_rejected(
            [&] { dbc::validate_census(census); },
            "empty action descriptor was accepted");
    });

    test("validation fails closed on deck imbalance", [] {
        dbc::Census census = make_valid_census();
        ++census.splits[0].decks[0].roots;
        census = dbc::testing::make_census(
            census.splits, census.roots);
        expect_rejected(
            [&] { dbc::validate_census(census); },
            "imbalanced deck summary was accepted");
    });

    test("report exposes census only", [] {
        const dbc::Census census = make_valid_census();
        std::ostringstream output;
        dbc::print_census(output, census);
        const std::string report = output.str();
        expect(
            report.find("policy_feature_width=893") !=
                std::string::npos,
            "feature width is absent");
        expect(
            report.find("census_split split=TRAIN") !=
                    std::string::npos &&
                report.find("census_split split=DEV") !=
                    std::string::npos,
            "split reports are absent");
        expect(
            report.find("result=PASS disposition=CENSUS_ONLY") !=
                std::string::npos,
            "census-only disposition is absent");
        expect(
            report.find("labels_scored=0") !=
                std::string::npos,
            "label firewall is absent");
    });

    test("teacher recipe and coordinate seeds are fixed", [] {
        const dbc::Census census = make_valid_census();
        const auto first =
            dbc::teacher_search_seed(
                census.roots.front().coordinate);
        const auto repeated =
            dbc::teacher_search_seed(
                census.roots.front().coordinate);
        auto changed_coordinate =
            census.roots.front().coordinate;
        ++changed_coordinate.nontrivial_ordinal;
        const auto changed =
            dbc::teacher_search_seed(
                changed_coordinate);
        expect(first == repeated, "teacher seed drifted");
        expect(first != changed, "teacher coordinate aliased");
        const auto config =
            dbc::teacher_search_config(first);
        expect(
            config.worlds == 8 &&
                config.rollouts_per_world == 1 &&
                config.horizon_turns == 8 &&
                config.evaluation_threads == 4 &&
                config.value_continuation_search_worlds ==
                    2 &&
                config.capture_priority_h0_boundaries &&
                config.terminal_utility_mode ==
                    old_school::LearnedTerminalUtilityMode::
                        C16DiscountedAbsoluteTurn,
            "teacher recipe drifted");
    });

    test("successor corpus validates equal deck and root weights", [] {
        const dbc::Corpus corpus =
            make_valid_corpus(true);
        dbc::validate_corpus(corpus);
        const auto examples =
            dbc::training_examples(corpus);
        expect(
            examples.size() ==
                dbc::kExpectedRootsPerSplit *
                    (2 * dbc::kTeacherWorlds - 1),
            "terminal cells entered TRAIN projection");
        double total_weight = 0.0;
        for (const auto& example : examples) {
            total_weight += example.weight;
        }
        expect(
            std::abs(total_weight - 1.0) < 1.0e-12,
            "TRAIN example weights did not sum to one");
    });

    test("corpus digest excludes transient boundary state", [] {
        dbc::Corpus first = make_valid_corpus();
        dbc::Corpus second = first;
        second.train.front()
            .cells.front()
            .boundary_state
            ->players[1]
            .hand.push_back(
                old_school::CardId::LightningBolt);
        expect(
            dbc::canonical_corpus_digest(first) ==
                dbc::canonical_corpus_digest(second),
            "transient hidden state entered corpus digest");
        expect(
            first != second,
            "repeated-collection seam ignored live state");
    });

    test("hidden repartition is nonvacuous and owner-safe", [] {
        old_school::GameState state;
        state.players[0].library = {
            old_school::CardId::Forest,
            old_school::CardId::GrizzlyBears,
        };
        state.players[1].hand = {
            old_school::CardId::LightningBolt,
            old_school::CardId::Mountain,
        };
        state.players[1].library = {
            old_school::CardId::IronclawOrcs,
            old_school::CardId::HillGiant,
        };
        const old_school::GameState clone =
            dbc::testing::hidden_repartition(state, 0);
        expect(
            clone != state,
            "hidden repartition was vacuous");
        expect(
            old_school::observe_game_state(state, 0) ==
                old_school::observe_game_state(clone, 0),
            "hidden repartition changed owner observation");
    });

    test("metrics reward calibrated action ranking", [] {
        const dbc::Corpus corpus =
            make_valid_corpus(true);
        const auto poor_predictions =
            make_predictions(corpus.dev, false);
        const auto good_predictions =
            make_predictions(corpus.dev, true);
        const dbc::Metrics poor =
            dbc::evaluate(
                corpus.dev, poor_predictions);
        const dbc::Metrics good =
            dbc::evaluate(
                corpus.dev, good_predictions);
        expect(
            good.equal_deck_weighted_bce <
                poor.equal_deck_weighted_bce,
            "better calibration did not lower BCE");
        expect(
            good.equal_deck_weighted_brier <
                poor.equal_deck_weighted_brier,
            "better calibration did not lower Brier");
        expect(
            poor.equal_deck_mean_regret > 0.5 &&
                good.equal_deck_mean_regret == 0.0,
            "teacher regret did not reflect action ordering");
        expect(
            poor.equal_deck_top_one_expected_agreement ==
                    0.0 &&
                good.equal_deck_top_one_expected_agreement ==
                    1.0,
            "exact-max top-one agreement drifted");
        expect(
            poor.stable_pairs > 0 &&
                poor.equal_deck_stable_pair_agreement ==
                    0.0 &&
                good.equal_deck_stable_pair_agreement ==
                    1.0,
            "stable-pair agreement drifted");
        for (const auto& deck : good.decks) {
            expect(
                deck.roots ==
                        dbc::kExpectedRootsPerDeckAndSplit &&
                    std::abs(deck.weight_mass - 0.2) <
                        1.0e-12,
                "per-deck metric mass drifted");
        }
    });

    test("metrics retain terminal utilities in action scores", [] {
        const dbc::Corpus corpus =
            make_valid_corpus(true);
        auto predictions =
            make_predictions(corpus.dev, true);
        predictions.front()
            .action_samples.front().front() = 0.5;
        expect_rejected(
            [&] {
                static_cast<void>(
                    dbc::evaluate(
                        corpus.dev, predictions));
            },
            "terminal action utility was replaceable");
    });

    test("corpus validation rejects cell weight drift", [] {
        dbc::Corpus corpus = make_valid_corpus();
        corpus.train.front().cells.front().weight *= 2.0;
        corpus = dbc::testing::make_corpus(
            corpus.census, corpus.parent_components,
            corpus.train, corpus.dev);
        expect_rejected(
            [&] { dbc::validate_corpus(corpus); },
            "cell weight mutation was accepted");
    });

    test("offline gate applies every metric conjunct", [] {
        const dbc::Corpus corpus =
            make_valid_corpus();
        const dbc::Metrics parent_train =
            dbc::evaluate(
                corpus.train,
                make_predictions(corpus.train, false));
        const dbc::Metrics candidate_train =
            dbc::evaluate(
                corpus.train,
                make_predictions(corpus.train, true));
        const dbc::Metrics parent_dev =
            dbc::evaluate(
                corpus.dev,
                make_predictions(corpus.dev, false));
        const dbc::Metrics candidate_dev =
            dbc::evaluate(
                corpus.dev,
                make_predictions(corpus.dev, true));
        dbc::FitReport fit{
            .optimizer = {
                .example_count =
                    corpus.train.size() * 2 *
                    dbc::kTeacherWorlds,
                .leaf_count =
                    old_school::
                        kLearnedOutputCalibrationLeafCount,
                .iterations = 32,
                .converged = true,
                .total_weight = 1.0,
                .before_weighted_bce = 0.7,
                .after_weighted_bce = 0.6,
                .max_parameter_delta = 0.1,
            },
            .authorized_output_parameters =
                dbc::kOutputParameterCount,
            .changed_output_parameters = 10,
            .parent_immutable = true,
            .repeated_fit_bit_identical = true,
            .parameter_replay_bit_identical = true,
            .only_output_layer_changed = true,
        };
        const dbc::OfflineGate gate =
            dbc::evaluate_offline_gate(
                corpus, fit,
                parent_train, candidate_train,
                parent_dev, candidate_dev,
                true, true);
        expect(
            gate.failures ==
                std::vector<std::string>{
                    "source or frozen subset identity failed"},
            "offline conjuncts failed beyond the synthetic "
            "unfrozen source");

        dbc::Metrics regressed = candidate_dev;
        regressed.equal_deck_top_one_expected_agreement =
            -1.0;
        regressed.equal_deck_mean_regret =
            parent_dev.equal_deck_mean_regret + 1.0;
        regressed.decks[0].mean_regret =
            parent_dev.decks[0].mean_regret + 1.0;
        const dbc::OfflineGate rejected =
            dbc::evaluate_offline_gate(
                corpus, fit,
                parent_train, candidate_train,
                parent_dev, regressed,
                true, true);
        expect(
            !rejected.dev_regret_strictly_improved &&
                !rejected.dev_top_one_non_decreasing &&
                !rejected.dev_deck_regret_guard[0],
            "DEV regression escaped conjunctive gate");
    });

    std::cout << passed
              << " decision-boundary critic tests passed\n";
    return 0;
}
