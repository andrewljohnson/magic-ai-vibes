#include "old_school/fq4_dev_coverage_census.hpp"

#include "old_school/fq4_dev_bundle.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::fq4_dev_coverage_census {
namespace {

namespace bundle = fq4_dev_bundle;
namespace generator = fq4_dev_generator;
namespace schedule_data = fq4_dev_schedule;

constexpr std::array<std::string_view, bundle::kDeckCount>
    kDeckNames{
        "Green",
        "Red",
        "Blue",
        "White",
        "RU_Aggro",
    };
constexpr std::array<std::string_view, kOwnerQuadrantCount>
    kQuadrantNames{
        "seat0_play",
        "seat0_draw",
        "seat1_play",
        "seat1_draw",
    };

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(
        "FQ4 DEV4 coverage census: " +
        std::string(message));
}

std::size_t split_index(bundle::Split split) {
    switch (split) {
    case bundle::Split::Fit:
        return 0;
    case bundle::Split::Check:
        return 1;
    }
    fail("root split is invalid");
}

std::string_view split_name(std::size_t split) {
    if (split == 0) {
        return "fit";
    }
    if (split == 1) {
        return "check";
    }
    fail("split index is invalid");
}

bool all_zero(const bundle::Hash256& digest) {
    return std::all_of(
        digest.begin(), digest.end(),
        [](std::uint8_t byte) {
            return byte == 0;
        });
}

bool parent_scoring_consistent(
    const bundle::ScoreAccounting& accounting,
    std::size_t selected_rows) {
    constexpr std::uint64_t kRolloutsPerWorld = 1;
    return
        accounting.score_calls == selected_rows &&
        accounting.scored_actions >=
            accounting.score_calls &&
        accounting.scored_actions <=
            selected_rows *
                bundle::kMaximumActions &&
        accounting.sampled_worlds ==
            accounting.score_calls *
                bundle::kWorldCount &&
        accounting.rollout_evaluations ==
            accounting.scored_actions *
                bundle::kWorldCount *
                kRolloutsPerWorld &&
        accounting.terminal_evaluations +
                accounting.bootstrap_evaluations ==
            accounting.rollout_evaluations;
}

struct MutableCount {
    std::size_t roots = 0;
    std::size_t options = 0;
    std::set<bundle::Hash256> games;
};

void add(
    MutableCount& count,
    const generator::CoverageRootObservation& root) {
    ++count.roots;
    count.options += root.option_count;
    count.games.insert(root.physical_game_sha256);
}

Count freeze(const MutableCount& count) {
    return {
        .roots = count.roots,
        .options = count.options,
        .distinct_games = count.games.size(),
    };
}

struct MutableDeck {
    MutableCount retained;
    std::array<MutableCount, 2> dominance_positive;
    std::array<MutableCount, 2> selected_background;
    std::array<MutableCount, 2>
        unselected_dominance_negative;
    MutableCount eligible_all;
    MutableCount eligible_main;
    MutableCount eligible_other;
    std::array<
        std::array<
            std::array<
                std::set<bundle::Hash256>,
                kOwnerQuadrantCount>,
            bundle::kDeckCount>,
        kScheduleBlockCount>
        eligible_games;
};

using PhysicalGameKey =
    std::tuple<std::size_t, std::size_t, bundle::Hash256>;
using PhysicalGameContext =
    std::tuple<std::size_t, std::size_t, std::size_t>;

bool valid_stack_feature(
    const generator::CoverageRootObservation& root) {
    if (root.stack_feature_bits.size() !=
            root.option_count ||
        root.stack_feature_bits.empty()) {
        return false;
    }
    const double expected =
        static_cast<double>(
            root.public_stack_size) /
        static_cast<double>(
            kStackSizeEncodingDenominator);
    if (!std::isfinite(expected)) {
        return false;
    }
    const std::uint64_t expected_bits =
        std::bit_cast<std::uint64_t>(expected);
    if (!std::all_of(
            root.stack_feature_bits.begin(),
            root.stack_feature_bits.end(),
            [expected_bits](std::uint64_t bits) {
                return bits == expected_bits;
            })) {
        return false;
    }
    const double decoded =
        std::bit_cast<double>(expected_bits);
    const double scaled =
        decoded *
        static_cast<double>(
            kStackSizeEncodingDenominator);
    double integer = 0.0;
    return
        std::isfinite(decoded) &&
        !std::signbit(decoded) &&
        std::isfinite(scaled) &&
        std::modf(scaled, &integer) == 0.0 &&
        integer <
            static_cast<double>(
                std::numeric_limits<std::size_t>::max()) &&
        static_cast<std::size_t>(integer) ==
            root.public_stack_size;
}

