#include "old_school/fq0_bellman_audit.hpp"
#include "old_school/fq4_priority_fit.hpp"

#include <algorithm>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

int main(int argc, char* argv[]) {
    namespace audit = old_school::fq0_bellman_audit;
    namespace fit = old_school::fq4_priority_fit;
    if (argc != 1) {
        const std::string program =
            argc > 0 && argv[0] != nullptr
                ? argv[0]
                : "old-school-fq4-priority-fit-d0b";
        std::cerr << "Usage: " << program << '\n';
        return static_cast<int>(
            fit::ExitClassification::
                InfrastructureFailure);
    }
    try {
        const auto parent =
            old_school::
                load_learned_value_challenger_artifact(
                    std::string(audit::kModelArtifactPath),
                    audit::kModelTrainingGames,
                    audit::kModelTrainingSeed,
                    audit::kModelGenerations)
                .model();
        const fit::D0bReport report =
            fit::fit_d0b_production(parent);
        const fit::ExitClassification classification =
            fit::classify_d0b_exit(report);

        std::cout
                  << std::setprecision(
                         std::numeric_limits<double>::
                             max_digits10)
                  << "FQ4-D0b fixed 512-epoch Priority microfit\n"
                  << "parent_fingerprint="
                  << report.anchor.fit.parent_fingerprint
                  << '\n'
                  << "training_input_sha256="
                  << report.anchor.fit.training_input_sha256
                  << '\n'
                  << "parent_below_0.005="
                  << report.parent_margins_below_gate
                  << '\n';
        for (const double margin :
             report.parent_margins) {
            std::cout
                << "parent_margin=" << margin << '\n';
        }
        std::cout
            << "anchor_epochs=" << report.anchor.epochs
            << " anchor_fingerprint="
            << report.anchor.fit.candidate_fingerprint
            << " anchor_kl="
            << report.anchor
                   .pooled_target_to_candidate_kl
            << '\n'
            << "treatment_epochs="
            << report.treatment.epochs
            << " treatment_fingerprint="
            << report.treatment.fit.candidate_fingerprint
            << " treatment_kl="
            << report.treatment
                   .pooled_target_to_candidate_kl
            << " roots="
            << report.treatment.fit.roots.size()
            << " constraints="
            << report.treatment.fit.discovered_constraints
            << " treatment_at_0.005="
            << report.treatment.fit
                   .candidate_margins_at_gate
            << "/" << fit::kExpectedDominanceConstraints
            << '\n';
        const std::size_t printable_roots = std::min({
            report.anchor.fit.roots.size(),
            report.treatment.fit.roots.size(),
            report.anchor.root_kl.size(),
            report.treatment.root_kl.size(),
        });
        for (std::size_t root = 0;
             root < printable_roots;
             ++root) {
            const auto& anchor_root =
                report.anchor.fit.roots[root];
            const auto& treatment_root =
                report.treatment.fit.roots[root];
            std::cout
                << "root=" << anchor_root.stable_id
                << " legal="
                << anchor_root.descriptors.size()
                << " constraints="
                << anchor_root.constraints.size()
                << " anchor_kl="
                << report.anchor.root_kl[root]
                       .target_to_candidate_kl
                << " treatment_kl="
                << report.treatment.root_kl[root]
                       .target_to_candidate_kl
                << '\n';
            const std::size_t printable_constraints =
                std::min(
                    anchor_root.constraints.size(),
                    treatment_root.constraints.size());
            for (std::size_t constraint = 0;
                 constraint < printable_constraints;
                 ++constraint) {
                std::cout
                    << "constraint="
                    << anchor_root.stable_id << ":"
                    << anchor_root.constraints[constraint]
                           .descriptor
                    << " worlds="
                    << treatment_root
                           .constraints[constraint]
                           .strict_worlds
                    << "/" << fit::kDominanceWorlds
                    << " parent_margin="
                    << anchor_root.constraints[constraint]
                           .parent_margin
                    << " anchor_margin="
                    << anchor_root.constraints[constraint]
                           .candidate_margin
                    << " treatment_margin="
                    << treatment_root
                           .constraints[constraint]
                           .candidate_margin
                    << '\n';
            }
        }
        for (const auto& control :
             report.treatment.fit.controls) {
            std::cout
                << "control=" << control.name
                << " verdict="
                << (control.passed ? "PASS" : "REJECT")
                << '\n';
        }
        std::cout
            << "contracts parent="
            << (report.parent_contract_qualified ? 1 : 0)
            << " anchor="
            << (report.anchor_contract_qualified ? 1 : 0)
            << " optimizer_only_epochs="
            << (report.optimizer_only_epochs_differ ? 1 : 0)
            << " input_identity="
            << (report.checkpoint_inputs_bit_identical
                    ? 1
                    : 0)
            << " kl_improved="
            << (report.target_kl_strictly_improved ? 1 : 0)
            << '\n'
            << "treatment_isolation parent_immutable="
            << (report.treatment.fit.parent_immutable
                    ? 1
                    : 0)
            << " only_priority="
            << (report.treatment.fit
                        .only_priority_component_changed
                    ? 1
                    : 0)
            << " repeat="
            << (report.treatment.fit
                        .repeated_fit_bit_identical
                    ? 1
                    : 0)
            << " hidden="
            << (report.treatment.fit
                        .hidden_repartition_bit_identical
                    ? 1
                    : 0)
            << " order="
            << (report.treatment.fit
                        .action_order_bit_identical
                    ? 1
                    : 0)
            << " base_accounting="
            << (report.treatment.fit
                        .all_production_base_and_accounting_bit_identical
                    ? 1
                    : 0)
            << '\n';
        for (const std::string& failure :
             report.scientific_failures) {
            std::cout
                << "scientific_failure="
                << failure << '\n';
        }
        for (const std::string& failure :
             report.infrastructure_failures) {
            std::cout
                << "infrastructure_failure="
                << failure << '\n';
        }
        std::cout
            << "verdict="
            << (classification ==
                        fit::ExitClassification::Pass
                    ? "PASS"
                    : classification ==
                              fit::ExitClassification::
                                  ScientificReject
                          ? "REJECT"
                          : "INFRASTRUCTURE_FAILURE")
            << '\n';
        return static_cast<int>(classification);
    } catch (const std::exception& error) {
        std::cerr
            << "FQ4-D0b infrastructure failure: "
            << error.what() << '\n';
        return static_cast<int>(
            fit::ExitClassification::
                InfrastructureFailure);
    }
}
