#include "old_school/action_q_nested_actor_anchor.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq =
    old_school::action_q_nested_actor_anchor;

constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";

static_assert(aq::kSelectorSeed != aq::g1::kSelectorSeed);
static_assert(aq::kSelectorSeed != aq::g2::kSelectorSeed);

std::shared_ptr<const old_school::LearnedModel>
load_parent() {
    const auto artifact =
        old_school::load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            800, 424242, 16);
    const auto parent = artifact.model();
    if (old_school::learned_model_fingerprint(parent) !=
        aq::g1::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ4-G3 loaded parent fingerprint drifted");
    }
    return parent;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    if (!aq::parse_command(arguments).has_value()) {
        aq::print_usage(std::cerr);
        return 2;
    }
    try {
        const auto parent = load_parent();
        const aq::OfflineRunReport report =
            aq::run_offline(parent);
        aq::print_offline(std::cout, report);
        if (!report.selection_ready()) {
            return 1;
        }
        const old_school::BotBenchmarkSummary selector =
            aq::run_selector(parent, report);
        aq::print_selector(std::cout, selector);
        return aq::g1::classify_selector(selector) ==
                       aq::g1::SelectorDisposition::Reject
                   ? 1
                   : 0;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=action_q_nested_actor_anchor_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
