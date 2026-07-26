#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace old_school::artifact_integrity {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

class Sha256 {
  public:
    void update(std::span<const std::byte> bytes) {
        constexpr std::uint64_t kMaximumBytes =
            std::numeric_limits<std::uint64_t>::max() / CHAR_BIT;
        if (bytes.size() > kMaximumBytes - total_bytes_) {
            throw std::length_error(
                "SHA-256 input exceeds its 64-bit length field");
        }
        total_bytes_ += static_cast<std::uint64_t>(bytes.size());

        std::size_t offset = 0;
        if (buffer_size_ != 0) {
            const std::size_t copied =
                std::min(
                    bytes.size(), buffer_.size() - buffer_size_);
            std::memcpy(
                buffer_.data() + buffer_size_, bytes.data(), copied);
            buffer_size_ += copied;
            offset += copied;
            if (buffer_size_ == buffer_.size()) {
                transform(buffer_);
                buffer_size_ = 0;
            }
        }

        while (bytes.size() - offset >= buffer_.size()) {
            std::array<std::byte, 64> block;
            std::memcpy(
                block.data(), bytes.data() + offset, block.size());
            transform(block);
            offset += block.size();
        }

        const std::size_t remaining = bytes.size() - offset;
        if (remaining != 0) {
            std::memcpy(
                buffer_.data(), bytes.data() + offset, remaining);
            buffer_size_ = remaining;
        }
    }

    std::string finish() {
        const std::uint64_t bit_count =
            total_bytes_ * static_cast<std::uint64_t>(CHAR_BIT);
        buffer_[buffer_size_++] = std::byte{0x80};
        if (buffer_size_ > 56) {
            while (buffer_size_ < buffer_.size()) {
                buffer_[buffer_size_++] = std::byte{0};
            }
            transform(buffer_);
            buffer_size_ = 0;
        }
        while (buffer_size_ < 56) {
            buffer_[buffer_size_++] = std::byte{0};
        }
        for (std::size_t byte = 0; byte < 8; ++byte) {
            const auto shift =
                static_cast<unsigned int>((7 - byte) * CHAR_BIT);
            buffer_[56 + byte] = static_cast<std::byte>(
                (bit_count >> shift) & 0xffU);
        }
        transform(buffer_);
        buffer_size_ = 0;

        static constexpr char kHex[] = "0123456789abcdef";
        std::string digest(64, '0');
        for (std::size_t word = 0; word < state_.size(); ++word) {
            for (std::size_t nibble = 0; nibble < 8; ++nibble) {
                const auto shift =
                    static_cast<unsigned int>((7 - nibble) * 4);
                digest[word * 8 + nibble] =
                    kHex[(state_[word] >> shift) & 0xfU];
            }
        }
        return digest;
    }

  private:
    void transform(const std::array<std::byte, 64>& block) {
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t word = 0; word < 16; ++word) {
            const std::size_t offset = word * 4;
            schedule[word] =
                (std::to_integer<std::uint32_t>(block[offset]) << 24) |
                (std::to_integer<std::uint32_t>(block[offset + 1]) << 16) |
                (std::to_integer<std::uint32_t>(block[offset + 2]) << 8) |
                std::to_integer<std::uint32_t>(block[offset + 3]);
        }
        for (std::size_t word = 16; word < schedule.size(); ++word) {
            const std::uint32_t sigma_zero =
                std::rotr(schedule[word - 15], 7) ^
                std::rotr(schedule[word - 15], 18) ^
                (schedule[word - 15] >> 3);
            const std::uint32_t sigma_one =
                std::rotr(schedule[word - 2], 17) ^
                std::rotr(schedule[word - 2], 19) ^
                (schedule[word - 2] >> 10);
            schedule[word] =
                schedule[word - 16] + sigma_zero +
                schedule[word - 7] + sigma_one;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];