bool main_phase(TurnPhase phase) {
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::SecondMain;
}

bool recurse_balanced_rows(
    const std::array<
        std::array<std::size_t, kOwnerQuadrantCount>,
        bundle::kDeckCount>& capacities,
    const std::array<std::size_t, bundle::kDeckCount>&
        opponents,
    std::size_t opponent_count,
    std::size_t row,
    std::array<std::size_t, kOwnerQuadrantCount>&
        remaining) {
    if (row == opponent_count) {
        return std::all_of(
            remaining.begin(), remaining.end(),
            [](std::size_t value) {
                return value == 0;
            });
    }
    const std::size_t opponent = opponents[row];
    for (std::size_t first = 0;
         first <= kGamesPerOpponent; ++first) {
        for (std::size_t second = 0;
             second <= kGamesPerOpponent - first;
             ++second) {
            for (std::size_t third = 0;
                 third <=
                     kGamesPerOpponent -
                         first - second;
                 ++third) {
                const std::size_t fourth =
                    kGamesPerOpponent -
                    first - second - third;
                const std::array<std::size_t, 4>
                    allocation{
                        first, second, third, fourth};
                bool admissible = true;
                for (std::size_t quadrant = 0;
                     quadrant < kOwnerQuadrantCount;
                     ++quadrant) {
                    if (allocation[quadrant] >
                            capacities[opponent][quadrant] ||
                        allocation[quadrant] >
                            remaining[quadrant]) {
                        admissible = false;
                    }
                }
                if (!admissible) {
                    continue;
                }
                for (std::size_t quadrant = 0;
                     quadrant < kOwnerQuadrantCount;
                     ++quadrant) {
                    remaining[quadrant] -=
                        allocation[quadrant];
                }
                if (recurse_balanced_rows(
                        capacities, opponents,
                        opponent_count, row + 1,
                        remaining)) {
                    return true;
                }
                for (std::size_t quadrant = 0;
                     quadrant < kOwnerQuadrantCount;
                     ++quadrant) {
                    remaining[quadrant] +=
                        allocation[quadrant];
                }
            }
        }
    }
    return false;
}

