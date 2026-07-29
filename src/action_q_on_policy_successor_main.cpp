#include "old_school/action_q_on_policy_successor.hpp"

#include "old_school/artifact_integrity.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace op1 =
    old_school::action_q_on_policy_successor;
namespace g4b =
    old_school::action_q_nested_actor_broad_distill;
namespace diagnostic =
    old_school::action_q_nested_actor_diagnostic;

constexpr std::string_view kC16ArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
constexpr std::uintmax_t kC16ArtifactBytes = 3111437;
constexpr std::string_view kC16ArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";

using ArtifactSnapshot =
    old_school::artifact_integrity::RegularFileSnapshot;

ArtifactSnapshot c16_artifact_snapshot() {
    const auto snapshot =
        old_school::artifact_integrity::
            snapshot_regular_file(
                std::string(kC16ArtifactPath));
    if (snapshot.byte_size != kC16ArtifactBytes ||
        snapshot.sha256 != kC16ArtifactSha256) {
        throw std::runtime_error(
            "AQ4-OP1 C16 artifact identity drifted");
    }
    return snapshot;
}

void require_c16_artifact_unchanged(
    const ArtifactSnapshot& expected) {
    if (c16_artifact_snapshot() != expected) {
        throw std::runtime_error(
            "AQ4-OP1 C16 artifact changed during the command");
    }
}

std::shared_ptr<const old_school::LearnedModel>
load_c16(const ArtifactSnapshot& before) {
    const auto artifact =
        old_school::load_learned_value_challenger_artifact(
            std::string(kC16ArtifactPath),
            800, 424242, 16);
    const auto model = artifact.model();
    require_c16_artifact_unchanged(before);
    if (old_school::learned_model_fingerprint(model) !=
        g4b::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ4-OP1 loaded C16 fingerprint drifted");
    }
    return model;
}

struct WarmReconstruction {
    std::shared_ptr<const old_school::LearnedModel> model;
    diagnostic::PreflightReport frozen_preflight;
    std::size_t replayed_g4b_train_labels = 0;
    std::size_t replayed_g4b_dev_labels = 0;
};

WarmReconstruction reconstruct_warm_parent(
    const std::shared_ptr<const old_school::LearnedModel>& c16,
    const ArtifactSnapshot& artifact) {
    diagnostic::PreflightReport preflight =
        diagnostic::run_preflight(
            c16, g4b::preflight_recipe());
    require_c16_artifact_unchanged(artifact);
    if (!g4b::preflight_exact(preflight)) {
        throw std::runtime_error(
            "AQ4-OP1 frozen G4B preflight replay drifted");
    }

    const g4b::Census census =
        g4b::collect_census(c16);
    g4b::require_frozen_census(census);
    require_c16_artifact_unchanged(artifact);
    const g4b::Corpus corpus =
        g4b::collect_corpus(
            c16, census, preflight);
    require_c16_artifact_unchanged(artifact);
    const g4b::FitReport fit =
        g4b::fit(corpus, c16);
    require_c16_artifact_unchanged(artifact);
    if (!fit.candidate ||
        old_school::learned_model_fingerprint(
            fit.candidate) !=
            op1::kRequiredWarmParentFingerprint ||
        !fit.parent_immutable ||
        !fit.repeated_fit_bit_identical ||
        !fit.only_priority_component_changed) {
        throw std::runtime_error(
            "AQ4-OP1 G4B warm-parent reconstruction drifted");
    }
    return {
        .model = fit.candidate,
        .frozen_preflight = std::move(preflight),
        .replayed_g4b_train_labels = corpus.train.size(),
        .replayed_g4b_dev_labels = corpus.dev.size(),
    };
}

int run_census(
    const WarmReconstruction& warm,
    const ArtifactSnapshot& artifact) {
    const op1::Census census =
        op1::collect_census(warm.model);
    op1::validate_census(census);
    require_c16_artifact_unchanged(artifact);
    op1::print_census(
        std::cout, census,
        warm.replayed_g4b_train_labels,
        warm.replayed_g4b_dev_labels);
    return 0;
}

int run_experiment(
    const std::shared_ptr<const old_school::LearnedModel>& c16,
    const WarmReconstruction& warm,
    const ArtifactSnapshot& artifact) {
    const op1::Census census =
        op1::collect_census(warm.model);
    op1::require_frozen_census(census);
    require_c16_artifact_unchanged(artifact);
    const op1::Corpus corpus =
        op1::collect_corpus(
            warm.model, census,
            warm.frozen_preflight);
    require_c16_artifact_unchanged(artifact);
    const op1::FitReport fit =
        op1::fit(corpus, warm.model);
    require_c16_artifact_unchanged(artifact);
    if (!fit.candidate) {
        throw std::runtime_error(
            "AQ4-OP1 fit returned no candidate");
    }
    const op1::OfflineReport offline =
        op1::evaluate_offline(
            corpus, fit, warm.frozen_preflight,
            warm.model, fit.candidate);
    require_c16_artifact_unchanged(artifact);
    op1::print_offline(
        std::cout, corpus, fit, offline);
    if (!offline.gate_passed()) {
        return 1;
    }

    const old_school::BotBenchmarkSummary selector =
        op1::run_selector(
            corpus, warm.frozen_preflight,
            c16, warm.model, fit.candidate,
            fit, offline);
    require_c16_artifact_unchanged(artifact);
    const op1::SelectorDisposition disposition =
        op1::classify_selector(selector);
    op1::print_selector(
        std::cout, selector, disposition);
    return disposition == op1::SelectorDisposition::Reject
               ? 1
               : 0;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    const auto command = op1::parse_command(arguments);
    if (!command.has_value()) {
        op1::print_usage(std::cerr);
        return 2;
    }
    try {
        // Fail closed before loading or reconstructing any model when the
        // measured source census has not yet been frozen into source.
        if (*command == op1::Command::Run &&
            !op1::frozen_census_seal_populated()) {
            throw std::runtime_error(
                "AQ4-OP1 census hash/count seal is not frozen; --run is sealed");
        }
        const auto artifact = c16_artifact_snapshot();
        const auto c16 = load_c16(artifact);
        const WarmReconstruction warm =
            reconstruct_warm_parent(c16, artifact);
        const int result =
            *command == op1::Command::Census
                ? run_census(warm, artifact)
                : run_experiment(
                      c16, warm, artifact);
        require_c16_artifact_unchanged(artifact);
        return result;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=action_q_on_policy_successor_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
