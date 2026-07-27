#pragma once

#include "old_school/fq0_dominance.hpp"
#include "old_school/probes.hpp"

#include <cstddef>
#include <string>

namespace old_school::fq0_dominance_transition {

// Evaluation-only, policy-free transition used by the FQ0 dominance audit.
// It settles the forced root action and current stack with the production
// rules helpers, then passes every later Priority window, declares no
// attackers or blockers, chooses cleanup discards by
// (CardId numeric value, original hand index), and stops at terminal or the
// next turn's First Main boundary.
fq0_dominance::Settlement advance_to_next_first_main(
    const probes::DecisionProbe& probe,
    const GameState& information_set_world,
    std::size_t candidate_index,
    std::string root_information_fingerprint);

} // namespace old_school::fq0_dominance_transition
