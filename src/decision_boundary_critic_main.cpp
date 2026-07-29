#include "old_school/decision_boundary_critic.hpp"
#include "old_school/decision_boundary_critic_gate.hpp"

#include "old_school/artifact_integrity.hpp"

#include <exception>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace dbc =
    old_school::decision_boundary_critic;
namespace dbc_gate =
    old_school::decision_boundary_critic_gate;

constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
constexpr std::uintmax_t kParentArtifactBytes = 3111437;
constexpr std::string_view kParentArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";

old_school::artifact_integrity::RegularFileSnapshot
parent_artifact_snapshot() {
    const auto snapshot =
        old_school::artifact_integrity::
            snapshot_regular_file(
                std::string(kParentArtifactPath));
    if (snapshot.byte_size != kParentArtifactBytes ||
        snapshot.sha256 != kParentArtifactSha256) {
        throw std::runtime_error(
            "AQ10-DBC parent artifact identity drifted");
    }
    return snapshot;
}

void require_parent_artifact_unchanged(
    const old_school::artifact_integrity::
        RegularFileSnapshot& expected) {
    if (parent_artifact_snapshot() != expected) {
        throw std::runtime_error(
            "AQ10-DBC parent artifact changed during run");
    }
}

std::shared_ptr<const old_school::LearnedModel>
load_parent(
    const old_school::artifact_integrity::
        RegularFileSnapshot& before) {
    const auto artifact =
        old_school::load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            800, 424242, 16);
    const auto parent = artifact.model();
    require_parent_artifact_unchanged(before);
    if (old_school::learned_model_fingerprint(parent) !=
            dbc::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ10-DBC loaded parent fingerprint drifted");
    }
    return parent;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    const bool census_mode =
        dbc::parse_census_command(arguments);
    const bool run_mode =
        arguments.size() == 1 &&
        arguments.front() == "--run";
    if (!census_mode && !run_mode) {
        dbc::print_usage(std::cerr);
        return 2;
    }

    try {
        const auto parent_artifact =
            parent_artifact_snapshot();
        const auto parent =
            load_parent(parent_artifact);
        if (census_mode) {
            const dbc::Census census =
                dbc::collect_census(parent);
            require_parent_artifact_unchanged(
                parent_artifact);
            dbc::print_census(std::cout, census);
            require_parent_artifact_unchanged(
                parent_artifact);
            return 0;
        }

        const dbc::RunReport offline =
            dbc::run(parent);
        require_parent_artifact_unchanged(parent_artifact);
        dbc::print_run(std::cout, offline);
        if (!offline.gate.passed()) {
            std::cout
                << "final_result=REJECT stage=offline"
                << " mechanism_opened=0 selector_opened=0"
                << " pilot_licensed=0\n";
            require_parent_artifact_unchanged(
                parent_artifact);
            return 0;
        }

        const dbc_gate::MechanismReport mechanism =
            dbc_gate::run_mechanism_gate(
                parent, offline.fit);
        require_parent_artifact_unchanged(parent_artifact);
        dbc_gate::print_mechanism_report(
            std::cout, mechanism);
        if (!mechanism.selector_licensed()) {
            std::cout
                << "final_result=REJECT stage=mechanism"
                << " selector_opened=0 pilot_licensed=0\n";
            require_parent_artifact_unchanged(
                parent_artifact);
            return 0;
        }

        const dbc_gate::SelectorReport selector =
            dbc_gate::run_selector(parent, offline.fit);
        require_parent_artifact_unchanged(parent_artifact);
        dbc_gate::print_selector_report(
            std::cout, selector);
        std::cout
            << "final_result="
            << (selector.pilot_licensed
                    ? "PILOT_LICENSED"
                    : "REJECT")
            << " stage=selector selector_opened=1"
            << " pilot_licensed="
            << selector.pilot_licensed
            << " fast_go=" << selector.fast_go
            << " strength_claim=0 champion_replaced=0\n";
        require_parent_artifact_unchanged(parent_artifact);
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=decision_boundary_critic_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
