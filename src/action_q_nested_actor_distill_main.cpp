#include "old_school/action_q_nested_actor_distill.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq =
    old_school::action_q_nested_actor_distill;
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
        aq::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ4-G1 loaded parent fingerprint drifted");
    }
    return parent;
}

int run_census(
    const std::shared_ptr<const old_school::LearnedModel>&
        parent) {
    const aq::Census census = aq::collect_census(parent);
    aq::validate_census(census);
    aq::print_census(std::cout, census);
    return 0;
}

int run_experiment(
    const std::shared_ptr<const old_school::LearnedModel>&
        parent) {
    // The source manifest and frozen identity are reconstructed before
    // opening any reserved AQ4 preflight or teacher coordinate.
    const aq::Census census = aq::collect_census(parent);
    aq::validate_census(census);
    aq::require_frozen_census(census);

    const aq4::PreflightReport preflight =
        aq4::run_preflight(parent, aq::preflight_recipe());
    aq::print_preflight(std::cout, preflight);
    if (!preflight.gate_passed()) {
        throw std::runtime_error(
            "AQ4-G1 preflight failed before corpus scoring");
    }

    const aq::Corpus corpus =
        aq::collect_corpus(parent, census);
    aq::validate_corpus(corpus);
    const aq::FitReport fit = aq::fit(corpus, parent);
    if (!fit.candidate) {
        throw std::runtime_error(
            "AQ4-G1 fit returned no candidate");
    }
    const aq::OfflineReport offline =
        aq::evaluate_offline(
            corpus, fit, parent, fit.candidate);
    aq::print_offline(std::cout, corpus, fit, offline);
    if (!offline.gate_passed()) {
        return 1;
    }

    const old_school::BotBenchmarkSummary selector =
        aq::run_selector(
            parent, fit.candidate, fit, offline);
    const aq::SelectorDisposition disposition =
        aq::classify_selector(selector);
    aq::print_selector(
        std::cout, selector, disposition);
    return disposition == aq::SelectorDisposition::Reject
               ? 1
               : 0;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    const auto command = aq::parse_command(arguments);
    if (!command.has_value()) {
        aq::print_usage(std::cerr);
        return 2;
    }
    try {
        const auto parent = load_parent();
        if (*command == aq::Command::Census) {
            return run_census(parent);
        }
        return run_experiment(parent);
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=action_q_nested_actor_distill_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
