#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace old_school::learned_iteration {
struct ScheduledGame;
}

namespace old_school::audit_common {

// Stable, deterministic audit hash. Integer values are serialized
// little-endian and doubles by their exact IEEE-754 bit pattern.
class ContentHash {
  public:
    void add_byte(std::uint8_t byte);
    void add_u64(std::uint64_t value);
    void add_size(std::size_t value);
    void add_double(double value);
    void add_text(std::string_view value);
    std::string finish() const;

  private:
    std::array<std::uint64_t, 4> state_ = {
        0x6a09e667f3bcc909ULL,
        0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL,
        0xa54ff53a5f1d36f1ULL,
    };
    std::uint64_t count_ = 0;
};

std::string format_real(double value);
std::string sanitize_tsv(std::string_view value);
void require_probability(double value, std::string_view field);

bool bit_identical(double left, double right);
bool bit_identical(
    std::span<const double> left,
    std::span<const double> right);

bool is_lower_hex_digest(std::string_view value);
double soft_log_loss(double prediction, double target);
bool same_strict_sign(double left, double right);
double mass_tolerance(double expected_mass);

void hash_scheduled_task(
    ContentHash& hash, std::size_t physical_game,
    std::size_t block,
    const learned_iteration::ScheduledGame& scheduled);
void hash_optional_index(
    ContentHash& hash,
    const std::optional<std::size_t>& value);
void hash_observation(
    ContentHash& hash,
    std::span<const double> observation);

} // namespace old_school::audit_common
