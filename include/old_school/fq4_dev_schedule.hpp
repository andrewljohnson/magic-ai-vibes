#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_dev_schedule {

inline constexpr std::uint64_t kFitSeedBase =
    14991670039259730681ULL;
inline constexpr std::uint64_t kCheckSeedBase =
    2769767503634781211ULL;
inline constexpr std::uint64_t kGenerationNamespace =
    0x465134445632474eULL;
inline constexpr std::size_t kScheduleBlocks = 4;
inline constexpr std::size_t kPhysicalGamesPerBlock = 40;
inline constexpr std::size_t kPhysicalGamesPerSplit =
    kScheduleBlocks * kPhysicalGamesPerBlock;
inline constexpr std::size_t kOwnerPerspectivesPerSplit = 320;
inline constexpr std::size_t kPerspectivesPerDeck = 64;
inline constexpr std::size_t kSeatZeroPerDeck = 32;
inline constexpr std::size_t kOnPlayPerDeck = 32;
inline constexpr std::size_t kQuadrantPerDeck = 16;
inline constexpr std::string_view kScheduleSchema =
    "old-school-fq4-priority-dev-schedule-v2";
inline constexpr std::size_t kExpectedFitScheduleBytes = 12747;
inline constexpr std::size_t kExpectedCheckScheduleBytes = 12592;
inline constexpr std::string_view kExpectedFitScheduleSha256 =
    "c1f1e7cb4f8f9619ba951d1a1c9c199b8e2e01850b9b0987c9169c0b8bffab0b";
inline constexpr std::string_view kExpectedCheckScheduleSha256 =
    "f5e021c32287aba9286e5f32250ac4f8980e2b91c4a9033679fb21ce1b9f739b";

enum class Split : std::uint8_t {
    Fit = 0,
    Check = 1,
};

struct SourceGame {
    Split split = Split::Fit;
    std::uint64_t source_seed_base = 0;
    std::size_t schedule_block = 0;
    // Global index in the four-block concatenation, always 0..159.
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
    std::array<std::size_t, kScheduleBlocks>
        physical_games_by_block{};
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
std::size_t schedule_block_for_index(
    std::size_t global_schedule_index);
std::size_t local_schedule_index(
    std::size_t global_schedule_index);
std::size_t expected_schedule_bytes(Split split);
std::string_view expected_schedule_sha256(Split split);
std::vector<SourceGame> source_schedule(Split split);
std::string serialize_source_schedule(
    const std::vector<SourceGame>& schedule);
std::string source_schedule_sha256(Split split);
ScheduleBalance audit_schedule_balance(
    const std::vector<SourceGame>& schedule);

} // namespace old_school::fq4_dev_schedule
