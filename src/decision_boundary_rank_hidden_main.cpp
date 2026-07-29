#include "old_school/decision_boundary_rank_hidden.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/decision_boundary_critic_gate.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace dbc =
    old_school::decision_boundary_critic;
namespace dbc_gate =
    old_school::decision_boundary_critic_gate;
namespace direct =
    old_school::decision_boundary_rank_direct;
namespace hidden =
    old_school::decision_boundary_rank_hidden;

constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
constexpr std::uintmax_t kParentArtifactBytes = 3111437;
constexpr std::string_view kParentArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";
constexpr std::string_view kCachePath =
    "build/model-cache/"
    "old-school-aq10-dbc1-owner-safe-corpus-v1.bin";
constexpr std::uintmax_t kCacheBytes = 25886525;
constexpr std::string_view kCacheSha256 =
    "9234b10d7181d566d4dacb972fbb32bf20d2961eb34c4d95d7e92ece1622a4a4";

old_school::artifact_integrity::RegularFileSnapshot
exact_snapshot(
    std::string_view path, std::uintmax_t bytes,
    std::string_view sha256, std::string_view label) {
    const auto snapshot =
        old_school::artifact_integrity::
            snapshot_regular_file(std::string(path));
    if (snapshot.byte_size != bytes ||
        snapshot.sha256 != sha256) {
        throw std::runtime_error(
            std::string(label) + " identity drifted");
    }
    return snapshot;
}

std::shared_ptr<const old_school::LearnedModel>
load_parent(
    const old_school::artifact_integrity::
        RegularFileSnapshot& expected) {
    const auto artifact =
        old_school::load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            800, 424242, 16);
    const auto parent = artifact.model();
    if (exact_snapshot(
            kParentArtifactPath, kParentArtifactBytes,
            kParentArtifactSha256, "AQ12 parent") !=
            expected ||
        old_school::learned_model_fingerprint(parent) !=
            dbc::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ12 loaded parent identity drifted");
    }
    return parent;
}

void require_artifacts_unchanged(
    const old_school::artifact_integrity::
        RegularFileSnapshot& parent,
    const old_school::artifact_integrity::
        RegularFileSnapshot& cache) {
    if (exact_snapshot(
            kParentArtifactPath, kParentArtifactBytes,
            kParentArtifactSha256, "AQ12 parent") !=
            parent ||
        exact_snapshot(
            kCachePath, kCacheBytes,
            kCacheSha256, "AQ12 cache") != cache) {
        throw std::runtime_error(
            "AQ12 frozen input changed during run");
    }
}

void print_metrics(
    std::ostream& output, std::string_view split,
    const hidden::Metrics& parent,
    const hidden::Metrics& candidate) {
    output
        << "metrics split=" << split
        << " deck=equal"
        << " roots=" << parent.roots
        << " parent_listwise="
        << parent.equal_deck_listwise_cross_entropy
        << " candidate_listwise="
        << candidate.equal_deck_listwise_cross_entropy
        << " parent_regret="
        << parent.equal_deck_mean_regret
        << " candidate_regret="
        << candidate.equal_deck_mean_regret
        << " parent_top1="
        << parent.equal_deck_top_one_expected_agreement
        << " candidate_top1="
        << candidate.equal_deck_top_one_expected_agreement
        << " parent_stable_pair="
        << parent.equal_deck_stable_pair_agreement
        << " candidate_stable_pair="
        << candidate.equal_deck_stable_pair_agreement
        << " parent_bce="
        << parent.equal_deck_successor_bce
        << " candidate_bce="
        << candidate.equal_deck_successor_bce
        << " parent_brier="
        << parent.equal_deck_successor_brier
        << " candidate_brier="
        << candidate.equal_deck_successor_brier
        << " parent_bias="
        << parent.equal_deck_successor_bias
        << " candidate_bias="
        << candidate.equal_deck_successor_bias
        << " parent_ece="
        << parent.equal_deck_successor_ece
        << " candidate_ece="
        << candidate.equal_deck_successor_ece
        << '\n';
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const auto& before = parent.decks[deck];
        const auto& after = candidate.decks[deck];
        output
            << "metrics split=" << split
            << " deck="
            << old_school::deck_name(
                   static_cast<old_school::DeckId>(deck))
            << " roots=" << before.roots
            << " parent_listwise="
            << before.listwise_cross_entropy
            << " candidate_listwise="
            << after.listwise_cross_entropy
            << " parent_regret=" << before.mean_regret
            << " candidate_regret=" << after.mean_regret
            << " parent_top1="
            << before.top_one_expected_agreement
            << " candidate_top1="
            << after.top_one_expected_agreement
            << " stable_pairs=" << before.stable_pairs
            << " parent_stable_pair="
            << before.stable_pair_agreement
            << " candidate_stable_pair="
            << after.stable_pair_agreement
            << " parent_bce=" << before.successor_bce
            << " candidate_bce=" << after.successor_bce
            << " parent_brier="
            << before.successor_brier
            << " candidate_brier="
            << after.successor_brier
            << " parent_bias="
            << before.successor_bias
            << " candidate_bias="
            << after.successor_bias
            << " parent_ece="
            << before.successor_ece
            << " candidate_ece="
            << after.successor_ece
            << '\n';
    }
}

