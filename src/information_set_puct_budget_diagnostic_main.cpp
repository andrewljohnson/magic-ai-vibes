#include "old_school/information_set_puct_budget_diagnostic.hpp"

#include "old_school/artifact_integrity.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace isp0 =
    old_school::information_set_puct_preflight;
namespace isp1 =
    old_school::information_set_puct_budget_diagnostic;

std::shared_ptr<const old_school::LearnedModel>
load_parent() {
    const auto snapshot =
        old_school::artifact_integrity::
            snapshot_regular_file(
                std::string(isp0::kRequiredParentPath));
    if (snapshot.byte_size !=
            isp0::kRequiredParentBytes ||
        snapshot.sha256 !=
            isp0::kRequiredParentSha256) {
        throw std::runtime_error(
            "ISP1 frozen C16 artifact bytes or SHA-256 "
            "drifted");
    }
    const auto artifact =
        old_school::
            load_learned_value_challenger_artifact(
                std::string(isp0::kRequiredParentPath),
                800, 424242, 16);
    const auto parent = artifact.model();
    if (old_school::learned_model_fingerprint(parent) !=
        isp0::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "ISP1 frozen C16 model fingerprint drifted");
    }
    return parent;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2 ||
        std::string_view(argv[1]) != "--run-isp1") {
        std::cerr
            << "Usage: "
            << (argc > 0
                    ? argv[0]
                    : "old-school-information-set-puct-"
                      "budget-diagnostic")
            << " --run-isp1\n";
        return 2;
    }
    try {
        const auto parent = load_parent();
        const isp1::DiagnosticReport report =
            isp1::run_diagnostic(parent);
        isp1::print_report(report, std::cout);
        return report.gate_passed() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=isp1_budget_diagnostic_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