bool census_consistent(const CoverageCensus& census) {
    std::size_t retained_rows = 0;
    std::size_t retained_options = 0;
    std::size_t selected_background_rows = 0;
    bool capacity = true;
    for (const SplitCensus* split :
         {&census.fit, &census.check}) {
        for (std::size_t owner = 0;
             owner < bundle::kDeckCount; ++owner) {
            const DeckCensus& deck =
                split->decks[owner];
            retained_rows += deck.retained.roots;
            retained_options += deck.retained.options;
            const std::size_t positive_roots =
                deck.dominance_positive.empty.roots +
                deck.dominance_positive.active.roots;
            const std::size_t positive_options =
                deck.dominance_positive.empty.options +
                deck.dominance_positive.active.options;
            const std::size_t background_roots =
                deck.selected_background.empty.roots +
                deck.selected_background.active.roots;
            const std::size_t background_options =
                deck.selected_background.empty.options +
                deck.selected_background.active.options;
            const std::size_t negative_roots =
                deck.unselected_dominance_negative
                        .empty.roots +
                deck.unselected_dominance_negative
                        .active.roots;
            const std::size_t negative_options =
                deck.unselected_dominance_negative
                        .empty.options +
                deck.unselected_dominance_negative
                        .active.options;
            if (deck.retained.roots !=
                    positive_roots +
                        background_roots +
                        negative_roots ||
                deck.retained.options !=
                    positive_options +
                        background_options +
                        negative_options ||
                deck.eligible_stack_empty.all.roots !=
                    deck.unselected_dominance_negative
                        .empty.roots ||
                deck.eligible_stack_empty.all.options !=
                    deck.unselected_dominance_negative
                        .empty.options ||
                deck.eligible_stack_empty.all.roots !=
                    deck.eligible_stack_empty
                            .first_or_second_main.roots +
                        deck.eligible_stack_empty.other.roots ||
                deck.eligible_stack_empty.all.options !=
                    deck.eligible_stack_empty
                            .first_or_second_main.options +
                        deck.eligible_stack_empty.other.options) {
                return false;
            }
            selected_background_rows += background_roots;
            std::size_t block_games = 0;
            bool deck_capacity = true;
            for (const BlockEligibility& block :
                 deck.eligible_blocks) {
                std::size_t matrix_total = 0;
                for (std::size_t opponent = 0;
                     opponent < bundle::kDeckCount;
                     ++opponent) {
                    for (std::size_t quadrant = 0;
                         quadrant < kOwnerQuadrantCount;
                         ++quadrant) {
                        matrix_total +=
                            block
                                .games_by_opponent_quadrant[
                                    opponent][quadrant];
                    }
                }
                if (matrix_total !=
                        block.distinct_games ||
                    block.balanced_eight_feasible !=
                        balanced_block_feasible(
                            owner,
                            block
                                .games_by_opponent_quadrant)) {
                    return false;
                }
                block_games += block.distinct_games;
                deck_capacity =
                    deck_capacity &&
                    block.balanced_eight_feasible;
            }
            if (block_games !=
                    deck.eligible_stack_empty.all
                        .distinct_games ||
                deck.capacity_met != deck_capacity) {
                return false;
            }
            capacity = capacity && deck_capacity;
        }
    }
    return
        retained_rows == census.retained_rows &&
        retained_options == census.retained_options &&
        selected_background_rows ==
            census.selected_background_rows &&
        census.selected_rows ==
            census.selected_positive_rows +
                census.selected_background_rows &&
        census.selected_positive_rows <=
            census.retained_rows &&
        census.selected_rows <= census.retained_rows &&
        census.action_invariant_stack_rows ==
            census.retained_rows &&
        census.exact_public_stack_rows ==
            census.retained_rows &&
        census.valid_public_phase_rows ==
            census.retained_rows &&
        census.capacity_licensed == capacity;
}

void write_count(
    std::ostringstream& output,
    std::string_view split,
    std::string_view deck,
    std::string_view category,
    const Count& count) {
    output
        << "coverage split=" << split
        << " deck=" << deck
        << " category=" << category
        << " roots=" << count.roots
        << " options=" << count.options
        << " distinct_games="
        << count.distinct_games
        << '\n';
}

void write_stack(
    std::ostringstream& output,
    std::string_view split,
    std::string_view deck,
    std::string_view category,
    const StackCounts& counts) {
    write_count(
        output, split, deck,
        std::string(category) + "_stack_empty",
        counts.empty);
    write_count(
        output, split, deck,
        std::string(category) + "_stack_active",
        counts.active);
}

} // namespace

bool balanced_block_feasible(
    std::size_t owner_deck,
    const std::array<
        std::array<std::size_t, kOwnerQuadrantCount>,
        bundle::kDeckCount>&
        games_by_opponent_quadrant) {
    if (owner_deck >= bundle::kDeckCount ||
        std::any_of(
            games_by_opponent_quadrant.begin(),
            games_by_opponent_quadrant.end(),
            [](const auto& opponent) {
                return std::any_of(
                    opponent.begin(), opponent.end(),
                    [](std::size_t count) {
                        return count > 1;
                    });
            }) ||
        std::any_of(
            games_by_opponent_quadrant[owner_deck].begin(),
            games_by_opponent_quadrant[owner_deck].end(),
            [](std::size_t count) {
                return count != 0;
            })) {
        return false;
    }
    std::array<std::size_t, bundle::kDeckCount>
        opponents{};
    std::size_t opponent_count = 0;
    for (std::size_t opponent = 0;
         opponent < bundle::kDeckCount; ++opponent) {
        if (opponent != owner_deck) {
            opponents[opponent_count++] = opponent;
        }
    }
    if (opponent_count !=
        bundle::kDeckCount - 1) {
        return false;
    }
    std::array<std::size_t, kOwnerQuadrantCount>
        remaining{};
    remaining.fill(kGamesPerOwnerQuadrant);
    return recurse_balanced_rows(
        games_by_opponent_quadrant,
        opponents, opponent_count, 0,
        remaining);
}

