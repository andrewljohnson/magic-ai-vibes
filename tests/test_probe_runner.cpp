#include "alpha/probe_runner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using alpha::DeckId;
using alpha::LearnedActionSamples;
using alpha::probe_eval::DeckProbeMetrics;
using alpha::probe_runner::PolicyProbeReport;
using alpha::probe_runner::ProbeCacheStatus;
using alpha::probe_runner::ProbeReferenceSamples;
using alpha::probe_runner::ProbeScoreConfig;
using alpha::probe_runner::ProbeScoreReport;
using alpha::probes::DecisionKind;
using alpha::probes::DecisionProbe;

class TestRunner {
  public:
    void run(std::string_view name,
             const std::function<void()>& test) {
        try {
            test();
            ++passed_;
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "FAIL " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        if (failed_ != 0) {
            std::cerr << failed_ << " test(s) failed; "
                      << passed_ << " passed\n";
            return 1;
        }
        std::cout << passed_
                  << " probe runner tests passed\n";
        return 0;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        const auto stamp =
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();
        path_ = std::filesystem::temp_directory_path() /
                ("magic-ai-probe-runner-" +
                 std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
std::string expect_invalid(Function&& function,
                           std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const std::invalid_argument& error) {
        return error.what();
    }
    throw std::runtime_error(std::string(message));
}

const DecisionProbe& first_probe_of_kind(
    const std::vector<DecisionProbe>& probes,
    DecisionKind kind) {
    const auto found = std::find_if(
        probes.begin(), probes.end(),
        [kind](const DecisionProbe& probe) {
            return probe.decision_kind == kind;
        });
    if (found == probes.end()) {
        throw std::runtime_error("probe kind is missing");
    }
    return *found;
}

std::vector<ProbeReferenceSamples> synthetic_samples(
    const std::vector<DecisionProbe>& probes,
    std::size_t samples_per_candidate) {
    std::vector<ProbeReferenceSamples> result;
    result.reserve(probes.size());
    for (std::size_t probe_index = 0;
         probe_index < probes.size(); ++probe_index) {
        ProbeReferenceSamples probe_samples{
            .stable_id = probes[probe_index].stable_id,
            .root_deck = probes[probe_index].root_deck,
        };
        for (std::size_t candidate = 0;
             candidate < probes[probe_index].candidates.size();
             ++candidate) {
            alpha::probe_eval::CandidateSamples values;
            values.key =
                probes[probe_index].candidates[candidate].descriptor;
            for (std::size_t sample = 0;
                 sample < samples_per_candidate; ++sample) {
                const std::size_t bucket =
                    (probe_index + candidate + sample) % 7;
                values.q_samples.push_back(
                    0.15 + 0.1 * static_cast<double>(bucket));
            }
            probe_samples.candidates.push_back(std::move(values));
        }
        result.push_back(std::move(probe_samples));
    }
    return result;
}

void test_seed_and_fingerprint_ignore_iteration_order() {
    std::vector<DecisionProbe> probes =
        alpha::probes::make_probe_dev_v2();
    const std::string first_fingerprint =
        alpha::probe_runner::corpus_information_set_fingerprint(
            probes);
    const std::uint64_t first_seed =
        alpha::probe_runner::reference_seed_for_probe(
            alpha::probes::kProbeDevV2,
            probes.front().stable_id);
    const std::uint64_t second_seed =
        alpha::probe_runner::reference_seed_for_probe(
            alpha::probes::kProbeDevV2,
            probes[1].stable_id);
    expect(first_seed == 0xFED079E792FFB92DULL,
           "stable FNV-1a seed derivation changed");
    std::reverse(probes.begin(), probes.end());
    expect(
        alpha::probe_runner::corpus_information_set_fingerprint(
            probes) == first_fingerprint,
        "corpus fingerprint depends on iteration order");
    expect(
        alpha::probe_runner::reference_seed_for_probe(
            alpha::probes::kProbeDevV2,
            probes.back().stable_id) == first_seed,
        "probe seed depends on iteration order");
    expect(first_seed != second_seed,
           "distinct probe IDs received the same test seed");
}

void test_candidate_mapping_is_descriptor_safe() {
    const auto probes = alpha::probes::make_probe_dev_v2();
    const DecisionProbe& priority =
        first_probe_of_kind(probes, DecisionKind::Priority);
    LearnedActionSamples priority_rows;
    for (std::size_t candidate = 0;
         candidate < priority.candidates.size(); ++candidate) {
        priority_rows.q_samples.push_back(
            {0.1 * static_cast<double>(candidate + 1), 0.5});
    }
    const auto priority_mapped =
        alpha::probe_runner::map_candidate_samples(
            priority, priority_rows);
    expect(priority_mapped.size() == priority.candidates.size(),
           "priority mapping lost candidates");
    for (std::size_t candidate = 0;
         candidate < priority_mapped.size(); ++candidate) {
        expect(
            priority_mapped[candidate].key ==
                priority.candidates[candidate].descriptor,
            "priority mapping changed descriptor order");
    }

    DecisionProbe attack =
        first_probe_of_kind(probes, DecisionKind::Attack);
    std::reverse(attack.candidates.begin(),
                 attack.candidates.end());
    const LearnedActionSamples attack_rows{
        {{0.2, 0.3}, {0.8, 0.9}}};
    const auto attack_mapped =
        alpha::probe_runner::map_candidate_samples(
            attack, attack_rows);
    for (std::size_t candidate = 0;
         candidate < attack.candidates.size(); ++candidate) {
        const auto& decision =
            std::get<alpha::probes::BinaryAttackDecision>(
                attack.candidates[candidate].action);
        expect(
            attack_mapped[candidate].q_samples ==
                attack_rows.q_samples[decision.include ? 1U : 0U],
            "binary attack rows were not remapped by action");
    }
}

void test_reference_resource_bounds_reject_early() {
    const auto probes = alpha::probes::make_probe_dev_v2();
    ProbeScoreConfig config;
    config.reference_worlds = 4097;
    (void)expect_invalid(
        [&]() {
            (void)alpha::probe_runner::make_probe_cache_metadata(
                config, probes, "model");
        },
        "oversized world budget was accepted");
    config.reference_worlds = 2;
    config.reference_horizon_turns = 0;
    (void)alpha::probe_runner::make_probe_cache_metadata(
        config, probes, "model");
    config.reference_horizon_turns = 129;
    (void)expect_invalid(
        [&]() {
            (void)alpha::probe_runner::make_probe_cache_metadata(
                config, probes, "model");
        },
        "oversized horizon was accepted");
    config.reference_horizon_turns = 1;
    config.reference_rollouts_per_world = 257;
    (void)expect_invalid(
        [&]() {
            (void)alpha::probe_runner::make_probe_cache_metadata(
                config, probes, "model");
        },
        "oversized rollout budget was accepted");
}

void test_cache_roundtrip_and_stale_rejection() {
    const auto probes = alpha::probes::make_probe_dev_v2();
    ProbeScoreConfig config;
    config.training_games = 7;
    config.training_seed = 12345;
    config.reference_worlds = 2;
    config.reference_horizon_turns = 1;
    config.reference_rollouts_per_world = 1;
    const auto metadata =
        alpha::probe_runner::make_probe_cache_metadata(
            config, probes, "synthetic-model-fingerprint-v1");
    const auto samples = synthetic_samples(probes, 2);
    TemporaryDirectory directory;
    const auto path = directory.path() / "nested" / "labels.tsv";
    alpha::probe_runner::write_probe_label_cache_atomic(
        path, metadata, probes, samples);
    expect(std::filesystem::exists(path),
           "atomic cache writer did not publish target");
    const std::string temporary_prefix =
        path.filename().string() + ".tmp.";
    for (const auto& entry :
         std::filesystem::directory_iterator(path.parent_path())) {
        expect(
            entry.path().filename().string().rfind(
                temporary_prefix, 0) != 0,
            "atomic cache writer left a uniquely suffixed temporary "
            "file");
    }

    const auto labels =
        alpha::probe_runner::load_probe_label_cache(
            path, metadata, probes);
    expect(labels.size() == probes.size(),
           "cache roundtrip changed probe count");
    expect(labels.front().stable_id < labels.back().stable_id,
           "cache did not load in deterministic stable-ID order");

    auto stale = metadata;
    ++stale.training_seed;
    const std::string error = expect_invalid(
        [&]() {
            (void)alpha::probe_runner::load_probe_label_cache(
                path, stale, probes);
        },
        "stale cache metadata was accepted");
    expect(error.find("--refresh-probe-cache") !=
               std::string::npos,
           "stale-cache error omitted refresh instruction");

    stale = metadata;
    stale.reference_model_fingerprint =
        "different-model-fingerprint";
    const std::string model_error = expect_invalid(
        [&]() {
            (void)alpha::probe_runner::load_probe_label_cache(
                path, stale, probes);
        },
        "cache from a different exact model was accepted");
    expect(model_error.find("model_fingerprint") !=
               std::string::npos,
           "model-fingerprint mismatch was not identified");

    stale = metadata;
    stale.semantic_revision = "old-semantics";
    const std::string semantics_error = expect_invalid(
        [&]() {
            (void)alpha::probe_runner::load_probe_label_cache(
                path, stale, probes);
        },
        "cache from a different scoring revision was accepted");
    expect(semantics_error.find("semantic_revision") !=
               std::string::npos,
           "semantic-revision mismatch was not identified");
}

void test_hidden_clone_preserves_information_set() {
    std::vector<DecisionProbe> probes =
        alpha::probes::make_probe_dev_v2();
    DecisionProbe clone_probe = probes.front();
    clone_probe.state =
        alpha::probe_runner::hidden_repartition_clone(
            probes.front());
    std::vector<DecisionProbe> clone_corpus = probes;
    clone_corpus.front() = clone_probe;

    expect(
        alpha::probe_runner::corpus_information_set_fingerprint(
            probes) ==
            alpha::probe_runner::corpus_information_set_fingerprint(
                clone_corpus),
        "opponent hidden repartition changed corpus fingerprint");
    expect(alpha::probes::hidden_clone_is_determinization_invariant(
               probes.front(), 99123),
           "probe clone changed fixed-seed determinization");
}

void test_tiny_reference_is_hidden_clone_invariant() {
    const auto probes = alpha::probes::make_probe_dev_v2();
    ProbeScoreConfig config;
    config.training_games = 1;
    config.training_seed = 777;
    config.reference_worlds = 2;
    config.reference_horizon_turns = 1;
    config.reference_rollouts_per_world = 1;
    const auto model =
        alpha::train_learned_actor_model(
            config.training_games, config.training_seed);
    const auto samples =
        alpha::probe_runner::generate_probe_reference_samples(
            probes.front(), model, config);
    expect(samples.stable_id == probes.front().stable_id,
           "tiny reference returned wrong probe ID");
    expect(samples.candidates.size() ==
               probes.front().candidates.size(),
           "tiny reference returned wrong candidate count");
    for (const auto& candidate : samples.candidates) {
        expect(candidate.q_samples.size() == 2,
               "tiny reference returned wrong K");
    }
}

void test_value_attack_probe_scores_are_seed_independent() {
    const auto probes = alpha::probes::make_probe_dev_v2();
    const auto value_model =
        alpha::train_learned_value_champion(1, 778);
    for (const DecisionProbe& probe : probes) {
        if (probe.decision_kind != DecisionKind::Attack) {
            continue;
        }
        std::vector<std::vector<alpha::PermanentId>> attack_sets;
        for (const auto& candidate : probe.candidates) {
            const auto& decision =
                std::get<alpha::probes::BinaryAttackDecision>(
                    candidate.action);
            attack_sets.push_back(
                decision.include
                    ? std::vector<alpha::PermanentId>{
                          decision.attacker}
                    : std::vector<alpha::PermanentId>{});
        }
        const std::uint64_t deployed_seed =
            alpha::probe_runner::reference_seed_for_probe(
                alpha::probes::kProbeDevV2, probe.stable_id,
                alpha::probe_runner::kProbeProductionPolicySeed);
        const auto first =
            alpha::learned_value_attack_set_scores(
                probe.state, probe.root_player, attack_sets,
                value_model, deployed_seed);
        const auto second =
            alpha::learned_value_attack_set_scores(
                probe.state, probe.root_player, attack_sets,
                value_model, deployed_seed + 1);
        expect(first.scores == second.scores &&
                   first.selected_candidate ==
                       second.selected_candidate,
               "current exhaustive attack probe depends on production "
               "RNG seed");
    }
}

void test_value_decision_detail_respects_ties_and_selectors() {
    const auto label = alpha::probe_eval::make_probe_label(
        "red.synthetic-selection", DeckId::Red,
        {
            {"best", {0.8, 0.8}},
            {"other", {0.4, 0.4}},
        });
    const alpha::probe_eval::ProbePrediction uniform_tie{
        "red.synthetic-selection",
        {{"other", 1.0}, {"best", 1.0}},
        0.7,
    };
    const auto uniform_detail =
        alpha::probe_runner::make_value_probe_decision_detail(
            label, uniform_tie);
    expect(
        uniform_detail.selected_keys ==
                std::vector<std::string>{"best", "other"} &&
            !uniform_detail.deterministic_selection &&
            uniform_detail.reference_best_set ==
                std::vector<std::string>{"best"},
        "uniform exact tie was not represented as a stable selected set");
    expect(
        std::abs(uniform_detail.selected_action_reference_q - 0.6) <
                1.0e-12 &&
            std::abs(uniform_detail.regret - 0.2) < 1.0e-12 &&
            std::abs(uniform_detail.critic_error - 0.1) < 1.0e-12,
        "uniform tie detail did not use expected selected-action Q");

    const alpha::probe_eval::ProbePrediction deterministic_tie{
        "red.synthetic-selection",
        {{"best", 1.0}, {"other", 1.0}},
        0.5,
        "other",
    };
    const auto deterministic_detail =
        alpha::probe_runner::make_value_probe_decision_detail(
            label, deterministic_tie, &uniform_detail,
            &uniform_detail);
    expect(
            deterministic_detail.selected_keys ==
                std::vector<std::string>{"other"} &&
            deterministic_detail.deterministic_selection &&
            deterministic_detail.selection_changed_from_reference &&
            deterministic_detail.selection_changed_from_previous,
        "deterministic deployed selector was mistaken for uniform tie");
    expect(
        std::abs(
            deterministic_detail.selected_action_reference_q - 0.4) <
                1.0e-12 &&
            std::abs(deterministic_detail.regret - 0.4) < 1.0e-12 &&
            std::abs(deterministic_detail.critic_error - 0.1) <
                1.0e-12,
        "deterministic detail did not use its selected candidate");

    const auto persistent_detail =
        alpha::probe_runner::make_value_probe_decision_detail(
            label, deterministic_tie, &uniform_detail,
            &deterministic_detail);
    expect(
        persistent_detail.selection_changed_from_reference &&
            !persistent_detail.selection_changed_from_previous,
        "persistent G0 disagreement was mistaken for an adjacent "
        "selection change");
}

void test_low_margin_summary_is_actionable() {
    const auto label = alpha::probe_eval::make_probe_label(
        "green.low-margin", DeckId::Green,
        {
            {"best", {0.60, 0.60, 0.60, 0.60}},
            {"near", {0.58, 0.58, 0.58, 0.58}},
            {"far", {0.20, 0.20, 0.20, 0.20}},
        });
    const auto summary =
        alpha::probe_runner::summarize_low_margin_best_pairs(
            {label});
    expect(summary.pair_count == 1 &&
               summary.by_deck[0].pair_count == 1,
           "low-margin summary did not isolate the near action");
    expect(summary.pairs.front().reference_best == "best" &&
               summary.pairs.front().other == "near" &&
               summary.pairs.front()
                   .effect_below_stable_threshold,
           "low-margin summary is not actionable");
}

void test_report_contains_required_schema_and_caveats() {
    ProbeScoreReport report;
    report.metadata = {
        .schema =
            std::string(alpha::probe_runner::kProbeCacheSchema),
        .algorithm =
            std::string(alpha::probe_runner::
                            kProbeReferenceAlgorithm),
        .semantic_revision =
            std::string(alpha::probe_runner::
                            kProbeSemanticRevision),
        .corpus_id = "probe-dev-v2",
        .reference_seed =
            alpha::probe_runner::kProbeReferenceSeed,
        .production_policy_seed =
            alpha::probe_runner::kProbeProductionPolicySeed,
        .training_seed = 424242,
        .training_games = 800,
        .worlds = 128,
        .horizon_turns = 12,
        .rollouts_per_world = 1,
        .probe_count = 16,
        .reference_model_fingerprint =
            "actor-model-fingerprint",
        .information_set_fingerprint = "0123456789abcdef",
    };
    report.cache_status = ProbeCacheStatus::Loaded;
    report.cache_path = "labels.tsv";
    report.reference_samples_per_candidate = 128;
    report.scoring_actor_model_fingerprint =
        "actor-candidate-fingerprint";
    report.value_model_fingerprint = "value-model-fingerprint";
    alpha::probe_eval::ProbeMetricSummary metrics;
    metrics.probe_count = 16;
    metrics.stable_pair_count = 9;
    metrics.top1_expected_agreement = 0.75;
    metrics.stable_pair_agreement = 0.8;
    metrics.mean_regret = 0.02;
    for (std::size_t deck = 0; deck < metrics.by_deck.size();
         ++deck) {
        metrics.by_deck[deck] = DeckProbeMetrics{
            .root_deck = static_cast<DeckId>(deck),
            .probe_count = 4,
            .stable_pair_count = 2,
            .top1_expected_agreement = 0.75,
            .stable_pair_agreement = 0.8,
            .mean_regret = 0.02,
        };
    }
    report.policies = {
        PolicyProbeReport{
            "Actor raw head", "raw", metrics, true, std::nullopt},
        PolicyProbeReport{"Actor deployed policy",
                          "Priority: K=2/H=0; Attack: raw masked "
                          "policy head",
                          metrics, true, std::nullopt},
        PolicyProbeReport{"Value deployed policy",
                          "Priority: K=2/H=4; Attack: deployed "
                          "public-board attack-set scorer",
                          metrics, true, std::nullopt},
        PolicyProbeReport{"Handcrafted agreement", "diagnostic",
                          metrics, false, std::nullopt},
        PolicyProbeReport{"Value-continuation deep cross-check",
                          "deep", metrics, true, std::nullopt},
    };
    alpha::probe_eval::CandidateQFitSummary q_fit;
    q_fit.candidate_count = 32;
    q_fit.mae = 0.03;
    q_fit.rmse = 0.04;
    for (std::size_t deck = 0; deck < q_fit.by_deck.size();
         ++deck) {
        q_fit.by_deck[deck] = {
            .root_deck = static_cast<DeckId>(deck),
            .candidate_count = 8,
            .mae = 0.03,
            .rmse = 0.04,
        };
    }
    report.policies.back().candidate_q_fit = q_fit;
    report.reference_sensitivity.actor_stable_pair_count = 9;
    report.reference_sensitivity.point_sign_reversal_count = 1;
    report.reference_sensitivity.dual_stable_reversal_count = 1;
    for (std::size_t deck = 0;
         deck < report.reference_sensitivity.by_deck.size(); ++deck) {
        report.reference_sensitivity.by_deck[deck].root_deck =
            static_cast<DeckId>(deck);
    }
    report.reference_sensitivity.flags.push_back({
        .stable_id = "blue.example",
        .root_deck = DeckId::Blue,
        .first = "pass",
        .second = "counter",
        .actor_delta_q = 0.1,
        .value_delta_q = -0.1,
        .value_pair_is_stable = true,
    });
    report.hidden_repartition = {
        .passed = true,
        .policy_count = 5,
        .probe_count = 16,
    };
    for (std::size_t deck = 0;
         deck < report.low_margin.by_deck.size(); ++deck) {
        report.low_margin.by_deck[deck].root_deck =
            static_cast<DeckId>(deck);
    }
    report.low_margin.pair_count = 1;
    report.low_margin.by_deck[2].pair_count = 1;
    report.low_margin.pairs.push_back({
        .stable_id = "blue.example",
        .root_deck = DeckId::Blue,
        .reference_best = "counter",
        .other = "pass",
        .delta_q = 0.02,
        .paired_standard_error = 0.02,
        .effect_below_stable_threshold = true,
        .confidence_interval_crosses_zero = true,
    });

    const std::string output =
        alpha::probe_runner::format_probe_score_report(report);
    expect(output.find("actor-model-fingerprint") !=
                   std::string::npos &&
               output.find("actor-candidate-fingerprint") !=
                   std::string::npos,
           "report did not distinguish reference and scoring Actor models");
    expect(output.find(
               "diagnostic only, 4 positions/deck") !=
               std::string::npos,
           "report omitted small-corpus warning");
    expect(output.find("top1") != std::string::npos &&
               output.find("stable pairs") !=
                   std::string::npos &&
               output.find("regret") != std::string::npos &&
               output.find("critic Brier") != std::string::npos &&
               output.find("MSE") != std::string::npos &&
               output.find("logloss") != std::string::npos &&
               output.find("bias") != std::string::npos &&
               output.find("ECE") != std::string::npos,
           "report omitted required metric columns");
    expect(output.find("critic n/a") != std::string::npos,
           "Handcrafted row pretended to have critic metrics");
    expect(output.find("not an objective ceiling") !=
               std::string::npos,
           "Handcrafted diagnostic caveat is missing");
    expect(output.find("[REFERENCE-SENSITIVE]") !=
               std::string::npos,
           "continuation-policy disagreement was not flagged");
    expect(output.find("[ESCALATE]") != std::string::npos &&
               output.find("targeted semantic/reference follow-up") !=
                   std::string::npos,
           "low-margin follow-up list is missing");
    expect(output.find("Hidden-repartition invariance: PASS") !=
               std::string::npos,
           "hidden-repartition result is missing");
    expect(output.find("5 policy views") !=
               std::string::npos &&
               output.find("selected-action reference Q") !=
                   std::string::npos &&
               report.policies.size() == 5,
           "report does not identify all five policy views");
    const std::size_t q_fit_position =
        output.find("Candidate-Q fit (candidate-weighted)");
    expect(q_fit_position != std::string::npos &&
               output.find("Candidate-Q fit (candidate-weighted)",
                           q_fit_position + 1) ==
                   std::string::npos &&
               output.find("Q MAE 0.0300") !=
                   std::string::npos &&
               output.find("Q RMSE 0.0400") !=
                   std::string::npos &&
               q_fit_position >
                   output.find(
                       "Value-continuation deep cross-check"),
           "Q fit was missing, duplicated, or attached to a non-Q row");
    expect(output.find("Value deployed policy") !=
               std::string::npos &&
               output.find("Attack: deployed public-board "
                           "attack-set scorer") !=
                   std::string::npos,
           "report does not state exact Value attack semantics");
    expect(output.find("Actor deployed policy") !=
               std::string::npos &&
               output.find("Attack: raw masked policy head") !=
                   std::string::npos,
           "report does not state exact Actor attack semantics");
    expect(output.find("Green") != std::string::npos &&
               output.find("Red") != std::string::npos &&
               output.find("Blue") != std::string::npos &&
               output.find("White") != std::string::npos,
           "report omitted per-deck sections");
}

void test_compact_checkpoint_report_shows_actionable_transitions() {
    ProbeScoreReport report;
    report.metadata = {
        .schema =
            std::string(alpha::probe_runner::kProbeCacheSchema),
        .algorithm =
            std::string(alpha::probe_runner::
                            kProbeReferenceAlgorithm),
        .semantic_revision =
            std::string(alpha::probe_runner::
                            kProbeSemanticRevision),
        .corpus_id = "probe-dev-v2",
        .reference_seed =
            alpha::probe_runner::kProbeReferenceSeed,
        .production_policy_seed =
            alpha::probe_runner::kProbeProductionPolicySeed,
        .training_seed = 424242,
        .training_games = 800,
        .worlds = 8,
        .horizon_turns = 0,
        .rollouts_per_world = 1,
        .probe_count = 1,
        .reference_model_fingerprint = "actor-reference",
        .information_set_fingerprint = "corpus-fingerprint",
    };
    report.scoring_actor_model_fingerprint = "actor-scoring";
    report.value_model_fingerprint = "value-g0-fingerprint";
    alpha::probe_eval::ProbeMetricSummary metrics;
    metrics.probe_count = 1;
    metrics.top1_expected_agreement = 0.5;
    metrics.mean_regret = 0.2;
    metrics.critic_brier = 0.01;
    for (std::size_t deck = 0; deck < metrics.by_deck.size();
         ++deck) {
        metrics.by_deck[deck].root_deck =
            static_cast<DeckId>(deck);
    }
    report.value_checkpoints = {
        {
            .name = "Value G0",
            .fingerprint = "value-g0-fingerprint",
            .metrics = metrics,
            .decisions = {{
                .stable_id = "red.synthetic",
                .root_deck = DeckId::Red,
                .selected_keys = {"best", "other"},
                .deterministic_selection = false,
                .reference_best_set = {"best"},
                .regret = 0.2,
                .critic_prediction = 0.7,
                .selected_action_reference_q = 0.6,
                .critic_error = 0.1,
                .selection_changed_from_previous = false,
            }},
        },
        {
            .name = "Value G1",
            .fingerprint = "value-g1-fingerprint",
            .metrics = metrics,
            .decisions = {{
                .stable_id = "red.synthetic",
                .root_deck = DeckId::Red,
                .selected_keys = {"best"},
                .deterministic_selection = true,
                .reference_best_set = {"best"},
                .regret = 0.0,
                .critic_prediction = 0.8,
                .selected_action_reference_q = 0.8,
                .critic_error = 0.0,
                .selection_changed_from_reference = true,
                .selection_changed_from_previous = true,
            }},
        },
        {
            .name = "Value G2",
            .fingerprint = "value-g2-fingerprint",
            .metrics = metrics,
            .decisions = {{
                .stable_id = "red.synthetic",
                .root_deck = DeckId::Red,
                .selected_keys = {"best"},
                .deterministic_selection = true,
                .reference_best_set = {"best"},
                .regret = 0.0,
                .critic_prediction = 0.8,
                .selected_action_reference_q = 0.8,
                .critic_error = 0.0,
                .selection_changed_from_reference = true,
                .selection_changed_from_previous = false,
            }},
        },
    };
    report.hidden_repartition = {
        .passed = true,
        .policy_count = 7,
        .probe_count = 1,
    };
    for (std::size_t deck = 0;
         deck < report.reference_sensitivity.by_deck.size();
         ++deck) {
        report.reference_sensitivity.by_deck[deck].root_deck =
            static_cast<DeckId>(deck);
        report.low_margin.by_deck[deck].root_deck =
            static_cast<DeckId>(deck);
    }

    const std::string output =
        alpha::probe_runner::format_probe_score_report(report);
    const std::size_t g0 = output.find("value-g0-fingerprint");
    const std::size_t g1 = output.find("value-g1-fingerprint");
    const std::size_t g2 = output.find("value-g2-fingerprint");
    expect(
        output.find("Value checkpoint transitions (compact)") !=
                std::string::npos &&
            g0 != std::string::npos &&
            g1 != std::string::npos &&
            g2 != std::string::npos &&
            g0 < g1 && g1 < g2,
        "compact checkpoint fingerprints were missing or reordered");
    expect(
        output.find(
            "[NONZERO-REGRET] Value G0 red.synthetic (Red)") !=
                std::string::npos &&
            output.find(
                "selected uniform exact-max set {best, other}") !=
                std::string::npos &&
            output.find("Actor-reference best {best}") !=
                std::string::npos,
        "nonzero-regret row omitted deployed/reference selection detail");
    expect(
        output.find(
            "[G0-DISAGREEMENT] Value G0 -> Value G1 "
            "red.synthetic (Red)") != std::string::npos &&
            output.find("G0 selection different, adjacent selection changed") !=
                std::string::npos &&
            output.find("selected-action reference Q 0.8000") !=
                std::string::npos &&
            output.find("critic error 0.0000") !=
                std::string::npos,
        "selection transition omitted critic attribution");
    expect(
        output.find(
            "[G0-DISAGREEMENT] Value G1 -> Value G2 "
            "red.synthetic (Red)") != std::string::npos &&
            output.find(
                "Value G1 -> Value G2 red.synthetic (Red): "
                "G0 selection different, adjacent selection same") !=
                std::string::npos,
        "persistent G0 disagreement was omitted after the adjacent "
        "selection stopped changing");
    expect(
        output.find("Value G1 deployed policy\n  Config:") ==
            std::string::npos,
        "checkpoint attribution duplicated a full policy report");
}

void test_candidate_scoring_reuses_reference_owned_cache() {
    TemporaryDirectory directory;
    const auto reference =
        alpha::train_learned_actor_model(1, 0xA11CEULL);
    const auto candidate =
        alpha::train_learned_actor_model(1, 0xB0BULL);
    expect(alpha::learned_model_fingerprint(reference) !=
               alpha::learned_model_fingerprint(candidate),
           "tiny candidate model unexpectedly aliases reference content");

    const ProbeScoreConfig config{
        .training_games = 1,
        .training_seed = 0xA11CEULL,
        .reference_worlds = 2,
        .reference_horizon_turns = 0,
        .reference_rollouts_per_world = 1,
        .cache_path = directory.path() / "candidate-cache.tsv",
        .refresh_cache = false,
    };
    std::ostringstream progress;
    const ProbeScoreReport generated =
        alpha::probe_runner::score_probe_dev_v2_with_models(
            config, progress, reference, reference, "Actor G0");
    const ProbeScoreReport loaded =
        alpha::probe_runner::score_probe_dev_v2_with_models(
            config, progress, reference, candidate, "Actor G1");

    expect(generated.cache_status == ProbeCacheStatus::Generated &&
               loaded.cache_status == ProbeCacheStatus::Loaded,
           "candidate-only change regenerated reference-owned labels");
    expect(generated.metadata == loaded.metadata &&
               generated.metadata.reference_model_fingerprint ==
                   alpha::learned_model_fingerprint(reference),
           "candidate-only change altered reference cache identity");
    expect(generated.scoring_actor_model_fingerprint ==
                   generated.metadata.reference_model_fingerprint &&
               loaded.scoring_actor_model_fingerprint ==
                   alpha::learned_model_fingerprint(candidate),
           "report did not bind the actual scoring Actor");
    expect(generated.policies.front().name == "Actor G0 raw head" &&
               loaded.policies.front().name == "Actor G1 raw head",
           "candidate policy labels were not generation-specific");
    expect(generated.hidden_repartition.passed &&
               loaded.hidden_repartition.passed,
           "candidate scoring did not enforce hidden repartition");

    const std::string null_error = expect_invalid(
        [&] {
            alpha::probe_runner::score_probe_dev_v2_with_models(
                config, progress, nullptr, candidate, "Actor G1");
        },
        "null reference Actor was accepted");
    expect(null_error.find("reference and scoring") !=
               std::string::npos,
           "null model error was not actionable");
    const std::string name_error = expect_invalid(
        [&] {
            alpha::probe_runner::score_probe_dev_v2_with_models(
                config, progress, reference, candidate,
                "Actor\tG1");
        },
        "unsafe candidate name was accepted");
    expect(name_error.find("contain no tabs/newlines") !=
               std::string::npos,
           "unsafe candidate name error was not actionable");

    const auto reference_value =
        alpha::train_learned_value_champion(
            1, 0xA11CEULL);
    const auto scoring_value =
        alpha::train_learned_value_champion(
            1, 0xB0BULL);
    expect(
        alpha::learned_model_fingerprint(reference_value) !=
            alpha::learned_model_fingerprint(scoring_value),
        "tiny Value candidate unexpectedly aliases reference content");
    const ProbeScoreReport value_loaded =
        alpha::probe_runner::score_probe_dev_v2_with_candidates(
            config, progress,
            {
                .reference_actor_model = reference,
                .scoring_actor_model = reference,
                .scoring_actor_name = "Actor G0",
                .reference_value_model = reference_value,
                .reference_value_name = "Value G0",
                .scoring_value_models = {
                    {"Value G8", scoring_value},
                },
            });
    expect(
        value_loaded.cache_status == ProbeCacheStatus::Loaded &&
            value_loaded.metadata == generated.metadata,
        "Value candidate changed Actor-owned cache identity");
    expect(
        value_loaded.value_model_fingerprint ==
                alpha::learned_model_fingerprint(reference_value) &&
            value_loaded.scoring_value_model_fingerprint ==
                alpha::learned_model_fingerprint(scoring_value),
        "report did not bind reference and scoring Value models");
    expect(
        value_loaded.policies.size() == 6 &&
            value_loaded.hidden_repartition.policy_count == 6 &&
            value_loaded.hidden_repartition.passed,
        "distinct Value candidate was not hidden-invariant sixth view");
    expect(
        value_loaded.policies[2].name ==
                "Value G0 deployed policy" &&
            value_loaded.policies[3].name ==
                "Value G8 deployed policy" &&
            value_loaded.policies.back().name ==
                "Value G0-continuation deep cross-check",
        "Value candidate/reference policy rows were mislabeled");
    const std::string value_output =
        alpha::probe_runner::format_probe_score_report(
            value_loaded);
    expect(
        value_output.find("Reference Value model fingerprint") !=
                std::string::npos &&
            value_output.find("Scoring Value model fingerprint") !=
                std::string::npos &&
            value_output.find("6 policy views") !=
                std::string::npos,
        "formatted report did not distinguish the Value candidate");

    const auto second_scoring_value =
        alpha::train_learned_value_champion(
            1, 0xC0DEULL);
    const auto third_scoring_value =
        alpha::train_learned_value_champion(
            1, 0xD00DULL);
    const ProbeScoreReport checkpoint_loaded =
        alpha::probe_runner::score_probe_dev_v2_with_candidates(
            config, progress,
            {
                .reference_actor_model = reference,
                .scoring_actor_model = reference,
                .scoring_actor_name = "Actor G0",
                .reference_value_model = reference_value,
                .reference_value_name = "Value G0",
                .scoring_value_models = {
                    {"Value Mix50 base", scoring_value},
                    {"Value Mix50 G1", second_scoring_value},
                    {"Value Mix50 G2", third_scoring_value},
                    {"Value Mix50 G3", scoring_value},
                    {"Value Mix50 G4", second_scoring_value},
                    {"Value Mix50 G5", third_scoring_value},
                    {"Value Mix50 G6", scoring_value},
                    {"Value Mix50 G7", second_scoring_value},
                    {"Value Mix50 G8", third_scoring_value},
                },
            });
    expect(
        checkpoint_loaded.cache_status == ProbeCacheStatus::Loaded &&
            checkpoint_loaded.metadata == generated.metadata &&
            checkpoint_loaded.metadata.reference_model_fingerprint ==
                alpha::learned_model_fingerprint(reference),
        "multiple Value checkpoints changed Actor cache identity");
    expect(
        checkpoint_loaded.reference_sensitivity
                    .actor_stable_pair_count ==
                value_loaded.reference_sensitivity
                    .actor_stable_pair_count &&
            checkpoint_loaded.reference_sensitivity
                    .point_sign_reversal_count ==
                value_loaded.reference_sensitivity
                    .point_sign_reversal_count &&
            checkpoint_loaded.reference_sensitivity
                    .dual_stable_reversal_count ==
                value_loaded.reference_sensitivity
                    .dual_stable_reversal_count &&
            checkpoint_loaded.reference_sensitivity.flags.size() ==
                value_loaded.reference_sensitivity.flags.size(),
        "Value checkpoints changed legacy continuation sensitivity");
    expect(
        checkpoint_loaded.policies.size() == 5 &&
            checkpoint_loaded.hidden_repartition.passed &&
            checkpoint_loaded.hidden_repartition.policy_count == 14,
        "multi-checkpoint scoring did not keep five full views and "
        "verify every compact row");
    expect(
        checkpoint_loaded.value_checkpoints.size() == 10 &&
            checkpoint_loaded.value_checkpoints[0].name ==
                "Value G0" &&
            checkpoint_loaded.value_checkpoints[1].name ==
                "Value Mix50 base" &&
            checkpoint_loaded.value_checkpoints[2].name ==
                "Value Mix50 G1" &&
            checkpoint_loaded.value_checkpoints[9].name ==
                "Value Mix50 G8",
        "Value checkpoint rows did not preserve exact caller order");
    const std::array<std::string, 10> expected_fingerprints{
        alpha::learned_model_fingerprint(reference_value),
        alpha::learned_model_fingerprint(scoring_value),
        alpha::learned_model_fingerprint(second_scoring_value),
        alpha::learned_model_fingerprint(third_scoring_value),
        alpha::learned_model_fingerprint(scoring_value),
        alpha::learned_model_fingerprint(second_scoring_value),
        alpha::learned_model_fingerprint(third_scoring_value),
        alpha::learned_model_fingerprint(scoring_value),
        alpha::learned_model_fingerprint(second_scoring_value),
        alpha::learned_model_fingerprint(third_scoring_value),
    };
    for (std::size_t checkpoint = 0;
         checkpoint < checkpoint_loaded.value_checkpoints.size();
         ++checkpoint) {
        const auto& row =
            checkpoint_loaded.value_checkpoints[checkpoint];
        expect(row.fingerprint == expected_fingerprints[checkpoint],
               "Value checkpoint fingerprint order changed");
        expect(row.decisions.size() == 16,
               "Value checkpoint lost a probe detail row");
        expect(
            std::is_sorted(
                row.decisions.begin(), row.decisions.end(),
                [](const auto& left, const auto& right) {
                    return left.stable_id < right.stable_id;
                }),
            "Value checkpoint details are not in stable-ID order");
    }
    const std::string checkpoint_output =
        alpha::probe_runner::format_probe_score_report(
            checkpoint_loaded);
    expect(
        checkpoint_output.find(
            "Value checkpoint transitions (compact)") !=
            std::string::npos,
        "formatted checkpoint report omitted its heading");
    const std::array<std::string_view, 10> expected_names{
        "Value G0",
        "Value Mix50 base",
        "Value Mix50 G1",
        "Value Mix50 G2",
        "Value Mix50 G3",
        "Value Mix50 G4",
        "Value Mix50 G5",
        "Value Mix50 G6",
        "Value Mix50 G7",
        "Value Mix50 G8",
    };
    std::size_t prior_position = 0;
    for (const std::string_view name : expected_names) {
        const std::string prefix =
            "  " + std::string(name) + ": fingerprint ";
        const std::size_t position =
            checkpoint_output.find(prefix, prior_position);
        expect(position != std::string::npos,
               "formatted Mix50 checkpoint rows lost caller order");
        prior_position = position + prefix.size();
    }
    expect(
        checkpoint_output.find("5 policy views") !=
                std::string::npos &&
            checkpoint_output.find(
                "Value Mix50 base deployed policy\n  Config:") ==
                std::string::npos &&
            checkpoint_output.find(
                "Value Mix50 G1 deployed policy\n  Config:") ==
                std::string::npos &&
            checkpoint_output.find(
                "Value Mix50 G8 deployed policy\n  Config:") ==
                std::string::npos,
        "multi-checkpoint output expanded compact rows into full views");
    expect(
        alpha::probe_runner::format_probe_score_report(generated)
                .find("Value checkpoint transitions (compact)") ==
            std::string::npos,
        "legacy five-view output gained a checkpoint section");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run("stable seed and corpus fingerprint",
               test_seed_and_fingerprint_ignore_iteration_order);
    runner.run("candidate descriptor mapping",
               test_candidate_mapping_is_descriptor_safe);
    runner.run("reference resource bounds",
               test_reference_resource_bounds_reject_early);
    runner.run("cache roundtrip and stale rejection",
               test_cache_roundtrip_and_stale_rejection);
    runner.run("hidden clone information set",
               test_hidden_clone_preserves_information_set);
    runner.run("tiny hidden-safe reference",
               test_tiny_reference_is_hidden_clone_invariant);
    runner.run("deployed Value attack seed independence",
               test_value_attack_probe_scores_are_seed_independent);
    runner.run("Value selection detail semantics",
               test_value_decision_detail_respects_ties_and_selectors);
    runner.run("actionable low-margin summary",
               test_low_margin_summary_is_actionable);
    runner.run("report summary schema",
               test_report_contains_required_schema_and_caveats);
    runner.run("compact checkpoint attribution",
               test_compact_checkpoint_report_shows_actionable_transitions);
    runner.run("candidate cache ownership",
               test_candidate_scoring_reuses_reference_owned_cache);
    return runner.finish();
}
