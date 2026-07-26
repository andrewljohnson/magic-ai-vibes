#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace old_school::artifact_integrity {

// Returns the lower-case hexadecimal SHA-256 digest of the exact input bytes.
std::string sha256_bytes(std::span<const std::byte> bytes);
std::string sha256_string(std::string_view text);

struct RegularFileSnapshot {
    // Absolute, lexically normalized spelling of the path supplied by the
    // caller. Ancestor symlink components are intentionally preserved here.
    std::string path;
    // Same final filename under a canonicalized parent directory. Thus,
    // ancestor symlinks are resolved without accepting a final symlink.
    std::string physical_path;
    std::uintmax_t byte_size = 0;
    std::string sha256;
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
    std::uint64_t link_count = 0;
    std::int64_t modification_seconds = 0;
    std::int64_t modification_nanoseconds = 0;
    std::int64_t change_seconds = 0;
    std::int64_t change_nanoseconds = 0;

    bool operator==(const RegularFileSnapshot&) const = default;
};

// Opens a regular file without following a final-component symlink, hashes
// its contents, and checks at several points that its identity, metadata, and
// ancestor-resolved physical path did not change while it was read. This
// detects ordinary accidental replacement or concurrent mutation observed by
// those checks. It is not an atomic filesystem snapshot and does not claim to
// defeat a hostile local process that deliberately races and restores state.
//
// Throws std::invalid_argument for an empty path and std::runtime_error (or a
// derived standard exception) for missing, non-regular, symlinked, unreadable,
// or concurrently mutated files.
RegularFileSnapshot snapshot_regular_file(
    const std::filesystem::path& path);

} // namespace old_school::artifact_integrity