CoverageCensus measure(
    const std::vector<
        generator::CoverageRootObservation>& roots) {
    constexpr std::uint8_t kPositive =
        static_cast<std::uint8_t>(
            bundle::Role::DominancePositive);
    constexpr std::uint8_t kBackground =
        static_cast<std::uint8_t>(
            bundle::Role::BackgroundControl);
    constexpr std::uint8_t kKnownRoles =
        kPositive | kBackground;

    std::array<
        std::array<MutableDeck, bundle::kDeckCount>,
        2>
        mutable_splits;
    std::set<bundle::Hash256> stable_roots;
    std::map<PhysicalGameKey, PhysicalGameContext>
        physical_contexts;
    CoverageCensus result;

    for (const generator::CoverageRootObservation& root :
         roots) {
        const std::size_t split =
            split_index(root.split);
        if (root.schedule_block >=
                kScheduleBlockCount ||
            root.owner_deck >= bundle::kDeckCount ||
            root.opponent_deck >=
                bundle::kDeckCount ||
            root.owner_deck ==
                root.opponent_deck ||
            root.owner_seat >= 2 ||
            root.option_count == 0 ||
            root.option_count >
                bundle::kMaximumActions ||
            all_zero(root.stable_root_id) ||
            all_zero(root.physical_game_sha256) ||
            !stable_roots.insert(
                root.stable_root_id).second) {
            fail("root identity or schedule context is invalid");
        }
        const std::size_t phase =
            static_cast<std::size_t>(root.phase);
        if (phase >= kPhaseCount) {
            fail("root public phase is invalid");
        }
        if (!valid_stack_feature(root)) {
            fail(
                "feature 20 is not action-invariant exact public stack size");
        }
        const bool selected_positive =
            (root.selected_roles & kPositive) != 0;
        const bool selected_background =
            (root.selected_roles &
             kBackground) != 0;
        if ((root.selected_roles & ~kKnownRoles) != 0 ||
            (selected_positive &&
             selected_background) ||
            (root.selected_roles != 0 &&
             selected_positive !=
                 root.dominance_positive)) {
            fail("selected role is invalid");
        }
        const std::size_t quadrant =
            static_cast<std::size_t>(
                root.owner_seat) *
                2 +
            (root.owner_on_play ? 0 : 1);
        const PhysicalGameKey game_key{
            split, root.owner_deck,
            root.physical_game_sha256};
        const PhysicalGameContext game_context{
            root.schedule_block,
            root.opponent_deck,
            quadrant};
        const auto [known, inserted] =
            physical_contexts.emplace(
                game_key, game_context);
        if (!inserted &&
            known->second != game_context) {
            fail(
                "one physical game has conflicting public strata");
        }

        MutableDeck& deck =
            mutable_splits[split][root.owner_deck];
        add(deck.retained, root);
        const std::size_t stack =
            root.public_stack_size == 0 ? 0 : 1;
        if (root.dominance_positive) {
            add(
                deck.dominance_positive[stack],
                root);
        } else if (selected_background) {
            add(
                deck.selected_background[stack],
                root);
            ++result.selected_background_rows;
        } else {
            add(
                deck
                    .unselected_dominance_negative[
                        stack],
                root);
            if (stack == 0) {
                add(deck.eligible_all, root);
                add(
                    main_phase(root.phase)
                        ? deck.eligible_main
                        : deck.eligible_other,
                    root);
                deck.eligible_games[
                    root.schedule_block][
                    root.opponent_deck][quadrant]
                    .insert(
                        root.physical_game_sha256);
            }
        }
        if (root.selected_roles != 0) {
            ++result.selected_rows;
            if (selected_positive) {
                ++result.selected_positive_rows;
            }
        }
        ++result.retained_rows;
        result.retained_options +=
            root.option_count;
        ++result.action_invariant_stack_rows;
        ++result.exact_public_stack_rows;
        ++result.valid_public_phase_rows;
    }

    result.capacity_licensed = true;
    for (std::size_t split = 0;
         split < mutable_splits.size(); ++split) {
        SplitCensus& frozen_split =
            split == 0 ? result.fit : result.check;
        for (std::size_t owner = 0;
             owner < bundle::kDeckCount; ++owner) {
            const MutableDeck& source =
                mutable_splits[split][owner];
            DeckCensus& target =
                frozen_split.decks[owner];
            target.retained = freeze(source.retained);
            target.dominance_positive = {
                .empty =
                    freeze(
                        source
                            .dominance_positive[0]),
                .active =
                    freeze(
                        source
                            .dominance_positive[1]),
            };
            target.selected_background = {
                .empty =
                    freeze(
                        source
                            .selected_background[0]),
                .active =
                    freeze(
                        source
                            .selected_background[1]),
            };
            target.unselected_dominance_negative = {
                .empty =
                    freeze(
                        source
                            .unselected_dominance_negative[
                                0]),
                .active =
                    freeze(
                        source
                            .unselected_dominance_negative[
                                1]),
            };
            target.eligible_stack_empty = {
                .all = freeze(source.eligible_all),
                .first_or_second_main =
                    freeze(source.eligible_main),
                .other =
                    freeze(source.eligible_other),
            };
            target.capacity_met = true;
            for (std::size_t block = 0;
                 block < kScheduleBlockCount;
                 ++block) {
                BlockEligibility& frozen =
                    target.eligible_blocks[block];
                for (std::size_t opponent = 0;
                     opponent < bundle::kDeckCount;
                     ++opponent) {
                    for (std::size_t quadrant = 0;
                         quadrant < kOwnerQuadrantCount;
                         ++quadrant) {
                        frozen
                            .games_by_opponent_quadrant[
                                opponent][quadrant] =
                            source.eligible_games[
                                block][opponent][
                                quadrant]
                                .size();
                        if (frozen
                                .games_by_opponent_quadrant[
                                    opponent][quadrant] >
                            1) {
                            fail(
                                "frozen schedule cell has multiple physical games");
                        }
                        frozen.distinct_games +=
                            frozen
                                .games_by_opponent_quadrant[
                                    opponent][quadrant];
                    }
                }
                frozen.balanced_eight_feasible =
                    balanced_block_feasible(
                        owner,
                        frozen
                            .games_by_opponent_quadrant);
                target.capacity_met =
                    target.capacity_met &&
                    frozen.balanced_eight_feasible;
            }
            result.capacity_licensed =
                result.capacity_licensed &&
                target.capacity_met;
        }
    }
    if (!census_consistent(result)) {
        fail("aggregate accounting is inconsistent");
    }
    return result;
}

