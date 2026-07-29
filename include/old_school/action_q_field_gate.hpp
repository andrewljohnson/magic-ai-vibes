#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace old_school::action_q_field_gate {

// Evaluation-only reconstruction of the owner-information root observed in
// the FQ4-EXPLORE-7 field diagnosis. The source trajectory was frozen C16
// (T800/S424242/G16, K8/H4, PD0 plus adversarial blocks) as Blue seat zero
// versus Random Red in game seed 24. The selected action at this root was
// Ancestral Recall targeting the opponent.
//
// Hidden-zone identities and order are intentionally canonical rather than
// copied from the physical source game. They are outside Blue's information
// set, and all consumers must be invariant to their repartition.
inline constexpr std::string_view kSchema =
    "old-school-action-q-ancestral-field-root-v1";
inline constexpr std::string_view kStableId =
    "field.blue.ancestral-opponent-seed24.aq0.v1";
inline constexpr std::uint64_t kCaptureGameSeed = 24;
inline constexpr std::size_t kCaptureMaximumTurns = 40;
inline constexpr std::size_t kCaptureTurn = 13;
inline constexpr std::uint64_t kReferenceSearchSeed =
    202607281701ULL;
inline constexpr std::size_t kCaptureWorlds = 8;
inline constexpr std::size_t kCaptureHorizonTurns = 4;
inline constexpr std::string_view kCaptureModelFingerprint =
    "68126afc5a3e3757eb1d510a056585aa"
    "974c4f54ce1b4a789ff430f1c7413e2f";

struct AncestralFieldRoot {
    GameState state;
    std::array<std::vector<CardId>, 2> original_decks;
    std::size_t actor = 0;
    LearnedDecisionContext context;
    std::vector<PriorityAction> legal_actions;
    std::size_t pass_index = 0;
    std::size_t self_target_index = 0;
    std::size_t opponent_target_index = 0;

    bool operator==(const AncestralFieldRoot&) const = default;
};

// Constructs the exact public/owner-visible root and a deterministic,
// information-equivalent canonical partition of hidden zones. It fails closed
// if the engine-authoritative complete legal action set no longer contains
// unique Pass, self-target Ancestral, and opponent-target Ancestral actions.
AncestralFieldRoot make_ancestral_field_root();

// Produces a physically distinct state with identical owner information.
// The actor's library order and the opponent's hidden hand/library partition
// change, while public zones, hand sizes, the actor's hand, and legal actions
// remain unchanged.
AncestralFieldRoot hidden_repartition_clone(
    const AncestralFieldRoot& root);

// Small fail-closed predicate for callers that load or copy the fixture.
bool has_required_action_identities(
    const AncestralFieldRoot& root);

struct LearnedValueScoringRecipe {
    std::size_t worlds = kCaptureWorlds;
    std::uint64_t seed = kReferenceSearchSeed;
    double continuation_epsilon = 0.0;
    double priority_residual_weight = 0.0;
    bool pass_dominance = false;
    LearnedContinuationController continuation_controller =
        LearnedContinuationController::Legacy;
    double resolved_shallow_prior_weight = 0.0;
};

// Convenience adapter for comparing frozen Learned Value models or residual
// settings on the complete root. New action-Q implementations may instead
// consume state/context/legal_actions directly; no score or preferred action
// from this module enters training or runtime play.
LearnedValuePriorityDiagnostic score_learned_value(
    const AncestralFieldRoot& root,
    std::shared_ptr<const LearnedModel> model,
    LearnedValueScoringRecipe recipe = {});

} // namespace old_school::action_q_field_gate
