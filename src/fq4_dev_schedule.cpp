#include "old_school/fq4_dev_schedule.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace old_school::fq4_dev_schedule {
namespace {

constexpr std::size_t kPlayerCount = 2;

static_assert(
    kPhysicalGamesPerBlock ==
    learned_iteration::kBalancedScheduleGames);

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        throw std::invalid_argument(
            "invalid FQ4 development deck");
    }
    return result;
}

template <typename Value>
void append_tab_field(std::string& output, Value value) {
    if (!output.empty() && output.back() != '\n') {
        output.push_back('\t');
    }
    output += std::to_string(value);
}

} // namespace

std::string_view split_name(Split split) {
    switch (split) {
    case Split::Fit:
        return "fit";
    case Split::Check:
        return "check";
    }
    throw std::invalid_argument(
        "invalid FQ4 development split");
}

std::uint64_t seed_base(Split split) {
    switch (split) {
    case Split::Fit:
        return kFitSeedBase;
    case Split::Check:
        return kCheckSeedBase;
    }
    throw std::invalid_argument(
        "invalid FQ4 development split");
}

std::size_t schedule_block_for_index(
    std::size_t global_schedule_index) {
    if (global_schedule_index >=
        kPhysicalGamesPerSplit) {
        throw std::out_of_range(
            "FQ4 global schedule index is out of range");
    }
    return
        global_schedule_index /
        kPhysicalGamesPerBlock;
}

std::size_t local_schedule_index(
    std::size_t global_schedule_index) {
    if (global_schedule_index >=
        kPhysicalGamesPerSplit) {
        throw std::out_of_range(
            "FQ4 global schedule index is out of range");
    }
    return
        global_schedule_index %
        kPhysicalGamesPerBlock;
}

std::size_t expected_schedule_bytes(Split split) {
    switch (split) {
    case Split::Fit:
        return kExpectedFitScheduleBytes;
    case Split::Check:
        return kExpectedCheckScheduleBytes;
    }
    throw std::invalid_argument(
        "invalid FQ4 development split");
}

std::string_view expected_schedule_sha256(
    Split split) {
    switch (split) {
    case Split::Fit:
        return kExpectedFitScheduleSha256;
    case Split::Check:
        return kExpectedCheckScheduleSha256;
    }
    throw std::invalid_argument(
        "invalid FQ4 development split");
}

std::vector<SourceGame> source_schedule(Split split) {
    const std::uint64_t split_seed = seed_base(split);
    std::vector<SourceGame> result;
    result.reserve(kPhysicalGamesPerSplit);
    for (std::size_t block = 0;
         block < kScheduleBlocks; ++block) {
        const auto balanced =
            learned_iteration::balanced_schedule(
                split_seed, kGenerationNamespace,
                block);
        for (const auto& game : balanced) {
            result.push_back({
                .split = split,
                .source_seed_base = split_seed,
                .schedule_block = block,
                .schedule_index =
                    block * kPhysicalGamesPerBlock +
                    game.schedule_index,
                .pairing_index = game.pairing_index,
                .seat_decks = game.seat_decks,
                .starting_player = game.starting_player,
                .game_seed = game.seed,
            });
        }
    }
    if (result.size() != kPhysicalGamesPerSplit) {
        throw std::logic_error(
            "FQ4 development schedule has wrong size");
    }
    return result;
}

std::string serialize_source_schedule(
    const std::vector<SourceGame>& schedule) {
    if (!audit_schedule_balance(schedule).exact) {
        throw std::invalid_argument(
            "cannot serialize malformed FQ4 development schedule");
    }
    std::string output;
    output.append(kScheduleSchema);
    output.push_back('\n');
    for (const SourceGame& game : schedule) {
        // Frozen eleven-field contract: split, seed base, generation
        // namespace, explicit balanced block, block-local schedule index,
        // global schedule index, pairing index, game seed, starting player,
        // seat-zero deck, and seat-one deck.
        append_tab_field(
            output,
            static_cast<std::uint64_t>(game.split));
        append_tab_field(output, game.source_seed_base);
        append_tab_field(output, kGenerationNamespace);
        append_tab_field(output, game.schedule_block);
        append_tab_field(
            output,
            local_schedule_index(
                game.schedule_index));
        append_tab_field(output, game.schedule_index);
        append_tab_field(output, game.pairing_index);
        append_tab_field(output, game.game_seed);
        append_tab_field(output, game.starting_player);
        append_tab_field(
            output,
            static_cast<std::uint64_t>(
                game.seat_decks[0]));
        append_tab_field(
            output,
            static_cast<std::uint64_t>(
                game.seat_decks[1]));
        output.push_back('\n');
    }
    return output;
}

