#include "old_school/fq0_bellman_audit.hpp"
#include "old_school/fq4_priority_fit.hpp"

#include <bit>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace {

std::string double_bits(double value) {
    std::ostringstream output;
    output << "0x" << std::hex << std::setfill('0')
           << std::setw(16)
           << std::bit_cast<std::uint64_t>(value);
    return output.str();
}

} // namespace

int main(int argc, char* argv[]) {
    namespace audit = old_school::fq0_bellman_audit;
    namespace fit = old_school::fq4_priority_fit;
    if (argc != 1) {
        const std::string program =
            argc > 0 && argv[0] != nullptr
                ? argv[0]
                : "old-school-fq4-priority-fit";
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
        const fit::FitReport report =
            fit::fit_production(parent);
        const fit::ExitClassification classification =
            fit::classify_exit(report);

        std::cout
                  << std::setprecision(
                         std::numeric_limits<double>::
                             max_digits10)
                  << "FQ4-D0 production-surface Priority microfit\n"
                  << "parent_fingerprint="
                  << report.parent_fingerprint << '\n'
                  << "candidate_fingerprint="
                  << report.candidate_fingerprint << '\n'
                  << "roots=" << report.roots.size()
                  << " constraints="
                  << report.discovered_constraints
                  << " parent_below_0.005="
                  << report.parent_margins_below_gate
                  << " candidate_at_0.005="
                  << report.candidate_margins_at_gate
                  << '\n';
        for (const auto& root : report.roots) {
            std::cout
                << "root=" << root.stable_id
                << " legal=" << root.descriptors.size()
                << " constraints=" << root.constraints.size()
                << " recipe="
                << (root.production_recipe_exact ? 1 : 0)
                << " immutable_base="
                << (root
                            .production_base_and_accounting_bit_identical
                        ? 1
                        : 0)
                << '\n';
            for (const auto& constraint :
                 root.constraints) {
                std::cout
                    << "constraint=" << root.stable_id
                    << ":" << constraint.descriptor
                    << " worlds="
                    << constraint.strict_worlds << "/"
                    << fit::kDominanceWorlds
                    << " active="
                    << (constraint
                                .active_projection_constraint
                            ? 1
                            : 0)
                    << " parent_margin="
                    << constraint.parent_margin
                    << " parent_margin_bits="
                    << double_bits(
                           constraint.parent_margin)
                    << " candidate_margin="
                    << constraint.candidate_margin
                    << " candidate_margin_bits="
                    << double_bits(
                           constraint.candidate_margin)
                    << '\n';
            }
        }
        for (const auto& control : report.controls) {
            std::cout
                << "control=" << control.name
                << " verdict="
                << (control.passed ? "PASS" : "REJECT")
                << '\n';
        }
        std::cout
            << "isolation parent_immutable="
            << (report.parent_immutable ? 1 : 0)
            << " only_priority="
            << (report.only_priority_component_changed ? 1 : 0)
            << " repeat="
            << (report.repeated_fit_bit_identical ? 1 : 0)
            << " hidden="
            << (report.hidden_repartition_bit_identical ? 1 : 0)
            << " order="
            << (report.action_order_bit_identical ? 1 : 0)
            << " base_accounting="
            << (report
                        .all_production_base_and_accounting_bit_identical
                    ? 1
                    : 0)
            << '\n';
        for (const std::string& failure :
             report.scientific_failures) {
            std::cout << "scientific_failure="
                      << failure << '\n';
        }
        for (const std::string& failure :
             report.infrastructure_failures) {
            std::cout << "infrastructure_failure="
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
            << "FQ4-D0 infrastructure failure: "
            << error.what() << '\n';
        return static_cast<int>(
            fit::ExitClassification::
                InfrastructureFailure);
    }
}
