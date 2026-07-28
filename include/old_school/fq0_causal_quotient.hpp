#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq0_causal_quotient {

inline constexpr std::string_view
    kRegisteredRowIdentitySha256 =
        "564d2a185c6d591b9848a33b7f19c669893c7e4c85aba0b7054e931f1533745c";

// Frozen only after the portable FR3 derivation is independently reproduced
// in the test binary. Production refuses to run while this is empty.
inline constexpr std::string_view
    kRegisteredResidualCatalogSha256 =
        "ffe52f04973793f49d5a841384b8992fc650682f99b7b4e1c8107948582cb7e8";

struct ActionComparison {
    PriorityAction action;
    bool complete = false;
    bool disposition_equal = false;
    bool terminal_result_equal = false;
    bool next_context_equal = false;
    bool next_legal_actions_equal = false;
    bool observation_bit_identical = false;
    bool value_bit_identical = false;
    bool canonical_successor_state_equal = false;
    bool accounting_equal = false;

    bool equivalent() const;
    bool operator==(const ActionComparison&) const = default;
};

struct PairComparison {
    bool graveyard_order_only = false;
    bool graveyard_order_nontrivial = false;
    bool shared_authoritative_actions = false;
    std::vector<ActionComparison> actions;

    bool equivalent() const;
    bool operator==(const PairComparison&) const = default;
};

// Compares two complete Priority states under the quotient that treats each
// graveyard as an unordered multiset. The states must otherwise be physically
// identical, including hidden zones and allocator/statistics fields. Every
// shared authoritative legal action is advanced with the same production
// macro-transition seed.
PairComparison compare_graveyard_order_pair(
    const GameState& first, const GameState& second,
    const std::array<std::vector<CardId>, 2>& original_decks,
    const LearnedDecisionContext& context,
    std::size_t observer,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t continuation_seed);

struct DirectControlReport {
    PairComparison blue_counter;
    PairComparison buried_white;
    bool life_perturbation_detected = false;

    bool passed() const;
    bool operator==(const DirectControlReport&) const = default;
};

// The two small, fixed FR1 controls: a naturally settled Blue counter-war
// pair and a reachable White state whose changed graveyard cards are buried
// below the top card. The life-total mutation proves the quotient does not
// erase an unrelated state field.
DirectControlReport compare_direct_controls(
    std::shared_ptr<const LearnedModel> model);

struct RegisteredPairSpec {
    std::string root_stable_id;
    std::string first_information_set;
    std::string second_information_set;
    std::size_t legacy_collision_rows = 0;

    bool operator==(const RegisteredPairSpec&) const = default;
};

// Frozen identities extracted from the registered FQ0-T0 collision section.
// Their row counts sum to 177: 163 Blue and 14 White.
const std::vector<RegisteredPairSpec>& registered_pair_specs();

// The exact five ordered White pair identities retained for the FR2
// sequence-order factorial.
const std::vector<RegisteredPairSpec>&
registered_factorial_pair_specs();

struct RegisteredPairResult {
    RegisteredPairSpec spec;
    std::size_t observer = 0;
    std::size_t representative_root_world = 0;
    std::string representative_root_action;
    std::uint64_t continuation_seed = 0;
    std::size_t authoritative_actions = 0;
    PairComparison comparison;

    bool operator==(const RegisteredPairResult&) const = default;
};

// The natural complete representatives retained for a registered pair that
// FR1 could not classify as graveyard-order-only. FR2 never compares these
// complete states directly: it derives every treatment from first_state and
// copies only the declared public sequence(s) from second_state.
struct RegisteredRepresentativePair {
    RegisteredPairSpec spec;
    GameState first_state;
    GameState second_state;
    std::array<std::vector<CardId>, 2> original_decks;
    LearnedDecisionContext context;
    std::vector<PriorityAction> authoritative_actions;
    std::size_t observer = 0;
    std::size_t first_root_world = 0;
    std::string first_root_action;
    std::uint64_t continuation_seed = 0;

    bool operator==(
        const RegisteredRepresentativePair&) const = default;
};

struct RegisteredAnatomyReport {
    std::size_t bounded_root_macros = 0;
    std::size_t incomplete_root_macros = 0;
    std::size_t registered_pairs = 0;
    std::size_t blue_pairs = 0;
    std::size_t white_pairs = 0;
    std::size_t legacy_collision_rows = 0;
    std::size_t blue_collision_rows = 0;
    std::size_t white_collision_rows = 0;
    std::size_t reconstructed_pairs = 0;
    std::size_t graveyard_only_pairs = 0;
    std::size_t graveyard_only_rows = 0;
    std::size_t additional_public_difference_pairs = 0;
    std::size_t additional_public_difference_rows = 0;
    std::size_t equivalent_pairs = 0;
    std::string registered_row_identity_sha256;
    std::vector<std::string> reconstruction_failures;
    std::vector<RegisteredPairResult> pairs;
    std::vector<RegisteredRepresentativePair>
        additional_public_representatives;

    bool exact_registered_anatomy() const;
    bool exact_registered_rejection() const;
    bool passed() const;
    bool operator==(const RegisteredAnatomyReport&) const = default;
};