bool Report::exact() const {
    return
        source_games_reconstructed ==
            2 *
                schedule_data::
                    kPhysicalGamesPerSplit &&
        selected_rows_reconstructed ==
            census.selected_rows &&
        selected_rows_reconstructed ==
            kPublishedSelectedRows &&
        census.selected_positive_rows ==
            kPublishedSelectedPositiveRows &&
        census.selected_background_rows ==
            kPublishedSelectedBackgroundRows &&
        parent_scoring_consistent(
            parent_scoring,
            selected_rows_reconstructed) &&
        parent_artifact_sha256 ==
            bundle::kParentArtifactSha256 &&
        bundle_bytes ==
            bundle::kPublishedArtifactBytes &&
        bundle_sha256 ==
            bundle::kPublishedArtifactSha256 &&
        schedules_exact &&
        scientific_manifest_exact &&
        fit_census_exact &&
        check_census_exact &&
        fit_selected_exact &&
        check_selected_exact &&
        fit_manifest_exact &&
        check_manifest_exact &&
        parent_immutable &&
        bundle_immutable &&
        parent_models_loaded == 1 &&
        fits == 0 &&
        candidate_rollout_evaluations == 0 &&
        gameplay_evaluation_seeds == 0 &&
        census_consistent(census);
}

Report run_fixed() {
    const generator::CoverageReconstruction reconstruction =
        generator::reconstruct_published_coverage_once();
    if (!reconstruction.exact()) {
        fail("fixed reconstruction evidence is incomplete");
    }
    return {
        .census = measure(reconstruction.roots),
        .source_games_reconstructed =
            reconstruction
                .source_games_reconstructed,
        .selected_rows_reconstructed =
            reconstruction
                .selected_rows_reconstructed,
        .parent_scoring =
            reconstruction.parent_scoring,
        .parent_artifact_sha256 =
            reconstruction
                .parent_artifact_sha256,
        .bundle_bytes =
            reconstruction.bundle_bytes,
        .bundle_sha256 =
            reconstruction.bundle_sha256,
        .schedules_exact =
            reconstruction.schedules_exact,
        .scientific_manifest_exact =
            reconstruction
                .scientific_manifest_exact,
        .fit_census_exact =
            reconstruction.fit_census_exact,
        .check_census_exact =
            reconstruction.check_census_exact,
        .fit_selected_exact =
            reconstruction.fit_selected_exact,
        .check_selected_exact =
            reconstruction.check_selected_exact,
        .fit_manifest_exact =
            reconstruction.fit_manifest_exact,
        .check_manifest_exact =
            reconstruction.check_manifest_exact,
        .parent_immutable =
            reconstruction.parent_immutable,
        .bundle_immutable =
            reconstruction.bundle_immutable,
        .parent_models_loaded =
            reconstruction.parent_models_loaded,
        .fits = reconstruction.fits,
        .candidate_rollout_evaluations =
            reconstruction
                .candidate_rollout_evaluations,
        .gameplay_evaluation_seeds =
            reconstruction
                .gameplay_evaluation_seeds,
    };
}

