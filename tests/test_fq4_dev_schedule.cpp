#include "old_school/fq4_dev_schedule.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace schedule = old_school::fq4_dev_schedule;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
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
    throw std::runtime_error(std::string(message));
}

std::uint8_t hex_nibble(char character) {
    if (character >= '0' && character <= '9') {
        return static_cast<std::uint8_t>(
            character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return static_cast<std::uint8_t>(
            character - 'a' + 10);
    }
    throw std::runtime_error(
        "seed derivation digest is not lowercase hex");
}

std::uint64_t first_u64_big_endian(
    std::string_view hexadecimal) {
    if (hexadecimal.size() != 64) {
        throw std::runtime_error(
            "seed derivation digest has wrong length");
    }
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 16;
         ++index) {
        result =
            (result << 4U) |
            hex_nibble(hexadecimal[index]);
    }
    return result;
}

void test_seed_and_namespace_literals() {
    constexpr std::string_view kCommit =
        "5784b10ef60ae884df1e3ecd9bcb8dd38108ffc4";
    const std::string fit_bytes =
        "old-school-fq4-priority-dev1-source-seed-v2\n"
        "fit\n" +
        std::string(kCommit) + "\n";
    const std::string check_bytes =
        "old-school-fq4-priority-dev1-source-seed-v2\n"
        "check\n" +
        std::string(kCommit) + "\n";
    const std::string fit_digest =
        old_school::artifact_integrity::
            sha256_string(fit_bytes);
    const std::string check_digest =
        old_school::artifact_integrity::
            sha256_string(check_bytes);
    expect(
        fit_digest ==
                "d00d1c78aff152f98b31ad6fcf7d5f69"
                "f1298b55061930b21377f5a76aeb8e02" &&
            check_digest ==
                "267030dea492541ba3f88ebf705800e5"
                "838b9d929b6eb580b1b42091f1b1f9ae",
        "DEV1 seed derivation digest drifted");
    expect(
        first_u64_big_endian(fit_digest) ==
                schedule::kFitSeedBase &&
            first_u64_big_endian(check_digest) ==
                schedule::kCheckSeedBase,
        "DEV1 seed base is not the first big-endian digest word");

    std::string generation_name;
    for (int shift = 56; shift >= 0; shift -= 8) {
        generation_name.push_back(
            static_cast<char>(
                (schedule::kGenerationNamespace >>
                 static_cast<unsigned>(shift)) &
                0xffU));
    }
    expect(
        generation_name == "FQ4DV2GN",
        "DEV1 generation namespace is not its frozen ASCII value");
}

void test_each_schedule_is_deterministic_and_balanced() {
    for (const schedule::Split split :
         {schedule::Split::Fit,
          schedule::Split::Check}) {
        const auto first =
            schedule::source_schedule(split);
        const auto second =
            schedule::source_schedule(split);
        expect(first == second,
               "development schedule is not deterministic");
        expect(
            first.size() ==
                schedule::kPhysicalGamesPerSplit,
            "development schedule has wrong game count");
        const auto balance =
            schedule::audit_schedule_balance(first);
        expect(balance.exact,
               "development schedule is not exactly balanced");
        expect(
            balance.owner_perspectives ==
                schedule::kOwnerPerspectivesPerSplit,
            "development schedule has wrong perspective count");
        for (const std::size_t count :
             balance.physical_games_by_block) {
            expect(
                count ==
                    schedule::kPhysicalGamesPerBlock,
                "development schedule has wrong block count");
        }
        for (std::size_t index = 0;
             index < first.size(); ++index) {
            const auto& game = first[index];
            const std::size_t block =
                schedule::schedule_block_for_index(index);
            const std::size_t local =
                schedule::local_schedule_index(index);
            const auto expected =
                old_school::learned_iteration::
                    balanced_schedule(
                        schedule::seed_base(split),
                        schedule::kGenerationNamespace,
                        block);
            expect(
                game.schedule_index == index &&
                    game.schedule_block == block &&
                    expected[local].schedule_index ==
                        local &&
                    game.pairing_index ==
                        expected[local].pairing_index &&
                    game.seat_decks ==
                        expected[local].seat_decks &&
                    game.starting_player ==
                        expected[local].starting_player &&
                    game.game_seed ==
                        expected[local].seed,
                "concatenated schedule lost its block/global locator");
        }
        for (std::size_t deck = 0;
             deck < old_school::kDeckCount;
             ++deck) {
            expect(
                balance.perspectives_by_deck[deck] ==
                    schedule::kPerspectivesPerDeck,
                "development schedule has wrong deck balance");
            expect(
                balance.seat_zero_by_deck[deck] ==
                    schedule::kSeatZeroPerDeck,
                "development schedule has wrong seat balance");
            expect(
                balance.on_play_by_deck[deck] ==
                    schedule::kOnPlayPerDeck,
                "development schedule has wrong play balance");
            for (const auto& by_play :
                 balance.seat_play_quadrants[deck]) {
                for (const std::size_t count : by_play) {
                    expect(
                        count ==
                            schedule::kQuadrantPerDeck,
                        "development schedule has wrong quadrant balance");
                }
            }
        }
        const std::string bytes =
            schedule::serialize_source_schedule(first);
        expect(
            bytes.size() ==
                schedule::
                    expected_schedule_bytes(split),
            "frozen development schedule byte count drifted");
        expect(
            schedule::source_schedule_sha256(split) ==
                old_school::artifact_integrity::
                    sha256_string(bytes),
            "development schedule helper returned wrong hash");
        expect(
            schedule::source_schedule_sha256(split) ==
                schedule::
                    expected_schedule_sha256(split),
            "frozen development schedule hash drifted");
        expect(
            static_cast<std::size_t>(
                std::count(
                    bytes.begin(), bytes.end(), '\n')) ==
                schedule::kPhysicalGamesPerSplit + 1,
            "development schedule has wrong serialized row count");
        expect(
            static_cast<std::size_t>(
                std::count(
                    bytes.begin(), bytes.end(), '\t')) ==
                schedule::kPhysicalGamesPerSplit * 10,
            "development schedule has wrong serialized field count");
    }

    expect_rejected(
        [] {
            static_cast<void>(
                schedule::schedule_block_for_index(
                    schedule::
                        kPhysicalGamesPerSplit));
        },
        "out-of-range global block locator was accepted");
    expect_rejected(
        [] {
            static_cast<void>(
                schedule::local_schedule_index(
                    schedule::
                        kPhysicalGamesPerSplit));
        },
        "out-of-range local block locator was accepted");
}

