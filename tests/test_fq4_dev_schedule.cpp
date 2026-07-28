#include "old_school/fq4_dev_schedule.hpp"

#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <array>
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
                schedule::kPhysicalGamesPerSplit * 9,
            "development schedule has wrong serialized field count");
    }
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
    auto duplicate =
        schedule::source_schedule(
            schedule::Split::Fit);
    duplicate.back() = duplicate.front();
    expect(
        !schedule::audit_schedule_balance(duplicate)
             .exact,
        "duplicate schedule row passed");

    auto mixed =
        schedule::source_schedule(
            schedule::Split::Fit);
    mixed.back().split = schedule::Split::Check;
    expect(
        !schedule::audit_schedule_balance(mixed)
             .exact,
        "mixed schedule split passed");
}

} // namespace

int main() {
    const std::array<
        std::pair<std::string_view,
                  std::function<void()>>,
        3>
        tests{{
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