// Reconstructs only the two implicated production roots at the frozen
// registered root-world coordinates. It opens no successor bank and derives
// no new seed. Each registered information-set pair is rebuilt with one
// member's hidden state copied into both sides before comparison.
RegisteredAnatomyReport reconstruct_registered_anatomy(
    std::shared_ptr<const LearnedModel> frozen_c16);

enum class SequenceTreatment : std::uint8_t {
    Graveyards,
    ObserverHand,
    GraveyardsAndObserverHand,
};

std::string_view sequence_treatment_name(
    SequenceTreatment treatment);

struct FactorialContrast {
    RegisteredPairSpec spec;
    SequenceTreatment treatment =
        SequenceTreatment::Graveyards;
    std::uint64_t continuation_seed = 0;
    bool relevant_multisets_preserved = false;
    bool treatment_nontrivial = false;
    bool treated_factor_only = false;
    std::string baseline_information_set;
    std::string expected_information_set;
    std::string intervention_information_set;
    bool information_identity_equal = false;
    bool shared_authoritative_actions = false;
    std::vector<ActionComparison> actions;

    bool equivalent() const;
    bool operator==(const FactorialContrast&) const = default;
};

struct FactorialReport {
    RegisteredAnatomyReport anatomy;
    std::size_t eligible_pairs = 0;
    std::size_t contrasts = 0;
    std::size_t action_comparisons = 0;
    std::size_t graveyard_contrasts_equal = 0;
    std::size_t observer_hand_contrasts_equal = 0;
    std::size_t combined_contrasts_equal = 0;
    bool life_perturbation_detected = false;
    FactorialContrast life_control;
    std::size_t wrong_mask_controls_detected = 0;
    std::vector<FactorialContrast> wrong_mask_controls;
    std::vector<std::string> failures;
    std::vector<FactorialContrast> results;

    bool infrastructure_valid() const;
    bool passed() const;
    bool operator==(const FactorialReport&) const = default;
};

// Reconstructs the exact FR1 44/177 anatomy, then evaluates the three
// preregistered sequence-order interventions for each of the five retained
// White representatives. The production entry point pins the exact C16
// artifact; no successor bank or new seed is opened.
FactorialReport evaluate_sequence_factorial(
    std::shared_ptr<const LearnedModel> frozen_c16);

struct ResidualCatalogRow {
    std::string root_stable_id;
    std::string first_information_set;
    std::string second_information_set;
    std::string first_quotient_information_set;
    std::string second_quotient_information_set;
    std::string action_descriptor;
    PriorityAction action;
    std::string first_legacy_consequence;
    std::string second_legacy_consequence;
    std::string first_quotient_consequence;
    std::string second_quotient_consequence;
    bool policy_features_bit_identical = false;

    bool operator==(const ResidualCatalogRow&) const = default;
};

struct ResidualConflictReport {
    FactorialReport prerequisite;
    std::size_t controlled_pairs = 0;
    std::size_t paired_actions = 0;
    std::size_t source_state_action_instances = 0;
    std::size_t exact_legacy_identity_pairs = 0;
    std::size_t quotient_information_pairs_equal = 0;
    std::size_t policy_feature_rows_bit_identical = 0;
    std::size_t legacy_leaf_conflict_pairs = 0;
    std::size_t quotient_leaf_conflict_pairs = 0;
    std::size_t legacy_consequence_conflicts = 0;
    std::size_t residual_quotient_conflicts = 0;
    bool changed_graveyard_multiset_distinct = false;
    bool life_total_distinct = false;
    bool hidden_repartition_aliased = false;
    std::string catalog_sha256;
    std::vector<std::string> infrastructure_failures;
    std::vector<ResidualCatalogRow> catalog_rows;

    bool infrastructure_valid() const;
    bool passed() const;
    bool operator==(const ResidualConflictReport&) const = default;
};

// Reconstructs the exact registered FR1 states once, verifies the complete
// FR1/FR2 prerequisite, and measures only the 44-pair/177-action residual
// under the separately domain-wrapped public-graveyard quotient projection.
// The production entry point pins the exact C16 artifact and the independently
// frozen catalog digest.
ResidualConflictReport evaluate_registered_residual_conflicts(
    std::shared_ptr<const LearnedModel> frozen_c16);

namespace testing {

// Portable unit-test seam. It replays the same frozen roots and seeds but
// deliberately omits the production C16 fingerprint pin, allowing a tiny
// Value model to exercise reconstruction in a clean clone without the
// gitignored 3 MB artifact. Production evidence must use the pinned API.
RegisteredAnatomyReport reconstruct_registered_anatomy(
    std::shared_ptr<const LearnedModel> value_model);

FactorialReport evaluate_sequence_factorial(
    std::shared_ptr<const LearnedModel> value_model);

// Portable derivation seam. Unlike the production entry point, this does not
// require the gitignored exact-C16 artifact or the not-yet-frozen catalog
// literal. It still reconstructs the same registered roots, coordinates,
// identities, actions, controls, and catalog.
ResidualConflictReport evaluate_registered_residual_conflicts(
    std::shared_ptr<const LearnedModel> value_model);

} // namespace testing

} // namespace old_school::fq0_causal_quotient