void test_splits_are_disjoint() {
    const auto fit =
        schedule::source_schedule(
            schedule::Split::Fit);
    const auto check =
        schedule::source_schedule(
            schedule::Split::Check);
    std::set<std::uint64_t> fit_seeds;
    for (const auto& game : fit) {
        fit_seeds.insert(game.game_seed);
    }
    expect(
        std::none_of(
            check.begin(), check.end(),
            [&](const auto& game) {
                return fit_seeds.contains(
                    game.game_seed);
            }),
        "FIT and CHECK game seeds overlap");
    expect(
        schedule::serialize_source_schedule(fit) !=
            schedule::serialize_source_schedule(check),
        "FIT and CHECK schedule bytes collide");
}

void test_malformed_schedules_fail_balance() {
    const auto require_malformed =
        [](const std::vector<schedule::SourceGame>& value,
           std::string_view message) {
            expect(
                !schedule::audit_schedule_balance(value)
                     .exact,
                message);
            expect_rejected(
                [&] {
                    static_cast<void>(
                        schedule::
                            serialize_source_schedule(
                                value));
                },
                "malformed schedule was serialized");
        };

    auto duplicate =
        schedule::source_schedule(
            schedule::Split::Fit);
    duplicate.back() = duplicate.front();
    require_malformed(
        duplicate, "duplicate schedule row passed");

    auto mixed =
        schedule::source_schedule(
            schedule::Split::Fit);
    mixed.back().split = schedule::Split::Check;
    require_malformed(
        mixed, "mixed schedule split passed");

    auto wrong_block =
        schedule::source_schedule(
            schedule::Split::Fit);
    ++wrong_block[40].schedule_block;
    require_malformed(
        wrong_block, "block mutation passed");

    auto wrong_global =
        schedule::source_schedule(
            schedule::Split::Fit);
    ++wrong_global[40].schedule_index;
    require_malformed(
        wrong_global, "global-index mutation passed");

    auto wrong_seed =
        schedule::source_schedule(
            schedule::Split::Fit);
    wrong_seed[40].game_seed ^= 1U;
    require_malformed(
        wrong_seed, "one-bit game-seed mutation passed");

    auto reordered =
        schedule::source_schedule(
            schedule::Split::Fit);
    std::swap(reordered[39], reordered[40]);
    require_malformed(
        reordered, "cross-block row reordering passed");

    auto truncated =
        schedule::source_schedule(
            schedule::Split::Fit);
    truncated.pop_back();
    require_malformed(
        truncated, "schedule row-count mutation passed");
}

} // namespace

int main() {
    const std::array<
        std::pair<std::string_view,
                  std::function<void()>>,
        4>
        tests{{
            {
                "frozen seed and namespace derivation",
                test_seed_and_namespace_literals,
            },
            {
                "deterministic exact schedule balance",
                test_each_schedule_is_deterministic_and_balanced,
            },
            {
                "FIT CHECK seed disjointness",
                test_splits_are_disjoint,
            },
            {
                "malformed schedule rejection",
                test_malformed_schedules_fail_balance,
            },
        }};
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr
                << "[FAIL] " << name << ": "
                << error.what() << '\n';
        }
    }
    std::cout << passed << "/" << tests.size()
              << " tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
