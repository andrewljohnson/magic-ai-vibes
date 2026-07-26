#include "old_school/audit_common.hpp"
#include "old_school/learned_iteration.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace common = old_school::audit_common;

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
            std::cout << "[FAIL] " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " passed, " << failed_
                  << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Exception, typename Function>
void expect_throws(
    Function function, std::string_view expected_message) {
    try {
        function();
    } catch (const Exception& error) {
        expect(
            error.what() == expected_message,
            "exception message changed");
        return;
    }
    throw std::runtime_error("expected exception was not thrown");
}

void test_formatting_and_tsv_sanitization() {
    expect(
        common::format_real(0.1) ==
            "0.10000000000000001",
        "max-digits formatting changed");
    expect(
        common::format_real(-0.0) == "-0",
        "negative-zero formatting changed");
    expect(
        common::sanitize_tsv("alpha\tbeta\ngamma\rdelta") ==
            "alpha beta gamma delta",
        "TSV control-character replacement changed");
    expect(
        common::sanitize_tsv("plain \xC2\xB5 text") ==
            "plain \xC2\xB5 text",
        "TSV sanitizer changed non-control bytes");
}

void test_probability_validation() {
    common::require_probability(0.0, "fixture");
    common::require_probability(0.5, "fixture");
    common::require_probability(1.0, "fixture");

    const auto reject = [](double value) {
        expect_throws<std::invalid_argument>(
            [&] {
                common::require_probability(value, "fixture");
            },
            "fixture must be finite and in [0, 1]");
    };
    reject(std::nextafter(0.0, -1.0));
    reject(std::nextafter(1.0, 2.0));
    reject(std::numeric_limits<double>::quiet_NaN());
    reject(std::numeric_limits<double>::infinity());
    reject(-std::numeric_limits<double>::infinity());
}

void test_bit_identity() {
    const double adjacent =
        std::nextafter(1.0, 2.0);
    expect(
        common::bit_identical(1.0, 1.0),
        "equal scalar bits differ");
    expect(
        !common::bit_identical(0.0, -0.0),
        "positive and negative zero became bit-identical");
    expect(
        !common::bit_identical(1.0, adjacent),
        "adjacent doubles became bit-identical");

    constexpr std::uint64_t nan_bits =
        0x7ff8000000000042ULL;
    const double nan =
        std::bit_cast<double>(nan_bits);
    expect(
        common::bit_identical(nan, nan),
        "identical NaN payload bits differ");

    const std::array<double, 3> first = {
        0.0, -0.0, adjacent};
    const std::array<double, 3> same = first;
    const std::array<double, 3> changed = {
        0.0, 0.0, adjacent};
    const std::array<double, 2> shorter = {
        0.0, -0.0};
    expect(
        common::bit_identical(first, same),
        "equal spans differ");
    expect(
        !common::bit_identical(first, changed),
        "changed span compared equal");
    expect(
        !common::bit_identical(first, shorter),
        "different span sizes compared equal");
}

void test_digest_validation_and_mass_tolerance() {
    expect(
        common::is_lower_hex_digest(std::string(64, '0')),
        "lowercase digest rejected");
    expect(
        common::is_lower_hex_digest(std::string(64, 'f')),
        "lowercase f digest rejected");
    expect(
        !common::is_lower_hex_digest(std::string(64, 'F')),
        "uppercase digest accepted");
    expect(
        !common::is_lower_hex_digest(std::string(63, '0')),
        "short digest accepted");
    std::string non_hex(64, '0');
    non_hex[17] = 'g';
    expect(
        !common::is_lower_hex_digest(non_hex),
        "non-hex digest accepted");

    const double epsilon =
        std::numeric_limits<double>::epsilon();
    expect(
        common::bit_identical(
            common::mass_tolerance(0.0),
            64.0 * epsilon),
        "unit-scale mass tolerance changed");
    expect(
        common::bit_identical(
            common::mass_tolerance(-3.5),
            64.0 * epsilon * 3.5),
        "scaled mass tolerance changed");
}

void test_soft_log_loss_and_sign() {
    const double expected =
        -0.75 * std::log(0.25) -
        0.25 * std::log(0.75);
    expect(
        common::bit_identical(
            common::soft_log_loss(0.25, 0.75),
            expected),
        "soft-log formula changed");
    expect(
        std::isfinite(common::soft_log_loss(0.0, 1.0)) &&
            std::isfinite(
                common::soft_log_loss(1.0, 0.0)),
        "soft-log clamp stopped producing finite values");
    expect(
        common::same_strict_sign(1.0, 2.0) &&
            common::same_strict_sign(-1.0, -2.0) &&
            !common::same_strict_sign(0.0, 1.0) &&
            !common::same_strict_sign(-1.0, 1.0),
        "strict-sign semantics changed");
}

void test_content_hash_golden() {
    common::ContentHash empty;
    expect(
        empty.finish() ==
            "TODO_EMPTY_DIGEST",
        "empty content-hash golden changed");

    common::ContentHash hash;
    hash.add_text("audit-common-characterization-v1");
    common::hash_optional_index(hash, std::nullopt);
    common::hash_optional_index(hash, std::size_t{17});
    const std::array<double, 4> observation = {
        0.0,
        -0.0,
        std::nextafter(1.0, 2.0),
        std::bit_cast<double>(0x7ff8000000000042ULL),
    };
    common::hash_observation(hash, observation);
    const old_school::learned_iteration::ScheduledGame scheduled = {
        .schedule_index = 23,
        .pairing_index = 5,
        .seat_decks = {
            old_school::DeckId::Blue,
            old_school::DeckId::RUAggro,
        },
        .starting_player = 1,
        .seed = 0x0123456789abcdefULL,
    };
    common::hash_scheduled_task(
        hash, 41, 7, scheduled);
    expect(
        hash.finish() ==
            "TODO_COMPOSED_DIGEST",
        "composed content-hash golden changed");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "formatting and TSV sanitization",
        test_formatting_and_tsv_sanitization);
    runner.run(
        "probability validation",
        test_probability_validation);
    runner.run("bit identity", test_bit_identity);
    runner.run(
        "digest validation and mass tolerance",
        test_digest_validation_and_mass_tolerance);
    runner.run(
        "soft log loss and strict sign",
        test_soft_log_loss_and_sign);
    runner.run(
        "content hash golden",
        test_content_hash_golden);
    return runner.finish();
}