        for (std::size_t round = 0;
             round < kRoundConstants.size(); ++round) {
            const std::uint32_t sum_one =
                std::rotr(e, 6) ^ std::rotr(e, 11) ^
                std::rotr(e, 25);
            const std::uint32_t choice =
                (e & f) ^ ((~e) & g);
            const std::uint32_t temporary_one =
                h + sum_one + choice + kRoundConstants[round] +
                schedule[round];
            const std::uint32_t sum_zero =
                std::rotr(a, 2) ^ std::rotr(a, 13) ^
                std::rotr(a, 22);
            const std::uint32_t majority =
                (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temporary_two =
                sum_zero + majority;

            h = g;
            g = f;
            f = e;
            e = d + temporary_one;
            d = c;
            c = b;
            b = a;
            a = temporary_one + temporary_two;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_ = {
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    };
    std::array<std::byte, 64> buffer_{};
    std::size_t buffer_size_ = 0;
    std::uint64_t total_bytes_ = 0;
};

class FileDescriptor {
  public:
    explicit FileDescriptor(int descriptor)
        : descriptor_(descriptor) {}

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    ~FileDescriptor() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }

    int get() const {
        return descriptor_;
    }

  private:
    int descriptor_;
};

struct Timestamp {
    std::int64_t seconds;
    std::int64_t nanoseconds;

    bool operator==(const Timestamp&) const = default;
};

Timestamp modification_time(const struct stat& status) {
#if defined(__APPLE__)
    return {
        static_cast<std::int64_t>(status.st_mtimespec.tv_sec),
        static_cast<std::int64_t>(status.st_mtimespec.tv_nsec),
    };
#elif defined(__linux__)
    return {
        static_cast<std::int64_t>(status.st_mtim.tv_sec),
        static_cast<std::int64_t>(status.st_mtim.tv_nsec),
    };
#else
#error "artifact_integrity supports macOS and Linux"
#endif
}

Timestamp change_time(const struct stat& status) {
#if defined(__APPLE__)
    return {
        static_cast<std::int64_t>(status.st_ctimespec.tv_sec),
        static_cast<std::int64_t>(status.st_ctimespec.tv_nsec),
    };
#elif defined(__linux__)
    return {
        static_cast<std::int64_t>(status.st_ctim.tv_sec),
        static_cast<std::int64_t>(status.st_ctim.tv_nsec),
    };
#else
#error "artifact_integrity supports macOS and Linux"
#endif
}

bool same_file_metadata(
    const struct stat& left, const struct stat& right) {
    return left.st_dev == right.st_dev &&
           left.st_ino == right.st_ino &&
           left.st_mode == right.st_mode &&
           left.st_nlink == right.st_nlink &&
           left.st_size == right.st_size &&
           modification_time(left) == modification_time(right) &&
           change_time(left) == change_time(right);
}

std::filesystem::path physical_path_for(
    const std::filesystem::path& absolute_path) {
    std::error_code path_error;
    const std::filesystem::path canonical_parent =
        std::filesystem::canonical(
            absolute_path.parent_path(), path_error);
    if (path_error) {
        throw std::system_error(
            path_error,
            "cannot resolve artifact parent directory");
    }
    return (canonical_parent / absolute_path.filename())
        .lexically_normal();
}

[[noreturn]] void throw_system_call(
    std::string_view operation,
    const std::filesystem::path& path, int error) {
    throw std::system_error(
        error, std::generic_category(),
        std::string(operation) + " '" + path.string() + "'");
}

struct stat retry_lstat(const std::filesystem::path& path) {
    struct stat status {};
    while (::lstat(path.c_str(), &status) != 0) {
        const int error = errno;
        if (error == EINTR) {
            continue;
        }
        throw_system_call("cannot inspect", path, error);
    }
    return status;
}

struct stat retry_fstat(
    int descriptor, const std::filesystem::path& path) {
    struct stat status {};
    while (::fstat(descriptor, &status) != 0) {
        const int error = errno;
        if (error == EINTR) {
            continue;
        }
        throw_system_call("cannot inspect opened file", path, error);
    }
    return status;
}

int retry_open(const std::filesystem::path& path) {
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    while (true) {
        const int descriptor = ::open(path.c_str(), flags);
        if (descriptor >= 0) {
            return descriptor;
        }
        const int error = errno;
        if (error == EINTR) {
            continue;
        }
        throw_system_call("cannot open regular file", path, error);
    }
}

std::size_t retry_read(
    int descriptor, std::span<std::byte> buffer,
    const std::filesystem::path& path) {
    while (true) {
        const ssize_t count =
            ::read(descriptor, buffer.data(), buffer.size());
        if (count >= 0) {
            return static_cast<std::size_t>(count);
        }
        const int error = errno;
        if (error == EINTR) {
            continue;
        }
        throw_system_call("cannot read regular file", path, error);
    }
}

void require_regular_file(
    const struct stat& status,
    const std::filesystem::path& path) {
    if (S_ISLNK(status.st_mode)) {
        throw std::runtime_error(
            "artifact path is a symlink: '" + path.string() + "'");
    }
    if (!S_ISREG(status.st_mode)) {
        throw std::runtime_error(
            "artifact path is not a regular file: '" +
            path.string() + "'");
    }
    if (status.st_size < 0) {
        throw std::runtime_error(
            "artifact file has a negative size: '" +
            path.string() + "'");
    }
}

void require_unchanged(
    const struct stat& expected, const struct stat& observed,
    const std::filesystem::path& path) {
    if (!same_file_metadata(expected, observed)) {
        throw std::runtime_error(
            "artifact file changed while being read: '" +
            path.string() + "'");
    }
}

} // namespace

