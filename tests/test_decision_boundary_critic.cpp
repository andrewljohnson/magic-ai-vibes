#include "old_school/decision_boundary_critic.hpp"

#include "old_school/probes.hpp"

#include <array>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
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

    std::cout << passed
              << " decision-boundary critic tests passed\n";
    return 0;
}
