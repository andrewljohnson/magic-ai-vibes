#include "old_school/fq4_dev_coverage_census.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace census =
    old_school::fq4_dev_coverage_census;
namespace bundle =
    old_school::fq4_dev_bundle;
namespace generator =
    old_school::fq4_dev_generator;

namespace {

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
            std::cerr
                << "[FAIL] " << name << ": "
                << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout
            << passed_ << " passed, "
            << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(
            std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(
        std::string(message));
}

bundle::Hash256 digest(std::size_t value) {
    bundle::Hash256 result{};
    for (std::size_t byte = 0;
         byte < sizeof(value); ++byte) {
        result[byte] =
            static_cast<std::uint8_t>(
                (value >> (byte * 8U)) &
                0xffU);
    }
    if (value == 0) {
        result.back() = 1;
    }
    return result;
}

generator::CoverageRootObservation root(
    std::size_t stable_id,
    std::size_t physical_game,
    bundle::Split split = bundle::Split::Fit,
    std::size_t owner = 0,
    std::size_t opponent = 1,
    std::size_t block = 0,
    std::size_t quadrant = 0,
    std::size_t stack_size = 0,
    old_school::TurnPhase phase =
        old_school::TurnPhase::FirstMain,
    bool dominance_positive = false,
    std::uint8_t roles = 0,
    std::size_t options = 2) {
    const double stack_feature =
        static_cast<double>(stack_size) /
        static_cast<double>(
            census::kStackSizeEncodingDenominator);
    return {
        .split = split,
        .schedule_block =
            static_cast<std::uint8_t>(block),
        .owner_deck =
            static_cast<std::uint8_t>(owner),
        .opponent_deck =
            static_cast<std::uint8_t>(opponent),
        .owner_seat =
            static_cast<std::uint8_t>(
                quadrant / 2),
        .owner_on_play =
            quadrant % 2 == 0,
        .stable_root_id = digest(stable_id),
        .physical_game_sha256 =
            digest(100000 + physical_game),
        .option_count = options,
        .dominance_positive =
            dominance_positive,
        .selected_roles = roles,
        .public_stack_size = stack_size,
        .phase = phase,
        .stack_feature_bits =
            std::vector<std::uint64_t>(
                options,
                std::bit_cast<std::uint64_t>(
                    stack_feature)),
    };
}

std::size_t different_deck(
    std::size_t owner, std::size_t offset) {
    std::size_t opponent = offset;
    if (opponent >= owner) {
        ++opponent;
    }
    return opponent;
}

void test_balanced_block_feasibility() {
    std::array<
        std::array<
            std::size_t,
            census::kOwnerQuadrantCount>,
        bundle::kDeckCount>
        counts{};
    for (std::size_t offset = 0;
         offset < bundle::kDeckCount - 1;
         ++offset) {
        const std::size_t opponent =
            different_deck(0, offset);
        counts[opponent][offset] = 1;
        counts[opponent][
            (offset + 1) %
                census::kOwnerQuadrantCount] = 1;
    }
    expect(
        census::balanced_block_feasible(
            0, counts),
        "exact degree-two matrix was rejected");

    auto short_count = counts;
    short_count[4][3] = 0;
    expect(
        !census::balanced_block_feasible(
            0, short_count),
        "seven-game matrix was accepted");

    decltype(counts) quadrant_starved{};
    quadrant_starved[1][0] = 1;
    quadrant_starved[1][1] = 1;
    quadrant_starved[2][0] = 1;
    quadrant_starved[2][1] = 1;
    quadrant_starved[3][0] = 1;
    quadrant_starved[3][2] = 1;
    quadrant_starved[4][2] = 1;
    quadrant_starved[4][3] = 1;
    expect(
        !census::balanced_block_feasible(
            0, quadrant_starved),
        "quadrant-starved matrix was accepted");

    auto owner_contaminated = counts;
    owner_contaminated[0][0] = 1;
    expect(
        !census::balanced_block_feasible(
            0, owner_contaminated),
        "owner-as-opponent count was accepted");
    expect(
        !census::balanced_block_feasible(
            bundle::kDeckCount, counts),
        "out-of-range owner was accepted");

    auto impossible_multiplicity = counts;
    impossible_multiplicity[1][0] = 2;
    expect(
        !census::balanced_block_feasible(
            0, impossible_multiplicity),
        "two games in one frozen schedule cell were accepted");
}

void test_exact_32_game_capacity() {
    std::vector<
        generator::CoverageRootObservation>
        roots;
    std::size_t stable = 1;
    std::size_t game = 1;
    for (std::size_t split = 0;
         split < 2; ++split) {
        for (std::size_t owner = 0;
             owner < bundle::kDeckCount; ++owner) {
            for (std::size_t block = 0;
                 block < census::kScheduleBlockCount;
                 ++block) {
                for (std::size_t offset = 0;
                     offset <
                         bundle::kDeckCount - 1;
                     ++offset) {
                    const std::size_t opponent =
                        different_deck(owner, offset);
                    for (std::size_t copy = 0;
                         copy <
                             census::
                                 kGamesPerOpponent;
                         ++copy) {
                        roots.push_back(root(
                            stable++, game++,
                            split == 0
                                ? bundle::Split::Fit
                                : bundle::Split::Check,
                            owner, opponent, block,
                            (offset + copy) %
                                census::
                                    kOwnerQuadrantCount));
                    }
                }
            }
        }
    }
    const census::CoverageCensus exact =
        census::measure(roots);
    expect(
        exact.capacity_licensed,
        "all-40-cell capacity was not licensed");
    for (const census::SplitCensus* split :
         {&exact.fit, &exact.check}) {
        for (const census::DeckCensus& deck :
             split->decks) {
            expect(
                deck.capacity_met &&
                    deck.eligible_stack_empty.all
                            .distinct_games ==
                        32,
                "deck did not retain exact 32-game capacity");
            for (const census::BlockEligibility& block :
                 deck.eligible_blocks) {
                expect(
                    block.distinct_games == 8 &&
                        block.balanced_eight_feasible,
                    "block did not retain balanced eight-game capacity");
            }
        }
    }

    roots.pop_back();
    const census::CoverageCensus short_one =
        census::measure(roots);
    expect(
        !short_one.capacity_licensed &&
            !short_one.check.decks.back()
                 .eligible_blocks.back()
                 .balanced_eight_feasible,
        "one missing game did not close the exact capacity gate");
}

void test_category_and_distinct_game_accounting() {
    constexpr std::uint8_t kPositive =
        static_cast<std::uint8_t>(
            bundle::Role::DominancePositive);
    constexpr std::uint8_t kBackground =
        static_cast<std::uint8_t>(
            bundle::Role::BackgroundControl);
    std::vector<
        generator::CoverageRootObservation>
        roots{
            root(
                1, 1, bundle::Split::Fit,
                0, 1, 0, 0, 0,
                old_school::TurnPhase::FirstMain,
                true, kPositive, 2),
            root(
                2, 2, bundle::Split::Fit,
                0, 1, 0, 1, 0,
                old_school::TurnPhase::BeginCombat,
                false, kBackground, 3),
            root(
                3, 3, bundle::Split::Fit,
                0, 2, 0, 2, 0,
                old_school::TurnPhase::FirstMain,
                false, 0, 2),
            root(
                4, 3, bundle::Split::Fit,
                0, 2, 0, 2, 0,
                old_school::TurnPhase::SecondMain,
                false, 0, 4),
            root(
                5, 3, bundle::Split::Fit,
                0, 2, 0, 2, 0,
                old_school::TurnPhase::EndCombat,
                false, 0, 2),
            root(
                6, 4, bundle::Split::Fit,
                0, 3, 0, 3, 2,
                old_school::TurnPhase::BeginCombat,
                false, 0, 2),
        };
    const census::CoverageCensus measured =
        census::measure(roots);
    const census::DeckCensus& green =
        measured.fit.decks[0];
    expect(
        green.retained.roots == 6 &&
            green.retained.options == 15 &&
            green.retained.distinct_games == 4,
        "retained accounting drifted");
    expect(
        green.dominance_positive.empty.roots == 1 &&
            green.selected_background.empty.roots == 1 &&
            green.unselected_dominance_negative
                    .empty.roots ==
                3 &&
            green.unselected_dominance_negative
                    .active.roots ==
                1,
        "category partition drifted");
    expect(
        green.eligible_stack_empty.all.roots == 3 &&
            green.eligible_stack_empty.all.options == 8 &&
            green.eligible_stack_empty.all
                    .distinct_games ==
                1 &&
            green.eligible_stack_empty
                    .first_or_second_main.roots ==
                2 &&
            green.eligible_stack_empty.other.roots == 1 &&
            green.eligible_stack_empty.other
                    .distinct_games ==
                1,
        "phase or game deduplication drifted");
    expect(
        measured.selected_rows == 2 &&
            measured.selected_background_rows == 1 &&
            measured.action_invariant_stack_rows == 6 &&
            measured.exact_public_stack_rows == 6 &&
            measured.valid_public_phase_rows == 6,
        "global contract accounting drifted");
}

void test_malformed_roots_fail_closed() {
    const auto valid = root(1, 1);
    const auto reject_one =
        [](generator::CoverageRootObservation changed) {
            expect_rejected(
                [&] {
                    static_cast<void>(
                        census::measure({changed}));
                },
                "malformed root was accepted");
        };

    auto changed = valid;
    changed.schedule_block =
        census::kScheduleBlockCount;
    reject_one(changed);
    changed = valid;
    changed.owner_deck = bundle::kDeckCount;
    reject_one(changed);
    changed = valid;
    changed.opponent_deck = changed.owner_deck;
    reject_one(changed);
    changed = valid;
    changed.owner_seat = 2;
    reject_one(changed);
    changed = valid;
    changed.phase =
        static_cast<old_school::TurnPhase>(
            census::kPhaseCount);
    reject_one(changed);
    changed = valid;
    changed.option_count = 0;
    changed.stack_feature_bits.clear();
    reject_one(changed);
    changed = valid;
    changed.stack_feature_bits.pop_back();
    reject_one(changed);
    changed = valid;
    changed.stack_feature_bits.back() =
        std::bit_cast<std::uint64_t>(0.2);
    reject_one(changed);
    changed = valid;
    changed.stack_feature_bits.front() =
        std::bit_cast<std::uint64_t>(-0.0);
    reject_one(changed);
    changed = valid;
    changed.stack_feature_bits.front() =
        std::bit_cast<std::uint64_t>(
            std::numeric_limits<double>::
                quiet_NaN());
    reject_one(changed);
    changed = valid;
    changed.selected_roles =
        static_cast<std::uint8_t>(
            bundle::Role::DominancePositive);
    reject_one(changed);
    changed = valid;
    changed.selected_roles = 4;
    reject_one(changed);
    changed = valid;
    changed.stable_root_id = {};
    reject_one(changed);
    changed = valid;
    changed.physical_game_sha256 = {};
    reject_one(changed);

    auto duplicate = valid;
    duplicate.physical_game_sha256 =
        digest(100002);
    expect_rejected(
        [&] {
            static_cast<void>(
                census::measure(
                    {valid, duplicate}));
        },
        "duplicate stable root was accepted");

    auto conflicting = root(
        2, 1, bundle::Split::Fit,
        0, 2, 1);
    expect_rejected(
        [&] {
            static_cast<void>(
                census::measure(
                    {valid, conflicting}));
        },
        "conflicting physical-game stratum was accepted");

    auto same_schedule_cell =
        root(2, 2);
    expect_rejected(
        [&] {
            static_cast<void>(
                census::measure(
                    {valid, same_schedule_cell}));
        },
        "two physical games in one frozen schedule cell were accepted");
}

census::Report synthetic_report() {
    constexpr std::uint8_t kPositive =
        static_cast<std::uint8_t>(
            bundle::Role::DominancePositive);
    std::vector<
        generator::CoverageRootObservation>
        roots;
    roots.reserve(192);
    for (std::size_t index = 0;
         index < 192; ++index) {
        const std::size_t owner = index % 5;
        const bool positive =
            index <
            census::
                kPublishedSelectedPositiveRows;
        roots.push_back(root(
            index + 1, index + 1,
            index % 2 == 0
                ? bundle::Split::Fit
                : bundle::Split::Check,
            owner, (owner + 1) % 5,
            index % 4, index % 4, 0,
            old_school::TurnPhase::FirstMain,
            positive,
            positive
                ? kPositive
                : static_cast<std::uint8_t>(
                      bundle::Role::
                          BackgroundControl)));
    }
    return {
        .census = census::measure(roots),
        .source_games_reconstructed = 320,
        .selected_rows_reconstructed = 192,
        .parent_scoring = {
            .score_calls = 192,
            .scored_actions = 384,
            .sampled_worlds =
                192 * bundle::kWorldCount,
            .rollout_evaluations =
                384 * bundle::kWorldCount,
            .terminal_evaluations = 0,
            .bootstrap_evaluations =
                384 * bundle::kWorldCount,
        },
        .parent_artifact_sha256 =
            std::string(
                bundle::kParentArtifactSha256),
        .bundle_bytes =
            bundle::kPublishedArtifactBytes,
        .bundle_sha256 =
            std::string(
                bundle::kPublishedArtifactSha256),
        .schedules_exact = true,
        .scientific_manifest_exact = true,
        .fit_census_exact = true,
        .check_census_exact = true,
        .fit_selected_exact = true,
        .check_selected_exact = true,
        .fit_manifest_exact = true,
        .check_manifest_exact = true,
        .parent_immutable = true,
        .bundle_immutable = true,
        .parent_models_loaded = 1,
    };
}

void test_aggregate_only_format() {
    census::Report report =
        synthetic_report();
    expect(report.exact(), "synthetic report is not exact");
    const std::string output =
        census::format_report(report);
    expect(
        std::count(
            output.begin(), output.end(), '\n') ==
            155,
        "aggregate report line count drifted");
    for (const std::string_view forbidden : {
             "root_id",
             "physical_game",
             "descriptor",
             "card=",
             "hand=",
             "state=",
             "outcome=",
             "selected_action",
             "per_root",
         }) {
        expect(
            output.find(forbidden) ==
                std::string::npos,
            "aggregate report leaked a forbidden field");
    }
    expect(
        output.find(
            "games_if_licensed=32") !=
                std::string::npos &&
            output.ends_with(
                "result=PASS capacity_licensed=0\n"),
        "aggregate report omitted fixed capacity disposition");

    report.fit_manifest_exact = false;
    expect_rejected(
        [&] {
            static_cast<void>(
                census::format_report(report));
        },
        "forged reconstruction report was formatted");

    report = synthetic_report();
    --report.census.selected_positive_rows;
    expect_rejected(
        [&] {
            static_cast<void>(
                census::format_report(report));
        },
        "selected-positive cross-sum mutation was formatted");
    report = synthetic_report();
    --report.census.selected_background_rows;
    expect_rejected(
        [&] {
            static_cast<void>(
                census::format_report(report));
        },
        "selected-background cross-sum mutation was formatted");
    report = synthetic_report();
    --report.selected_rows_reconstructed;
    expect_rejected(
        [&] {
            static_cast<void>(
                census::format_report(report));
        },
        "published selected-row mutation was formatted");
    report = synthetic_report();
    --report.parent_scoring.scored_actions;
    expect_rejected(
        [&] {
            static_cast<void>(
                census::format_report(report));
        },
        "parent scoring cross-sum mutation was formatted");
    report = synthetic_report();
    --report.parent_scoring.sampled_worlds;
    expect_rejected(
        [&] {
            static_cast<void>(
                census::format_report(report));
        },
        "parent sampled-world mutation was formatted");
    report = synthetic_report();
    ++report.parent_scoring.terminal_evaluations;
    expect_rejected(
        [&] {
            static_cast<void>(
                census::format_report(report));
        },
        "parent terminal/bootstrap mutation was formatted");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "balanced block feasibility",
        test_balanced_block_feasibility);
    tests.run(
        "exact 32-game capacity",
        test_exact_32_game_capacity);
    tests.run(
        "category and distinct-game accounting",
        test_category_and_distinct_game_accounting);
    tests.run(
        "malformed roots fail closed",
        test_malformed_roots_fail_closed);
    tests.run(
        "aggregate-only format",
        test_aggregate_only_format);
    return tests.finish();
}
