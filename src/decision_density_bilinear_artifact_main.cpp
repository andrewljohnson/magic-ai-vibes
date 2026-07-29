#include "old_school/decision_density_bilinear.hpp"
#include "old_school/learned_priority_bilinear_artifact.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-decision-density-bilinear-artifact "
           "--publish\n";
}

std::filesystem::path production_path(
    const char* executable) {
    return std::filesystem::absolute(executable)
               .parent_path() /
           "model-cache" /
           old_school::
               learned_priority_bilinear_artifact::
                   kProductionFilename;
}

} // namespace

int main(int argc, char** argv) {
    namespace aq19 =
        old_school::decision_density_bilinear;
    namespace artifact =
        old_school::
            learned_priority_bilinear_artifact;
    if (argc != 2 ||
        std::string_view(argv[1]) != "--publish") {
        print_usage(std::cerr);
        return 2;
    }
    try {
        const aq19::RunReport report =
            aq19::run_offline();
        if (!report.gate.passed() ||
            report.selector_seed_authorized ||
            report.selector_opened ||
            report.gameplay_games != 0 ||
            report.pilot_licensed ||
            report.fast_go) {
            throw std::runtime_error(
                "offline publication preconditions failed");
        }
        if (report.full_fit.parameter_sha256 !=
            artifact::kProductionParameterSha256) {
            throw std::runtime_error(
                "fitted parameter SHA-256 is " +
                report.full_fit.parameter_sha256 +
                ", expected " +
                std::string(
                    artifact::
                        kProductionParameterSha256));
        }

        const std::filesystem::path destination =
            production_path(argv[0]);
        std::filesystem::create_directories(
            destination.parent_path());
        const artifact::Identity identity =
            artifact::publish_atomic_no_replace(
                destination,
                report.full_fit.parameters,
                artifact::production_contract());
        if (identity.bytes !=
                artifact::kProductionArtifactBytes ||
            identity.file_sha256 !=
                artifact::kProductionFileSha256) {
            throw std::runtime_error(
                "published AQ19 file identity drifted");
        }
        std::cout
            << "published=" << destination.string()
            << " bytes=" << identity.bytes
            << " file_sha256="
            << identity.file_sha256
            << " parameter_sha256="
            << identity.parameter_sha256
            << " parent_fingerprint="
            << identity.parent_fingerprint
            << " gameplay_games="
            << report.gameplay_games << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "AQ19 artifact publication failed: "
                  << error.what() << '\n';
        return 1;
    }
}
