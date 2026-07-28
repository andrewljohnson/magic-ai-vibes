#include "old_school/fq0_bellman_audit.hpp"
#include "old_school/fq0_causal_quotient.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    namespace audit =
        old_school::fq0_bellman_audit;
    namespace causal =
        old_school::fq0_causal_quotient;
    if (argc != 1) {
        const std::string program =
            argc > 0 && argv[0] != nullptr
                ? argv[0]
                : "old-school-fq0-causal-quotient";
        std::cerr << "Usage: " << program << '\n';
        return 2;
    }
    try {
        const auto model =
            old_school::
                load_learned_value_challenger_artifact(
                    std::string(audit::kModelArtifactPath),
                    audit::kModelTrainingGames,
                    audit::kModelTrainingSeed,
                    audit::kModelGenerations)
                .model();
        const causal::DirectControlReport direct_first =
            causal::compare_direct_controls(model);
        const causal::DirectControlReport direct_second =
            causal::compare_direct_controls(model);
        const causal::ResidualConflictReport first =
            causal::evaluate_registered_residual_conflicts(
                model);
        const causal::ResidualConflictReport second =
            causal::evaluate_registered_residual_conflicts(
                model);
        const bool repeated =
            direct_first == direct_second &&
            first == second;
        const causal::FactorialReport& factorial =
            first.prerequisite;
        const causal::RegisteredAnatomyReport& anatomy =
            factorial.anatomy;
        if (!direct_first.passed() ||
            !anatomy.exact_registered_rejection() ||
            !factorial.infrastructure_valid() ||
            !first.infrastructure_valid() ||
            !repeated) {
            throw std::logic_error(
                "direct controls, exact FR1 rejection, FR2 "
                "infrastructure, FR3 infrastructure, or "
                "repeatability drifted");
        }
        const bool passed = first.passed();
        std::cout
            << "FR1/FR2/FR3 sequence-order causal quotient\n"
            << "direct_blue="
            << (direct_first.blue_counter.equivalent()
                    ? 1
                    : 0)
            << "/"
            << direct_first.blue_counter.actions.size()
            << " direct_white="
            << (direct_first.buried_white.equivalent()
                    ? 1
                    : 0)
            << "/"
            << direct_first.buried_white.actions.size()
            << " life_control="
            << (direct_first.life_perturbation_detected
                    ? 1
                    : 0)
            << '\n'
            << "root_macros=" << anatomy.bounded_root_macros
            << " incomplete="
            << anatomy.incomplete_root_macros
            << "\npairs=" << anatomy.registered_pairs
            << " blue_pairs=" << anatomy.blue_pairs
            << " white_pairs=" << anatomy.white_pairs
            << "\nlegacy_rows="
            << anatomy.legacy_collision_rows
            << " blue_rows="
            << anatomy.blue_collision_rows
            << " white_rows="
            << anatomy.white_collision_rows
            << "\nrow_identity_sha256="
            << anatomy.registered_row_identity_sha256
            << "\nreconstructed="
            << anatomy.reconstructed_pairs
            << " graveyard_only="
            << anatomy.graveyard_only_pairs << "/"
            << anatomy.graveyard_only_rows
            << " additional_public_difference="
            << anatomy.additional_public_difference_pairs
            << "/"
            << anatomy.additional_public_difference_rows
            << " equivalent=" << anatomy.equivalent_pairs
            << "\nfr1_verdict=REJECT"
            << "\nfr2_pairs=" << factorial.eligible_pairs
            << " contrasts=" << factorial.contrasts
            << " action_comparisons="
            << factorial.action_comparisons
            << "\nfr2_equal graveyards="
            << factorial.graveyard_contrasts_equal
            << " observer_hand="
            << factorial.observer_hand_contrasts_equal
            << " combined="
            << factorial.combined_contrasts_equal
            << "\nfr2_controls wrong_masks="
            << factorial.wrong_mask_controls_detected
            << "/"
            << factorial.wrong_mask_controls.size()
            << " life="
            << (factorial.life_perturbation_detected
                    ? 1
                    : 0)
            << "\nrepeat_bit_identical="
            << (repeated ? 1 : 0)
            << "\nfr2_verdict="
            << (factorial.passed() ? "PASS" : "REJECT")
            << "\nfr3_pairs=" << first.controlled_pairs
            << " paired_actions=" << first.paired_actions
            << " source_instances="
            << first.source_state_action_instances
            << "\nfr3_identity exact_legacy="
            << first.exact_legacy_identity_pairs
            << " quotient_equal="
            << first.quotient_information_pairs_equal
            << " feature_bit_identical="
            << first.policy_feature_rows_bit_identical
            << "/" << first.paired_actions
            << "\nfr3_consequences legacy_conflicts="
            << first.legacy_consequence_conflicts
            << " residual_quotient_conflicts="
            << first.residual_quotient_conflicts
            << "\nfr3_leaf legacy_conflicts="
            << first.legacy_leaf_conflict_pairs
            << " residual_quotient_conflicts="
            << first.quotient_leaf_conflict_pairs
            << "\nfr3_controls graveyard_multiset="
            << (first.changed_graveyard_multiset_distinct
                    ? 1
                    : 0)
            << " life="
            << (first.life_total_distinct ? 1 : 0)
            << " hidden_repartition="
            << (first.hidden_repartition_aliased ? 1 : 0)
            << "\nfr3_catalog_sha256="
            << first.catalog_sha256
            << "\nfr3_verdict="
            << (first.passed() ? "PASS" : "REJECT")
            << '\n';
        for (const std::string& failure :
             anatomy.reconstruction_failures) {
            std::cout
                << "fr1_failure=" << failure << '\n';
        }
        for (const std::string& failure :
             factorial.failures) {
            std::cout
                << "fr2_failure=" << failure << '\n';
        }
        for (const std::string& failure :
             first.infrastructure_failures) {
            std::cout
                << "fr3_failure=" << failure << '\n';
        }
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr
            << "FR1/FR2/FR3 infrastructure failure: "
            << error.what() << '\n';
        return 2;
    }
}
