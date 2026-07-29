#include "old_school/decision_boundary_adaptive_trunk.hpp"

#include "old_school/action_q_nested_actor_distill.hpp"
#include "old_school/artifact_integrity.hpp"

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

namespace adaptive =
    old_school::decision_boundary_adaptive_trunk;
namespace dbc =
    old_school::decision_boundary_critic;
namespace g1 =
    old_school::action_q_nested_actor_distill;
namespace offline =
    old_school::action_q_offline_gate;

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
            kParentArtifactSha256, "AQ14 parent") !=
            expected ||
        old_school::learned_model_fingerprint(parent) !=
            dbc::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ14 loaded parent identity drifted");
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
            kParentArtifactSha256, "AQ14 parent") !=
            parent ||
        exact_snapshot(
            kCachePath, kCacheBytes,
            kCacheSha256, "AQ14 cache") != cache) {
        throw std::runtime_error(
            "AQ14 frozen input changed during run");
    }
}

void print_metrics(
    std::string_view split,
    const adaptive::Metrics& parent,
    const adaptive::Metrics& candidate) {
    const auto print_row =
        [&](std::string_view deck,
            const auto& parent_pair,
            const auto& candidate_pair,
            const auto& parent_rank,
            const auto& candidate_rank) {
            std::cout
                << "metrics split=" << split
                << " deck=" << deck
                << " roots=" << parent_pair.roots
                << " all_tied="
                << parent_pair.all_tied_roots
                << " unordered_pairs="
                << parent_pair.unordered_pairs
                << " eligible_pairs="
                << parent_pair.eligible_pairs
                << " parent_pair_bce="
                << parent_pair.pair_bce
                << " candidate_pair_bce="
                << candidate_pair.pair_bce
                << " parent_listwise="
                << parent_rank.listwise_cross_entropy
                << " candidate_listwise="
                << candidate_rank.listwise_cross_entropy
                << " parent_regret="
                << parent_rank.mean_regret
                << " candidate_regret="
                << candidate_rank.mean_regret
                << " parent_top1="
                << parent_rank.top_one_expected_agreement
                << " candidate_top1="
                << candidate_rank.top_one_expected_agreement
                << " parent_stable="
                << parent_rank.stable_pair_agreement
                << " candidate_stable="
                << candidate_rank.stable_pair_agreement
                << " parent_successor_bce="
                << parent_rank.successor_bce
                << " candidate_successor_bce="
                << candidate_rank.successor_bce
                << " parent_successor_brier="
                << parent_rank.successor_brier
                << " candidate_successor_brier="
                << candidate_rank.successor_brier
                << " parent_successor_bias="
                << parent_rank.successor_bias
                << " candidate_successor_bias="
                << candidate_rank.successor_bias
                << " parent_successor_ece="
                << parent_rank.successor_ece
                << " candidate_successor_ece="
                << candidate_rank.successor_ece
                << '\n';
        };

    old_school::decision_boundary_action_pair::
        PairDeckMetrics aggregate_pair{
            .roots = parent.pairs.roots,
            .all_tied_roots =
                parent.pairs.all_tied_roots,
            .unordered_pairs =
                parent.pairs.unordered_pairs,
            .eligible_pairs =
                parent.pairs.eligible_pairs,
            .pair_bce =
                parent.pairs.equal_deck_pair_bce,
        };
    auto candidate_aggregate_pair = aggregate_pair;
    candidate_aggregate_pair.pair_bce =
        candidate.pairs.equal_deck_pair_bce;
    old_school::decision_boundary_rank_direct::
        DeckMetrics aggregate_rank{
            .roots = parent.ranking.roots,
            .listwise_cross_entropy =
                parent.ranking
                    .equal_deck_listwise_cross_entropy,
            .top_one_expected_agreement =
                parent.ranking
                    .equal_deck_top_one_expected_agreement,
            .stable_pair_agreement =
                parent.ranking
                    .equal_deck_stable_pair_agreement,
            .mean_regret =
                parent.ranking.equal_deck_mean_regret,
            .successor_bce =
                parent.ranking.equal_deck_successor_bce,
            .successor_brier =
                parent.ranking.equal_deck_successor_brier,
            .successor_bias =
                parent.ranking.equal_deck_successor_bias,
            .successor_ece =
                parent.ranking.equal_deck_successor_ece,
        };
    auto candidate_aggregate_rank = aggregate_rank;
    candidate_aggregate_rank.listwise_cross_entropy =
        candidate.ranking
            .equal_deck_listwise_cross_entropy;
    candidate_aggregate_rank.top_one_expected_agreement =
        candidate.ranking
            .equal_deck_top_one_expected_agreement;
    candidate_aggregate_rank.stable_pair_agreement =
        candidate.ranking
            .equal_deck_stable_pair_agreement;
    candidate_aggregate_rank.mean_regret =
        candidate.ranking.equal_deck_mean_regret;
    candidate_aggregate_rank.successor_bce =
        candidate.ranking.equal_deck_successor_bce;
    candidate_aggregate_rank.successor_brier =
        candidate.ranking.equal_deck_successor_brier;
    candidate_aggregate_rank.successor_bias =
        candidate.ranking.equal_deck_successor_bias;
    candidate_aggregate_rank.successor_ece =
        candidate.ranking.equal_deck_successor_ece;
    print_row(
        "equal", aggregate_pair,
        candidate_aggregate_pair, aggregate_rank,
        candidate_aggregate_rank);

    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        print_row(
            old_school::deck_name(
                static_cast<old_school::DeckId>(deck)),
            parent.pairs.decks[deck],
            candidate.pairs.decks[deck],
            parent.ranking.decks[deck],
            candidate.ranking.decks[deck]);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2 ||
        std::string_view(argv[1]) != "--run") {
        std::cerr
            << "Usage: "
               "old-school-decision-boundary-adaptive-trunk --run\n";
        return 2;
    }

    try {
        const auto parent_artifact =
            exact_snapshot(
                kParentArtifactPath, kParentArtifactBytes,
                kParentArtifactSha256, "AQ14 parent");
        const auto cache_artifact =
            exact_snapshot(
                kCachePath, kCacheBytes,
                kCacheSha256, "AQ14 cache");
        const auto parent =
            load_parent(parent_artifact);
        const dbc::Corpus source =
            dbc::load_corpus_cache(
                std::string(kCachePath), parent);
        const adaptive::Corpus corpus =
            adaptive::project_corpus(source, parent);
        require_artifacts_unchanged(
            parent_artifact, cache_artifact);
        if (!adaptive::frozen_pair_census_exact(
                corpus)) {
            throw std::runtime_error(
                "AQ14 frozen pair census drifted");
        }

        const adaptive::FoldReport oof =
            adaptive::evaluate_grouped_oof(
                corpus.train, parent);
        const adaptive::OptimizerReport fit =
            adaptive::optimize(corpus.train);
        const adaptive::OptimizerReport repeated_fit =
            adaptive::optimize(corpus.train);
        const adaptive::ModelIsolationReport isolation =
            adaptive::apply_parameters(
                parent, fit.parameters);
        const adaptive::ModelIsolationReport
            repeated_isolation =
                adaptive::apply_parameters(
                    parent, repeated_fit.parameters);
        const adaptive::ExactEvaluationReport exact =
            adaptive::evaluate_exact(
                corpus, parent, isolation,
                fit.parameters);
        const bool actual_model_agreement =
            exact.maximum_preactivation_difference <=
                adaptive::kMaximumAgreementError &&
            exact.maximum_activation_difference <=
                adaptive::kMaximumAgreementError &&
            exact.maximum_logit_difference <=
                adaptive::kMaximumAgreementError &&
            exact.maximum_residual_difference <=
                adaptive::kMaximumAgreementError &&
            oof.maximum_preactivation_difference <=
                adaptive::kMaximumAgreementError &&
            oof.maximum_activation_difference <=
                adaptive::kMaximumAgreementError &&
            oof.maximum_logit_difference <=
                adaptive::kMaximumAgreementError &&
            oof.maximum_residual_difference <=
                adaptive::kMaximumAgreementError;
        const adaptive::OfflineGate gate =
            adaptive::evaluate_offline_gate({
                .parent_train = exact.parent_train,
                .candidate_train =
                    exact.candidate_train,
                .parent_oof = oof.parent,
                .candidate_oof = oof.candidate,
                .parent_dev = exact.parent_dev,
                .candidate_dev = exact.candidate_dev,
                .cache_identity_exact =
                    source.digest ==
                    dbc::kFrozenCorpusDigest,
                .pair_census_exact = true,
                .optimizer_recipe_exact =
                    fit.config ==
                        adaptive::OptimizerConfig{} &&
                    fit.completed_steps ==
                        adaptive::kAdamSteps,
                .grouped_folds_exact =
                    oof.exact_balance,
                .repeat_fits_bit_identical =
                    adaptive::optimizer_bit_identical(
                        fit, repeated_fit) &&
                    isolation.candidate_fingerprint ==
                        repeated_isolation
                            .candidate_fingerprint &&
                    adaptive::model_scores_bit_identical(
                        corpus.train,
                        isolation.candidate,
                        repeated_isolation.candidate) &&
                    adaptive::model_scores_bit_identical(
                        corpus.dev,
                        isolation.candidate,
                        repeated_isolation.candidate) &&
                    oof.repeated_fits_bit_identical &&
                    oof.repeated_scores_bit_identical,
                .parameter_replay_bit_identical =
                    isolation
                        .repeated_application_bit_identical &&
                    isolation.exact_transform,
                .zero_delta_equivalent =
                    exact.zero_parameters_equivalent,
                .actual_model_agreement =
                    actual_model_agreement &&
                    exact
                        .legal_action_permutation_equivariant,
                .hidden_repartition_live_probe_eligible =
                    exact
                        .hidden_repartition_live_probe_eligible,
                .hidden_repartition_nonvacuous =
                    exact.hidden_repartition_nonvacuous,
                .hidden_repartition_owner_observation_bit_identical =
                    exact
                        .hidden_repartition_owner_observation_bit_identical,
                .hidden_repartition_actions_bit_identical =
                    exact
                        .hidden_repartition_actions_bit_identical,
                .hidden_repartition_options_bit_identical =
                    exact
                        .hidden_repartition_options_bit_identical,
                .hidden_repartition_logits_bit_identical =
                    exact
                        .hidden_repartition_logits_bit_identical,
                .hidden_repartition_centered_logits_bit_identical =
                    exact
                        .hidden_repartition_centered_logits_bit_identical,
                .hidden_repartition_residuals_bit_identical =
                    exact
                        .hidden_repartition_residuals_bit_identical,
                .parent_immutable =
                    isolation.parent_immutable,
                .model_isolation_passed =
                    isolation.passed() &&
                    oof.model_isolation_passed,
                .successor_predictions_bit_identical =
                    exact
                        .successor_predictions_bit_identical,
                .successor_metrics_bit_identical =
                    exact
                        .successor_metrics_bit_identical,
            });
        require_artifacts_unchanged(
            parent_artifact, cache_artifact);

        std::cout
            << std::fixed << std::setprecision(9)
            << "AQ14-DBC5-ADAPTIVE-TRUNK"
            << " source_digest=" << corpus.source_digest
            << " fit_tag=" << fit.config.fit_tag
            << " train_roots=" << corpus.train.roots.size()
            << " dev_roots=" << corpus.dev.roots.size()
            << '\n'
            << "optimizer steps=" << fit.completed_steps
            << " initial_objective="
            << fit.initial_objective
            << " final_objective="
            << fit.final_objective
            << " parameter_l2="
            << fit.parameter_l2_norm
            << " final_gradient_l2="
            << fit.final_gradient_l2_norm
            << " maximum_preclip_gradient_l2="
            << fit.maximum_preclip_gradient_l2_norm
            << " clipped_steps=" << fit.clipped_steps
            << '\n'
            << "model parent="
            << isolation.parent_fingerprint_before
            << " candidate="
            << isolation.candidate_fingerprint
            << " changed_coordinates="
            << isolation.changed_coordinates
            << " parent_positive_zero="
            << isolation.parent_positive_zero
            << " parent_immutable="
            << isolation.parent_immutable
            << " priority_adaptive_only="
            << isolation
                   .only_priority_adaptive_trunk_changed
            << " preactivation_max_abs="
            << exact.maximum_preactivation_difference
            << " activation_max_abs="
            << exact.maximum_activation_difference
            << " logit_max_abs="
            << exact.maximum_logit_difference
            << " residual_max_abs="
            << exact.maximum_residual_difference
            << " permutation_equivariant="
            << exact.legal_action_permutation_equivariant
            << " hidden_live_eligible="
            << exact
                   .hidden_repartition_live_probe_eligible
            << " hidden_nonvacuous="
            << exact.hidden_repartition_nonvacuous
            << " hidden_owner_observation="
            << exact
                   .hidden_repartition_owner_observation_bit_identical
            << " hidden_actions="
            << exact
                   .hidden_repartition_actions_bit_identical
            << " hidden_options="
            << exact
                   .hidden_repartition_options_bit_identical
            << " hidden_logits="
            << exact
                   .hidden_repartition_logits_bit_identical
            << " hidden_centered_logits="
            << exact
                   .hidden_repartition_centered_logits_bit_identical
            << " hidden_residuals="
            << exact
                   .hidden_repartition_residuals_bit_identical
            << '\n';
        print_metrics(
            "TRAIN", exact.parent_train,
            exact.candidate_train);
        print_metrics(
            "OOF", oof.parent, oof.candidate);
        print_metrics(
            "DEV", exact.parent_dev,
            exact.candidate_dev);
        for (const std::string& failure :
             gate.failures) {
            std::cout
                << "offline_failure="
                << failure << '\n';
        }
        std::cout
            << "offline_gate passed=" << gate.passed()
            << " invariants="
            << gate.invariants_passed
            << " successor_unchanged="
            << gate.successor_unchanged
            << " train_pair="
            << gate.train_pair_bce_improved
            << " train_regret="
            << gate.train_regret_improved
            << " train_listwise="
            << gate.train_listwise_non_increasing
            << " oof_pair="
            << gate.oof_pair_bce_improved
            << " oof_regret="
            << gate.oof_regret_improved
            << " oof_listwise="
            << gate.oof_listwise_non_increasing
            << " oof_top1="
            << gate.oof_top_one_non_decreasing
            << " oof_stable="
            << gate.oof_stable_pair_non_decreasing
            << " dev_pair="
            << gate.dev_pair_bce_improved
            << " dev_regret="
            << gate.dev_regret_improved
            << " dev_listwise="
            << gate.dev_listwise_non_increasing
            << " dev_top1="
            << gate.dev_top_one_non_decreasing
            << " dev_stable="
            << gate.dev_stable_pair_non_decreasing;
        for (std::size_t deck = 0;
             deck < old_school::kDeckCount; ++deck) {
            std::cout
                << " oof_deck_" << deck << "="
                << gate
                       .oof_deck_regret_non_increasing[deck]
                << " dev_deck_" << deck << "="
                << gate
                       .dev_deck_regret_non_increasing[deck];
        }
        std::cout << '\n';
        if (!gate.passed()) {
            std::cout
                << "final_result=REJECT stage=offline"
                << " model_gates_opened=0"
                << " selector_opened=0 pilot_licensed=0\n";
            return 0;
        }

        const offline::ModelGateReport model_gate =
            offline::evaluate_model_gates(
                parent, isolation.candidate);
        require_artifacts_unchanged(
            parent_artifact, cache_artifact);
        for (const std::string& failure :
             model_gate.failures()) {
            std::cout
                << "model_gate_failure="
                << failure << '\n';
        }
        std::cout
            << "model_gate passed="
            << model_gate.gate_passed() << '\n';
        if (!model_gate.gate_passed()) {
            std::cout
                << "final_result=REJECT stage=model_gate"
                << " selector_opened=0 pilot_licensed=0\n";
            return 0;
        }

        old_school::GameConfig game;
        game.max_turns = 500;
        game.learned_training_seed = 424242;
        game.learned_search_depth = 1;
        const old_school::BotConfig candidate_bot =
            g1::selector_bot_config(
                isolation.candidate,
                adaptive::kResidualWeight);
        const old_school::BotConfig baseline_bot =
            g1::selector_bot_config(parent, 0.0);
        if (!adaptive::selector_config_exact(
                candidate_bot, baseline_bot,
                parent, isolation.candidate)) {
            throw std::runtime_error(
                "AQ14 selector configuration drifted");
        }
        const old_school::BotBenchmarkSummary selector =
            old_school::run_bot_benchmark(
                g1::kSelectorRepetitions,
                adaptive::kSelectorSeed,
                candidate_bot, baseline_bot,
                game, false);
        g1::validate_selector_summary(
            selector, parent, isolation.candidate,
            adaptive::kSelectorSeed);
        require_artifacts_unchanged(
            parent_artifact, cache_artifact);
        const g1::SelectorDisposition disposition =
            g1::classify_selector(selector);
        std::cout
            << "selector seed=" << adaptive::kSelectorSeed
            << " wins=" << selector.challenger_stats.wins
            << " losses="
            << selector.challenger_stats.losses
            << " draws=" << selector.challenger_stats.draws;
        for (std::size_t deck = 0;
             deck < old_school::kDeckCount; ++deck) {
            std::cout
                << " deck_"
                << old_school::deck_name(
                       static_cast<old_school::DeckId>(deck))
                << "_wins="
                << selector.challenger_decks[deck].wins;
        }
        const bool pilot =
            disposition !=
            g1::SelectorDisposition::Reject;
        const bool fast_go =
            disposition ==
            g1::SelectorDisposition::FastGo;
        std::cout
            << '\n'
            << "final_result="
            << (pilot ? "PILOT_LICENSED" : "REJECT")
            << " stage=selector selector_opened=1"
            << " pilot_licensed=" << pilot
            << " fast_go=" << fast_go
            << " strength_claim=0 champion_replaced=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=decision_boundary_adaptive_trunk_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