void print_offline(
    std::ostream& output,
    const hidden::Corpus& corpus,
    const hidden::OptimizerReport& fit,
    const hidden::ExactEvaluationReport& exact,
    const hidden::ModelIsolationReport& isolation,
    const hidden::OfflineGate& gate) {
    output << std::fixed << std::setprecision(9);
    output
        << "AQ12-DBC3-RANK-HIDDEN"
        << " source_digest=" << corpus.source_digest
        << " fit_tag=" << fit.config.fit_tag
        << " train_roots=" << corpus.train.roots.size()
        << " dev_roots=" << corpus.dev.roots.size()
        << '\n'
        << "optimizer steps=" << fit.completed_steps
        << " initial_objective=" << fit.initial_objective
        << " final_objective=" << fit.final_objective
        << " delta_l2=" << fit.delta_l2_norm
        << " final_gradient_l2="
        << fit.final_gradient_l2_norm
        << " maximum_preclip_gradient_l2="
        << fit.maximum_preclip_gradient_l2_norm
        << " clipped_steps=" << fit.clipped_steps
        << '\n'
        << "model parent=" << exact.parent_fingerprint
        << " candidate=" << exact.candidate_fingerprint
        << " changed_coordinates="
        << isolation.changed_coordinates
        << " parent_immutable=" << isolation.parent_immutable
        << " independent_delta_exact="
        << isolation.independent_delta_exact
        << " output_bias_frozen="
        << isolation.output_bias_frozen
        << " direct_path_frozen="
        << isolation.direct_path_frozen
        << " context_direct_frozen="
        << isolation.context_direct_path_frozen
        << " policy_heads_frozen="
        << isolation.all_policy_heads_frozen
        << " surrogate_engine_max_abs="
        << exact.maximum_surrogate_engine_cell_difference
        << '\n';
    print_metrics(
        output, "TRAIN",
        exact.parent_train, exact.candidate_train);
    print_metrics(
        output, "DEV",
        exact.parent_dev, exact.candidate_dev);
    for (const std::string& failure : gate.failures) {
        output << "offline_failure=" << failure << '\n';
    }
    output
        << "offline_gate passed=" << gate.passed()
        << " repeat=" << gate.repeated_optimizer_bit_identical
        << " recipe=" << gate.optimizer_recipe_exact
        << " objective=" << gate.objective_strictly_improved
        << " surrogate_engine="
        << gate.surrogate_engine_agreement
        << " exact_model_identity="
        << gate.exact_model_identity
        << " isolation=" << gate.model_isolation_passed
        << " train_listwise="
        << gate.train_listwise_strictly_improved
        << " train_regret="
        << gate.train_regret_strictly_improved
        << " dev_listwise="
        << gate.dev_listwise_strictly_improved
        << " dev_regret="
        << gate.dev_regret_strictly_improved
        << " dev_top1="
        << gate.dev_top_one_non_decreasing
        << " dev_stable_pair="
        << gate.dev_stable_pair_non_decreasing
        << " dev_bce_guard="
        << gate.dev_successor_bce_guard
        << '\n';
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2 ||
        std::string_view(argv[1]) != "--run") {
        std::cerr
            << "Usage: "
               "old-school-decision-boundary-rank-hidden --run\n";
        return 2;
    }

    try {
        const auto parent_artifact =
            exact_snapshot(
                kParentArtifactPath, kParentArtifactBytes,
                kParentArtifactSha256, "AQ12 parent");
        const auto cache_artifact =
            exact_snapshot(
                kCachePath, kCacheBytes,
                kCacheSha256, "AQ12 cache");
        const auto parent =
            load_parent(parent_artifact);
        const hidden::Corpus corpus =
            [&] {
                const dbc::Corpus source =
                    dbc::load_corpus_cache(
                        std::string(kCachePath),
                        parent);
                const direct::Corpus ranked =
                    direct::project_corpus(
                        source, parent);
                return hidden::project_corpus(
                    ranked, parent);
            }();
        require_artifacts_unchanged(
            parent_artifact, cache_artifact);

        const hidden::OptimizerReport fit =
            hidden::optimize(corpus.train);
        const hidden::OptimizerReport repeated_fit =
            hidden::optimize(corpus.train);
        const hidden::ModelIsolationReport isolation =
            hidden::apply_delta(parent, fit.delta);
        const hidden::ExactEvaluationReport exact =
            hidden::evaluate_exact(
                corpus, parent, isolation.candidate,
                fit.delta);
        const hidden::OfflineGate offline =
            hidden::evaluate_offline_gate(
                fit, repeated_fit, exact, isolation);
        require_artifacts_unchanged(
            parent_artifact, cache_artifact);
        print_offline(
            std::cout, corpus, fit, exact,
            isolation, offline);
        if (!offline.passed()) {
            std::cout
                << "final_result=REJECT stage=offline"
                << " mechanism_opened=0 selector_opened=0"
                << " pilot_licensed=0\n";
            return 0;
        }

        const auto mechanism =
            dbc_gate::run_candidate_mechanism_gate(
                parent, isolation.candidate,
                offline.passed());
        require_artifacts_unchanged(
            parent_artifact, cache_artifact);
        dbc_gate::print_mechanism_report(
            std::cout, mechanism);
        if (!mechanism.selector_licensed()) {
            std::cout
                << "final_result=REJECT stage=mechanism"
                << " selector_opened=0 pilot_licensed=0\n";
            return 0;
        }

        const auto selector =
            dbc_gate::run_candidate_selector(
                parent, isolation.candidate,
                hidden::kSelectorSeed,
                offline.passed() &&
                    mechanism.selector_licensed());
        require_artifacts_unchanged(
            parent_artifact, cache_artifact);
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
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=decision_boundary_rank_hidden_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
