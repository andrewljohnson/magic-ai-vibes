#include "old_school/action_q_nested_actor_diagnostic.hpp"

#include "old_school/game.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq4 =
    old_school::action_q_nested_actor_diagnostic;

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
        aq4::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ4-D1 loaded parent fingerprint drifted");
    }
    return parent;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    if (!aq4::parse_command(arguments).has_value()) {
        aq4::print_usage(std::cerr);
        return 2;
    }
    try {
        const aq4::Report report =
            aq4::diagnose(load_parent());
        aq4::print_report(std::cout, report);
        return report.gate_passed() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=action_q_nested_actor_diagnostic_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