std::string source_schedule_sha256(Split split) {
    return artifact_integrity::sha256_string(
        serialize_source_schedule(
            source_schedule(split)));
}

ScheduleBalance audit_schedule_balance(
    const std::vector<SourceGame>& schedule) {
    ScheduleBalance result{
        .physical_games = schedule.size(),
        .owner_perspectives =
            schedule.size() * kPlayerCount,
    };
    if (schedule.empty()) {
        return result;
    }

    const Split expected_split = schedule.front().split;
    std::uint64_t expected_seed = 0;
    try {
        expected_seed = seed_base(expected_split);
    } catch (const std::exception&) {
        return result;
    }
    bool rows_valid = true;
    std::set<std::size_t> schedule_indices;
    std::set<std::uint64_t> game_seeds;
    for (std::size_t index = 0;
         index < schedule.size(); ++index) {
        const SourceGame& game = schedule[index];
        const std::size_t expected_block =
            index / kPhysicalGamesPerBlock;
        const std::size_t expected_local =
            index % kPhysicalGamesPerBlock;
        if (expected_block >= kScheduleBlocks) {
            rows_valid = false;
            continue;
        }
        const auto expected_games =
            learned_iteration::balanced_schedule(
                expected_seed, kGenerationNamespace,
                expected_block);
        const auto& expected_game =
            expected_games[expected_local];
        rows_valid =
            rows_valid &&
            game.split == expected_split &&
            game.source_seed_base == expected_seed &&
            game.schedule_block == expected_block &&
            game.schedule_index == index &&
            game.pairing_index ==
                expected_game.pairing_index &&
            game.seat_decks ==
                expected_game.seat_decks &&
            game.starting_player ==
                expected_game.starting_player &&
            game.game_seed == expected_game.seed &&
            game.starting_player < kPlayerCount &&
            game.seat_decks[0] !=
                game.seat_decks[1] &&
            schedule_indices
                .insert(game.schedule_index)
                .second &&
            game_seeds.insert(game.game_seed).second;
        ++result
              .physical_games_by_block[
                  expected_block];
        for (std::size_t seat = 0;
             seat < kPlayerCount; ++seat) {
            std::size_t deck = 0;
            try {
                deck =
                    deck_index(
                        game.seat_decks[seat]);
            } catch (const std::exception&) {
                rows_valid = false;
                continue;
            }
            const std::size_t on_play =
                seat == game.starting_player ? 1 : 0;
            ++result.perspectives_by_deck[deck];
            ++result
                  .seat_play_quadrants[deck][seat]
                                      [on_play];
            if (seat == 0) {
                ++result.seat_zero_by_deck[deck];
            }
            if (on_play != 0) {
                ++result.on_play_by_deck[deck];
            }
        }
    }

    result.exact =
        rows_valid &&
        result.physical_games ==
            kPhysicalGamesPerSplit &&
        result.owner_perspectives ==
            kOwnerPerspectivesPerSplit &&
        std::all_of(
            result.physical_games_by_block.begin(),
            result.physical_games_by_block.end(),
            [](std::size_t count) {
                return count ==
                    kPhysicalGamesPerBlock;
            }) &&
        std::all_of(
            result.perspectives_by_deck.begin(),
            result.perspectives_by_deck.end(),
            [](std::size_t count) {
                return count == kPerspectivesPerDeck;
            }) &&
        std::all_of(
            result.seat_zero_by_deck.begin(),
            result.seat_zero_by_deck.end(),
            [](std::size_t count) {
                return count == kSeatZeroPerDeck;
            }) &&
        std::all_of(
            result.on_play_by_deck.begin(),
            result.on_play_by_deck.end(),
            [](std::size_t count) {
                return count == kOnPlayPerDeck;
            }) &&
        std::all_of(
            result.seat_play_quadrants.begin(),
            result.seat_play_quadrants.end(),
            [](const auto& by_seat) {
                return std::all_of(
                    by_seat.begin(), by_seat.end(),
                    [](const auto& by_play) {
                        return std::all_of(
                            by_play.begin(),
                            by_play.end(),
                            [](std::size_t count) {
                                return count ==
                                    kQuadrantPerDeck;
                            });
                    });
            });
    return result;
}

} // namespace old_school::fq4_dev_schedule
