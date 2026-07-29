#include "old_school/action_q_long_horizon_diagnostic.hpp"

#include "old_school/game.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq3 =
    old_school::action_q_long_horizon_diagnostic;

constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";

std::shared_ptr<const old_school::LearnedModel>
load_parent() {
    const auto artifact =
        old_school::load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            800, 424242, 16);
    const auto parent = artifact.model();
    if (old_school::learned_model_fingerprint(parent) !=
        aq3::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ3-D0 loaded parent fingerprint drifted");
    }
    return parent;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    if (!aq3::parse_command(arguments).has_value()) {
        aq3::print_usage(std::cerr);
        return 2;
    }
    try {
        const auto parent = load_parent();
        const aq3::Report report =
            aq3::diagnose(parent);
        aq3::print_report(std::cout, report);
        return report.gate_passed() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=action_q_long_horizon_diagnostic_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
