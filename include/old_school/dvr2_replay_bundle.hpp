#pragma once

#include "old_school/probes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::dvr2_replay_bundle {

inline constexpr std::string_view kArtifactPath =
    "data/old-school-dvr2-c16-mirror-v1.dvr2";
inline constexpr std::size_t kArtifactBytes = 221079;
inline constexpr std::string_view kArtifactSha256 =
    "c6b5c199133b931de85386506ace16ff823407599362637e2000b743d9529804";
inline constexpr std::string_view kPayloadSha256 =
    "82f6b27fdead69c9273e72e96770f1ebcf29ae4a7d44cc74f621c77c6470690c";
inline constexpr std::string_view kBundleSchema =
    "old-school-dvr2-bundle-v1";
inline constexpr std::string_view kPayloadSchema =
    "old-school-dvr2-harvest-v1";
inline constexpr std::string_view kEnvironmentRevision =
    "old-school-environment-v3-cleanup-discard";
inline constexpr std::string_view kModelFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::size_t kRootCount = 52;
inline constexpr std::size_t kReplayCount = 4;

struct SourceMetadata {
    std::uint64_t seed_base = 0;
    std::size_t seed_base_index = 0;
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::uint64_t game_seed = 0;
    std::size_t owner_seat = 0;
    bool owner_on_play = false;
    std::size_t starting_player = 0;
    std::size_t trace_ordinal = 0;

    bool operator==(const SourceMetadata&) const = default;
};

struct ReplayRecord {
    std::size_t root_index = 0;
    SourceMetadata source;
    DeckId owner_deck = DeckId::Green;
    DeckId opponent_deck = DeckId::Red;
    std::string stratum;
    std::string information_action_fingerprint;
    std::string production_action_descriptor;
    probes::BsrRootScore reference_score;
    std::string serialized_dvr1;
    std::string dvr1_record_fingerprint;
    probes::Dvr1OwnerVisibleRecord dvr1;
    // DVR1 intentionally omits the opponent's captured private identities.
    // This probe's opponent hand/library are a deterministic synthetic
    // partition reconstructed from deck composition and public zone counts.
    probes::DecisionProbe probe;
};

struct ReplayBundle {
    std::string artifact_path;
    std::string artifact_sha256;
    std::string payload_sha256;
    std::string model_artifact_path;
    std::string model_artifact_sha256;
    std::string model_fingerprint;
    std::size_t selected_roots = 0;
    std::size_t stable_disagreements = 0;
    std::size_t stable_agreements = 0;
    std::size_t unstable_best_sets = 0;
    std::size_t invalid_invariance = 0;
    std::vector<ReplayRecord> replays;
};

// Loads only the exact sealed DVR2 artifact. The file is snapshotted before
// and after the read, must be a non-symlink regular file, and must match the
// frozen byte count and SHA-256 before any replay is exposed.
ReplayBundle load();
ReplayBundle load(const std::filesystem::path& path);

namespace testing {

// Structural decoder used by focused corruption tests. It still requires the
// canonical schemas, metadata, root census, four exact replay identities,
// inner checksum, and valid DVR1 records, but intentionally does not require
// the outer frozen file SHA. Production code must call load().
ReplayBundle decode_structurally_valid_bundle(
    std::string_view bytes);

} // namespace testing

} // namespace old_school::dvr2_replay_bundle