std::string format_report(const Report& report) {
    if (!report.exact()) {
        throw std::invalid_argument(
            "invalid FQ4 DEV4 coverage-census report");
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output
        << "schema=" << kSchema
        << " bundle_schema="
        << bundle::kBundleSchema
        << " bundle_bytes="
        << report.bundle_bytes
        << " bundle_sha256="
        << report.bundle_sha256
        << " parent_sha256="
        << report.parent_artifact_sha256
        << '\n'
        << "reconstruction source_games="
        << report.source_games_reconstructed
        << " retained_rows="
        << report.census.retained_rows
        << " retained_options="
        << report.census.retained_options
        << " selected_rows="
        << report.selected_rows_reconstructed
        << " selected_positive_rows="
        << report.census.selected_positive_rows
        << " selected_background_rows="
        << report.census.selected_background_rows
        << " parent_score_calls="
        << report.parent_scoring.score_calls
        << " parent_scored_actions="
        << report.parent_scoring.scored_actions
        << " parent_sampled_worlds="
        << report.parent_scoring.sampled_worlds
        << " parent_rollout_evaluations="
        << report.parent_scoring
               .rollout_evaluations
        << " parent_terminal_evaluations="
        << report.parent_scoring
               .terminal_evaluations
        << " parent_bootstrap_evaluations="
        << report.parent_scoring
               .bootstrap_evaluations
        << " schedules_exact=1"
           " scientific_manifest_exact=1"
           " fit_census_exact=1"
           " check_census_exact=1"
           " fit_selected_exact=1"
           " check_selected_exact=1"
           " fit_manifest_exact=1"
           " check_manifest_exact=1"
           " parent_immutable=1"
           " bundle_immutable=1\n";

    const std::array<const SplitCensus*, 2> splits{
        &report.census.fit,
        &report.census.check,
    };
    for (std::size_t split = 0;
         split < splits.size(); ++split) {
        for (std::size_t owner = 0;
             owner < bundle::kDeckCount; ++owner) {
            const DeckCensus& deck =
                splits[split]->decks[owner];
            const std::string_view split_label =
                split_name(split);
            const std::string_view owner_label =
                kDeckNames[owner];
            write_count(
                output, split_label, owner_label,
                "retained", deck.retained);
            write_stack(
                output, split_label, owner_label,
                "dominance_positive",
                deck.dominance_positive);
            write_stack(
                output, split_label, owner_label,
                "selected_background",
                deck.selected_background);
            write_stack(
                output, split_label, owner_label,
                "unselected_dominance_negative",
                deck
                    .unselected_dominance_negative);
            write_count(
                output, split_label, owner_label,
                "eligible_stack_empty_all",
                deck.eligible_stack_empty.all);
            write_count(
                output, split_label, owner_label,
                "eligible_stack_empty_main",
                deck.eligible_stack_empty
                    .first_or_second_main);
            write_count(
                output, split_label, owner_label,
                "eligible_stack_empty_other_phase",
                deck.eligible_stack_empty.other);
            for (std::size_t block = 0;
                 block < kScheduleBlockCount;
                 ++block) {
                const BlockEligibility& eligibility =
                    deck.eligible_blocks[block];
                output
                    << "eligibility split="
                    << split_label
                    << " deck=" << owner_label
                    << " block=" << block
                    << " distinct_games="
                    << eligibility.distinct_games;
                for (std::size_t opponent = 0;
                     opponent < bundle::kDeckCount;
                     ++opponent) {
                    if (opponent == owner) {
                        continue;
                    }
                    for (std::size_t quadrant = 0;
                         quadrant <
                             kOwnerQuadrantCount;
                         ++quadrant) {
                        output
                            << ' '
                            << kDeckNames[opponent]
                            << '.'
                            << kQuadrantNames[quadrant]
                            << '='
                            << eligibility
                                   .games_by_opponent_quadrant[
                                       opponent][quadrant];
                    }
                }
                output
                    << " balanced_eight_feasible="
                    << (eligibility
                                .balanced_eight_feasible
                            ? 1
                            : 0)
                    << '\n';
            }
            output
                << "capacity split="
                << split_label
                << " deck=" << owner_label
                << " blocks_feasible="
                << (deck.capacity_met
                        ? kScheduleBlockCount
                        : std::count_if(
                              deck.eligible_blocks.begin(),
                              deck.eligible_blocks.end(),
                              [](const BlockEligibility& block) {
                                  return block
                                      .balanced_eight_feasible;
                              }))
                << " blocks_required="
                << kScheduleBlockCount
                << " games_if_licensed="
                << kScheduleBlockCount *
                       kGamesPerBalancedBlock
                << " capacity_met="
                << (deck.capacity_met ? 1 : 0)
                << '\n';
        }
    }
    output
        << "contracts stack_feature_index="
        << kStackSizeFeatureIndex
        << " stack_encoding_denominator="
        << kStackSizeEncodingDenominator
        << " action_invariant_rows="
        << report.census
               .action_invariant_stack_rows
        << " exact_public_stack_rows="
        << report.census.exact_public_stack_rows
        << " valid_public_phase_rows="
        << report.census.valid_public_phase_rows
        << " phase_count=" << kPhaseCount
        << '\n'
        << "accounting parent_models_loaded="
        << report.parent_models_loaded
        << " parent_score_calls="
        << report.parent_scoring.score_calls
        << " parent_scored_actions="
        << report.parent_scoring.scored_actions
        << " parent_sampled_worlds="
        << report.parent_scoring.sampled_worlds
        << " parent_rollout_evaluations="
        << report.parent_scoring
               .rollout_evaluations
        << " parent_terminal_evaluations="
        << report.parent_scoring
               .terminal_evaluations
        << " parent_bootstrap_evaluations="
        << report.parent_scoring
               .bootstrap_evaluations
        << " fits=" << report.fits
        << " candidate_rollouts="
        << report.candidate_rollout_evaluations
        << " gameplay_evaluation_seeds="
        << report.gameplay_evaluation_seeds
        << '\n'
        << "result=PASS capacity_licensed="
        << (report.census.capacity_licensed ? 1 : 0)
        << '\n';
    return output.str();
}

} // namespace old_school::fq4_dev_coverage_census
