#pragma once

#include "old_school/probes.hpp"

#include <string_view>

namespace old_school::probes {

// Strictly decodes the canonical DVR1 envelope. Any missing, duplicate,
// unknown, out-of-range, noncanonical, truncated, or trailing field throws
// std::invalid_argument.
Dvr1OwnerVisibleRecord deserialize_dvr1_owner_visible_record(
    std::string_view bytes);

// Rebuilds one canonical engine state from the owner-visible envelope. The
// opponent's synthetic hidden partition is derived only from deck
// composition and public counts; no captured opponent hidden identity is
// consumed. Reporting-only PlayerGameStats are normalized to zero.
DecisionProbe rehydrate_dvr1_decision_probe(
    const Dvr1OwnerVisibleRecord& record);

} // namespace old_school::probes
