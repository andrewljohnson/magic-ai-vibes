#include "old_school/audit_common.hpp"

#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace old_school::audit_common {
namespace {

constexpr double kLogClamp = 1.0e-12;

} // namespace

void ContentHash::add_byte(std::uint8_t byte) {
    static constexpr std::array<std::uint64_t, 4> salt = {
        0x9e3779b97f4a7c15ULL,
        0xd1b54a32d192ed03ULL,
        0x94d049bb133111ebULL,
        0xbf58476d1ce4e5b9ULL,
    };
    for (std::size_t index = 0; index < state_.size(); ++index) {
        state_[index] = std::rotl(
            state_[index] ^
                (static_cast<std::uint64_t>(byte) + salt[index]),
            static_cast<int>(11 + 8 * index));
        state_[index] =
            state_[index] *
                (salt[(index + 1) % salt.size()] | 1ULL) +
            count_;
    }
    ++count_;
}

void ContentHash::add_u64(std::uint64_t value) {
    for (std::size_t byte = 0; byte < 8; ++byte) {
        add_byte(static_cast<std::uint8_t>(value >> (8 * byte)));
    }
}

void ContentHash::add_size(std::size_t value) {
    add_u64(static_cast<std::uint64_t>(value));
}

void ContentHash::add_double(double value) {
    add_u64(std::bit_cast<std::uint64_t>(value));
}

void ContentHash::add_text(std::string_view value) {
    add_size(value.size());
    for (const unsigned char byte : value) {
        add_byte(byte);
    }
}

std::string ContentHash::finish() const {
    static constexpr char hex[] = "0123456789abcdef";
    std::array<std::uint64_t, 4> digest = state_;
    for (std::size_t index = 0; index < digest.size(); ++index) {
        digest[index] ^=
            count_ +
            0x9e3779b97f4a7c15ULL *
                static_cast<std::uint64_t>(index + 1);
        digest[index] ^= digest[index] >> 30;
        digest[index] *= 0xbf58476d1ce4e5b9ULL;
        digest[index] ^= digest[index] >> 27;
        digest[index] *= 0x94d049bb133111ebULL;
        digest[index] ^= digest[index] >> 31;
    }
    std::string output(64, '0');
    for (std::size_t word = 0; word < digest.size(); ++word) {
        for (std::size_t nibble = 0; nibble < 16; ++nibble) {
            const auto shift =
                static_cast<unsigned int>(60 - 4 * nibble);
            output[word * 16 + nibble] =
                hex[(digest[word] >> shift) & 0xfULL];
        }
    }
    return output;
}

std::string format_real(double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(
                  std::numeric_limits<double>::max_digits10)
           << value;
    return output.str();
}

std::string sanitize_tsv(std::string_view value) {
    std::string result(value);
    for (char& character : result) {
        if (character == '\t' || character == '\n' ||
            character == '\r') {
            character = ' ';
        }
    }
    return result;
}

void require_probability(double value, std::string_view field) {
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
        throw std::invalid_argument(
            std::string(field) +
            " must be finite and in [0, 1]");
    }
}

bool bit_identical(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

bool bit_identical(
    std::span<const double> left,
    std::span<const double> right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!bit_identical(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

bool is_lower_hex_digest(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               });
}

double soft_log_loss(double prediction, double target) {
    prediction =
        std::clamp(prediction, kLogClamp, 1.0 - kLogClamp);
    return -target * std::log(prediction) -
           (1.0 - target) * std::log(1.0 - prediction);
}

bool same_strict_sign(double left, double right) {
    return (left > 0.0 && right > 0.0) ||
           (left < 0.0 && right < 0.0);
}

double mass_tolerance(double expected_mass) {
    return 64.0 * std::numeric_limits<double>::epsilon() *
           std::max(1.0, std::abs(expected_mass));
}

void hash_scheduled_task(
    ContentHash& hash, std::size_t physical_game,
    std::size_t block,
    const learned_iteration::ScheduledGame& scheduled) {
    hash.add_size(physical_game);
    hash.add_size(block);
    hash.add_size(scheduled.schedule_index);
    hash.add_size(scheduled.pairing_index);
    hash.add_size(
        static_cast<std::size_t>(scheduled.seat_decks[0]));
    hash.add_size(
        static_cast<std::size_t>(scheduled.seat_decks[1]));
    hash.add_size(scheduled.starting_player);
    hash.add_u64(scheduled.seed);
}

void hash_optional_index(
    ContentHash& hash,
    const std::optional<std::size_t>& value) {
    hash.add_u64(value.has_value() ? 1U : 0U);
    if (value.has_value()) {
        hash.add_size(*value);
    }
}

void hash_observation(
    ContentHash& hash,
    std::span<const double> observation) {
    hash.add_size(observation.size());
    for (const double value : observation) {
        hash.add_double(value);
    }
}

} // namespace old_school::audit_common