std::string sha256_bytes(std::span<const std::byte> bytes) {
    Sha256 hash;
    hash.update(bytes);
    return hash.finish();
}

std::string sha256_string(std::string_view text) {
    return sha256_bytes(std::as_bytes(std::span(text)));
}

RegularFileSnapshot snapshot_regular_file(
    const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::invalid_argument(
            "artifact path must not be empty");
    }

    std::error_code path_error;
    const std::filesystem::path absolute_path =
        std::filesystem::absolute(path, path_error).lexically_normal();
    if (path_error) {
        throw std::system_error(
            path_error, "cannot make artifact path absolute");
    }

    const struct stat path_before = retry_lstat(absolute_path);
    require_regular_file(path_before, absolute_path);
    const std::filesystem::path physical_path_before =
        physical_path_for(absolute_path);
    const struct stat physical_before =
        retry_lstat(physical_path_before);
    require_regular_file(physical_before, physical_path_before);
    require_unchanged(
        path_before, physical_before, absolute_path);

    const FileDescriptor descriptor(retry_open(absolute_path));
    const struct stat descriptor_before =
        retry_fstat(descriptor.get(), absolute_path);
    require_regular_file(descriptor_before, absolute_path);
    require_unchanged(
        path_before, descriptor_before, absolute_path);

    Sha256 hash;
    std::array<std::byte, 64 * 1024> buffer;
    std::uintmax_t bytes_read = 0;
    while (true) {
        const std::size_t count =
            retry_read(descriptor.get(), buffer, absolute_path);
        if (count == 0) {
            break;
        }
        if (bytes_read >
            std::numeric_limits<std::uintmax_t>::max() - count) {
            throw std::length_error(
                "artifact size overflow while reading '" +
                absolute_path.string() + "'");
        }
        bytes_read += count;
        hash.update(
            std::span<const std::byte>(buffer.data(), count));
    }

    const struct stat descriptor_after =
        retry_fstat(descriptor.get(), absolute_path);
    require_unchanged(
        descriptor_before, descriptor_after, absolute_path);
    const struct stat path_after = retry_lstat(absolute_path);
    require_regular_file(path_after, absolute_path);
    require_unchanged(
        descriptor_before, path_after, absolute_path);
    const std::filesystem::path physical_path_after =
        physical_path_for(absolute_path);
    if (physical_path_after != physical_path_before) {
        throw std::runtime_error(
            "artifact physical path changed while being read: '" +
            absolute_path.string() + "'");
    }
    const struct stat physical_after =
        retry_lstat(physical_path_after);
    require_regular_file(physical_after, physical_path_after);
    require_unchanged(
        descriptor_before, physical_after, absolute_path);

    const auto expected_size =
        static_cast<std::uintmax_t>(descriptor_before.st_size);
    if (bytes_read != expected_size) {
        throw std::runtime_error(
            "artifact byte count changed while being read: '" +
            absolute_path.string() + "'");
    }

    const Timestamp modified =
        modification_time(descriptor_before);
    const Timestamp changed =
        change_time(descriptor_before);
    return {
        .path = absolute_path.string(),
        .physical_path = physical_path_before.string(),
        .byte_size = bytes_read,
        .sha256 = hash.finish(),
        .device =
            static_cast<std::uint64_t>(descriptor_before.st_dev),
        .inode =
            static_cast<std::uint64_t>(descriptor_before.st_ino),
        .link_count =
            static_cast<std::uint64_t>(
                descriptor_before.st_nlink),
        .modification_seconds = modified.seconds,
        .modification_nanoseconds = modified.nanoseconds,
        .change_seconds = changed.seconds,
        .change_nanoseconds = changed.nanoseconds,
    };
}

} // namespace old_school::artifact_integrity
