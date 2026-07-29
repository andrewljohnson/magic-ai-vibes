#pragma once

#include "old_school/learned_priority_bilinear.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace old_school::learned_priority_bilinear_artifact {

inline constexpr std::string_view kSchema =
    "old-school-aq19-bilinear-artifact-v1";
inline constexpr std::string_view kProductionFilename =
    "old-school-aq19-dbc6-r2-bilinear.bin";
inline constexpr std::string_view
    kProductionParameterSha256 =
        "3114c898085375b7c39a8d8a7add5b0ab87dc70916d676deccd28d45e0942194";
inline constexpr std::uintmax_t
    kProductionArtifactBytes = 14502;
inline constexpr std::string_view
    kProductionFileSha256 =
        "445f93435aebafbafc16cda4d1faa9e4d56dc12a25196f79c1334fcc84d22c1a";
inline constexpr std::size_t kMaximumArtifactBytes =
    64U * 1024U;

struct Contract {
    std::string parent_fingerprint;
    std::string parameter_sha256;

    bool operator==(const Contract&) const = default;
};

struct Identity {
    std::uintmax_t bytes = 0;
    std::string file_sha256;
    std::string parameter_sha256;
    std::string parent_fingerprint;

    bool operator==(const Identity&) const = default;
};

struct Loaded {
    std::shared_ptr<const LearnedPriorityBilinear> residual;
    Identity identity;
};

const Contract& production_contract();

// Publishes deterministic bytes through a same-directory hard-link so an
// existing destination is never replaced. The caller supplies already-fitted
// parameters; publication performs no training or gameplay.
Identity publish_atomic_no_replace(
    const std::filesystem::path& destination,
    const LearnedPriorityBilinearParameters& parameters,
    const Contract& contract);

// Load-only, fail-closed boundary. It authenticates the exact file and
// canonical parameter bytes before constructing the immutable residual.
Loaded load(
    const std::filesystem::path& path,
    const Contract& expected_contract);

} // namespace old_school::learned_priority_bilinear_artifact
