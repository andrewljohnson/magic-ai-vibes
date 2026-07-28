#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_dev_schedule {

inline constexpr std::uint64_t kFitSeedBase = 202607280210ULL;
inline constexpr std::uint64_t kCheckSeedBase = 202607280211ULL;
inline constexpr std::uint64_t kGenerationNamespace =
    0x46513444455631ULL;
inline constexpr std::uint64_t kScheduleBlock = 0;
inline constexpr std::size_t kPhysicalGamesPerSplit = 40;
inline constexpr std::size_t kOwnerPerspectivesPerSplit = 80;
inline constexpr std::size_t kPerspectivesPerDeck = 16;
inline constexpr std::size_t kSeatZeroPerDeck = 8;
inline constexpr std::size_t kOnPlayPerDeck = 8;
inline constexpr std::size_t kQuadrantPerDeck = 4;
inline constexpr std::string_view kScheduleSchema =
    "old-school-fq4-priority-dev-schedule-v1";
inline constexpr std::size_t kExpectedFitScheduleBytes = 2686;
inline constexpr std::size_t kExpectedCheckScheduleBytes = 2689;
inline constexpr std::string_view kExpectedFitScheduleSha256 =
    "9b23969c646f75c1ee9d70d8359fbab9beb6a9f2b658e7f93ee51eaff5a9d6f2";
inline constexpr std::string_view kExpectedCheckScheduleSha256 =
    "e8a3267f0cfedf6a4c38a0a1345c15bbfcb48cb109c38f9d33c2547f0b41be6a";

enum class Split : std::uint8_t {
    Fit = 0,
    Check = 1,
};

struct SourceGame {
    Split split = Split::Fit;
    std::uint64_t source_seed_base = 0;
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::array<DeckId, 2> seat_decks{
        DeckId::Green,
        DeckId::Red,
    };
    std::size_t starting_player = 0;
    std::uint64_t game_seed = 0;

    bool operator==(const SourceGame&) const = default;
};

struct ScheduleBalance {
    std::size_t physical_games = 0;
    std::size_t owner_perspectives = 0;
    std::array<std::size_t, kDeckCount> perspectives_by_deck{};
    std::array<std::size_t, kDeckCount> seat_zero_by_deck{};
    std::array<std::size_t, kDeckCount> on_play_by_deck{};
    std::array<std::array<std::array<std::size_t, 2>, 2>,
               kDeckCount>
        seat_play_quadrants{};
    bool exact = false;

    bool operator==(const ScheduleBalance&) const = default;
};

std::string_view split_name(Split split);
std::uint64_t seed_base(Split split);
std::size_t expected_schedule_bytes(Split split);
std::string_view expected_schedule_sha256(Split split);
std::vector<SourceGame> source_schedule(Split split);
std::string serialize_source_schedule(
    const std::vector<SourceGame>& schedule);
std::string source_schedule_sha256(Split split);
ScheduleBalance audit_schedule_balance(
    const std::vector<SourceGame>& schedule);

} // namespace old_school::fq4_dev_schedule
