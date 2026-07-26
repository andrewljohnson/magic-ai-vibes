#include "old_school/probe_runner.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using old_school::DeckId;
using old_school::LearnedActionSamples;
using old_school::probe_eval::DeckProbeMetrics;
using old_school::probe_runner::PolicyProbeReport;
using old_school::probe_runner::ProbeCacheStatus;
using old_school::probe_runner::ProbeCorpusKind;
using old_school::probe_runner::ProbeReferenceSamples;
using old_school::probe_runner::ProbeScoreConfig;
using old_school::probe_runner::ProbeScoreReport;
using old_school::probes::DecisionKind;
using old_school::probes::DecisionProbe;

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

bool action_samples_are_bit_identical(
    const LearnedActionSamples& first,
    const LearnedActionSamples& second) {
    if (first.sampled_worlds != second.sampled_worlds ||
        first.rollout_evaluations !=
            second.rollout_evaluations ||
        first.terminal_evaluations !=
            second.terminal_evaluations ||
        first.bootstrapped_evaluations !=
            second.bootstrapped_evaluations ||
        first.q_samples.size() != second.q_samples.size()) {
        return false;
    }
    for (std::size_t candidate = 0;
         candidate < first.q_samples.size(); ++candidate) {
        if (first.q_samples[candidate].size() !=
            second.q_samples[candidate].size()) {
            return false;
        }
        for (std::size_t sample = 0;
             sample < first.q_samples[candidate].size(); ++sample) {
            if (std::bit_cast<std::uint64_t>(
                    first.q_samples[candidate][sample]) !=
                std::bit_cast<std::uint64_t>(
                    second.q_samples[candidate][sample])) {
                return false;
            }
        }
    }
    return true;
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
            old_school::probe_eval::CandidateSamples values;
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
        old_school::probes::make_probe_dev_v3();
    const std::string first_fingerprint =
        old_school::probe_runner::corpus_information_set_fingerprint(
            probes);
    const std::uint64_t first_seed =
        old_school::probe_runner::reference_seed_for_probe(
            old_school::probes::kProbeDevV3,
            probes.front().stable_id);
    const std::uint64_t second_seed =
        old_school::probe_runner::reference_seed_for_probe(
            old_school::probes::kProbeDevV3,
            probes[1].stable_id);
    expect(
        first_fingerprint == "d7168adf7e1aaa27",
        "frozen probe-dev-v3 information-set fingerprint changed");
    expect(first_seed == 0x89D27C5C0BC11CB5ULL,
           "stable FNV-1a seed derivation changed");
    std::reverse(probes.begin(), probes.end());
    expect(
        old_school::probe_runner::corpus_information_set_fingerprint(
            probes) == first_fingerprint,
        "corpus fingerprint depends on iteration order");
    expect(
        old_school::probe_runner::reference_seed_for_probe(
            old_school::probes::kProbeDevV3,
            probes.back().stable_id) == first_seed,
        "probe seed depends on iteration order");
    expect(first_seed != second_seed,
           "distinct probe IDs received the same test seed");
}

void test_old_school_fingerprint_covers_new_public_state() {
    const std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    const std::string baseline =
        old_school::probe_runner::corpus_information_set_fingerprint(
            probes);
    const auto expect_changed =
        [&baseline](const std::vector<DecisionProbe>& changed,
                    std::string_view message) {
            expect(
                old_school::probe_runner::
                    corpus_information_set_fingerprint(changed) !=
                    baseline,
                message);
        };

    std::vector<DecisionProbe> exile = probes;
    expect(!exile.front().state.players[0].library.empty(),
           "fixture has no card available for exile test");
    const auto exiled =
        exile.front().state.players[0].library.back();
    exile.front().state.players[0].library.pop_back();
    exile.front().state.players[0].exile.push_back(exiled);
    expect_changed(exile, "public exile did not change fingerprint");

    std::vector<DecisionProbe> marker = probes;
    auto creature_probe = std::find_if(
        marker.begin(), marker.end(),
        [](const DecisionProbe& probe) {
            return !probe.state.players[0].creatures.empty() ||
                   !probe.state.players[1].creatures.empty();
        });
    expect(creature_probe != marker.end(),
           "fixture has no creature for exile-marker test");
    auto& creature_player =
        !creature_probe->state.players[0].creatures.empty()
            ? creature_probe->state.players[0]
            : creature_probe->state.players[1];
    creature_player.creatures.front().exile_on_death_this_turn = true;
    expect_changed(
        marker, "transient Disintegrate marker did not change fingerprint");

    std::vector<DecisionProbe> growth = probes;
    auto growth_probe = std::find_if(
        growth.begin(), growth.end(),
        [](const DecisionProbe& probe) {
            return !probe.state.players[0].creatures.empty() ||
                   !probe.state.players[1].creatures.empty();
        });
    expect(growth_probe != growth.end(),
           "fixture has no creature for temporary-bonus test");
    auto& growth_player =
        !growth_probe->state.players[0].creatures.empty()
            ? growth_probe->state.players[0]
            : growth_probe->state.players[1];
    growth_player.creatures.front().temporary_power_bonus = 3;
    growth_player.creatures.front().temporary_toughness_bonus = 3;
    expect_changed(
        growth, "temporary Giant Growth bonus did not change fingerprint");

    std::vector<DecisionProbe> stack_x = probes;
    auto stack_probe = std::find_if(
        stack_x.begin(), stack_x.end(),
        [](const DecisionProbe& probe) {
            return !probe.state.stack.empty();
        });
    expect(stack_probe != stack_x.end(),
           "fixture has no stack object for public-X test");
    stack_probe->state.stack.front().x_value = 3;
    expect_changed(stack_x, "public stack X did not change fingerprint");

    std::vector<DecisionProbe> action_x = probes;
    auto* priority = std::get_if<old_school::PriorityAction>(
        &action_x.front().candidates.front().action);
    expect(priority != nullptr,
           "fixture has no priority action for public-X test");
    priority->x_value = 2;
    expect_changed(action_x, "candidate action X did not change fingerprint");

    std::vector<DecisionProbe> mana_pool = probes;
    mana_pool.front().state.players[0].mana_pool.blue = 1;
    expect_changed(
        mana_pool, "public floating mana did not change fingerprint");

    std::vector<DecisionProbe> extra_turn = probes;
    extra_turn.front().state.extra_turns_pending[0] = 1;
    expect_changed(
        extra_turn, "public queued extra turn did not change fingerprint");

    std::vector<DecisionProbe> failed_draw = probes;
    failed_draw.front().state.failed_draw[0] = true;
    expect_changed(
        failed_draw, "terminal failed-draw marker did not change fingerprint");
}

void test_candidate_mapping_is_descriptor_safe() {
    const auto probes = old_school::probes::make_probe_dev_v3();
    const DecisionProbe& priority =
        first_probe_of_kind(probes, DecisionKind::Priority);
    LearnedActionSamples priority_rows;
    for (std::size_t candidate = 0;
         candidate < priority.candidates.size(); ++candidate) {
        priority_rows.q_samples.push_back(
            {0.1 * static_cast<double>(candidate + 1), 0.5});
    }
    const auto priority_mapped =
        old_school::probe_runner::map_candidate_samples(
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
        old_school::probe_runner::map_candidate_samples(
            attack, attack_rows);
    for (std::size_t candidate = 0;
         candidate < attack.candidates.size(); ++candidate) {
        const auto& decision =
            std::get<old_school::probes::BinaryAttackDecision>(
                attack.candidates[candidate].action);
        expect(
            attack_mapped[candidate].q_samples ==
                attack_rows.q_samples[decision.include ? 1U : 0U],
            "binary attack rows were not remapped by action");
    }
}

void test_reference_resource_bounds_reject_early() {
    const auto probes = old_school::probes::make_probe_dev_v3();
    ProbeScoreConfig config;
    expect(config.scoring_value_continuation_epsilon == 0.0,
           "probe Value continuation epsilon did not default to zero");
    config.reference_worlds = 4097;
    (void)expect_invalid(
        [&]() {
            (void)old_school::probe_runner::make_probe_cache_metadata(
                config, probes, "model");
        },
        "oversized world budget was accepted");
    config.reference_worlds = 2;
    config.reference_horizon_turns = 0;
    (void)old_school::probe_runner::make_probe_cache_metadata(
        config, probes, "model");
    config.reference_horizon_turns = 129;
    (void)expect_invalid(
        [&]() {
            (void)old_school::probe_runner::make_probe_cache_metadata(
                config, probes, "model");
        },
        "oversized horizon was accepted");
    config.reference_horizon_turns = 1;
    config.reference_rollouts_per_world = 257;
    (void)expect_invalid(
        [&]() {
            (void)old_school::probe_runner::make_probe_cache_metadata(
                config, probes, "model");
        },
        "oversized rollout budget was accepted");
    config.reference_rollouts_per_world = 1;
    config.scoring_value_worlds = 1;
    (void)expect_invalid(
        [&]() {
            (void)old_school::probe_runner::make_probe_cache_metadata(
                config, probes, "model");
        },
        "one-world Value scoring budget was accepted");
    config.scoring_value_worlds = 4097;
    (void)expect_invalid(
        [&]() {
            (void)old_school::probe_runner::make_probe_cache_metadata(
                config, probes, "model");
        },
        "oversized Value scoring world budget was accepted");
    config.scoring_value_worlds = 2;
    for (const double invalid : {
             -0.01,
             1.01,
             std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(),
         }) {
        config.scoring_value_continuation_epsilon = invalid;
        (void)expect_invalid(
            [&]() {
                (void)old_school::probe_runner::
                    make_probe_cache_metadata(
                        config, probes, "model");
            },
            "invalid Value continuation epsilon was accepted");
    }
}

void test_cache_roundtrip_and_stale_rejection() {
    const auto probes = old_school::probes::make_probe_dev_v3();
    ProbeScoreConfig config;
    config.training_games = 7;
    config.training_seed = 12345;
    config.reference_worlds = 2;
    config.reference_horizon_turns = 1;
    config.reference_rollouts_per_world = 1;
    const auto metadata =
        old_school::probe_runner::make_probe_cache_metadata(
            config, probes, "synthetic-model-fingerprint-v1");
    expect(
        metadata.schema ==
            "old-school-probe-label-cache-v3",
        "cache metadata did not use the Old School hard-cut schema");
    expect(
        metadata.corpus_id ==
            "old-school-probe-dev-v3",
        "cache metadata retained the pre-Old-School corpus identity");
    expect(
        metadata.semantic_revision ==
            "old-school-probe-score-semantics-v3",
        "cache metadata retained the pre-Old-School semantics identity");
    expect(
        metadata.environment_revision ==
            old_school::probe_runner::kProbeEnvironmentRevision,
        "cache metadata omitted the cleanup-discard environment identity");
    expect(
        ProbeScoreConfig{}.cache_path ==
            std::filesystem::path(
                "data/old-school-probe-dev-v3-env-v3.labels.tsv"),
        "default cache path can collide with the legacy cache");
    const auto samples = synthetic_samples(probes, 2);
    TemporaryDirectory directory;
    const auto path = directory.path() / "nested" / "labels.tsv";
    old_school::probe_runner::write_probe_label_cache_atomic(
        path, metadata, probes, samples);
    expect(std::filesystem::exists(path),
           "atomic cache writer did not publish target");
    {
        std::ifstream cache(path);
        std::string magic;
        std::getline(cache, magic);
        expect(
            magic == "# old-school-probe-label-cache-v3",
            "cache writer emitted the legacy magic header");
    }
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
        old_school::probe_runner::load_probe_label_cache(
            path, metadata, probes);
    expect(labels.size() == probes.size(),
           "cache roundtrip changed probe count");
    expect(labels.front().stable_id < labels.back().stable_id,
           "cache did not load in deterministic stable-ID order");

    auto stale = metadata;
    ++stale.training_seed;
    const std::string error = expect_invalid(
        [&]() {
            (void)old_school::probe_runner::load_probe_label_cache(
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
            (void)old_school::probe_runner::load_probe_label_cache(
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
            (void)old_school::probe_runner::load_probe_label_cache(
                path, stale, probes);
        },
        "cache from a different scoring revision was accepted");
    expect(semantics_error.find("semantic_revision") !=
               std::string::npos,
           "semantic-revision mismatch was not identified");

    stale = metadata;
    stale.environment_revision = "old-school-environment-v2";
    const std::string environment_error = expect_invalid(
        [&]() {
            (void)old_school::probe_runner::load_probe_label_cache(
                path, stale, probes);
        },
        "cache from a different rules environment was accepted");
    expect(environment_error.find("environment_revision") !=
               std::string::npos,
           "rules-environment mismatch was not identified");

    const auto legacy_path =
        directory.path() / "legacy-alpha-cache.tsv";
    {
        std::ofstream legacy(legacy_path);
        legacy << "# alpha-probe-label-cache-v2\n";
    }
    const std::string legacy_error = expect_invalid(
        [&]() {
            (void)old_school::probe_runner::load_probe_label_cache(
                legacy_path, metadata, probes);
        },
        "legacy Alpha cache magic was accepted");
    expect(legacy_error.find("unknown magic header") !=
               std::string::npos,
           "legacy cache rejection did not identify its magic");

    const auto v2_path = directory.path() /
                         "old-school-probe-dev-v3-env-v2.labels.tsv";
    {
        std::ofstream v2(v2_path);
        v2 << "# old-school-probe-label-cache-v2\n";
    }
    const std::string v2_error = expect_invalid(
        [&]() {
            (void)old_school::probe_runner::load_probe_label_cache(
                v2_path, metadata, probes);
        },
        "Environment-v2 cache magic was accepted by Environment-v3");
    expect(v2_error.find("unknown magic header") !=
               std::string::npos,
           "Environment-v2 cache rejection did not identify its magic");
}

void test_validation_cache_identity_is_fail_closed() {
    const auto dev = old_school::probes::make_probe_dev_v3();
    const auto validation =
        old_school::probes::make_probe_validation_v1();
    ProbeScoreConfig config;
    config.training_games = 7;
    config.training_seed = 12345;
    config.reference_worlds = 2;
    config.reference_horizon_turns = 1;
    config.reference_rollouts_per_world = 1;

    const auto dev_metadata =
        old_school::probe_runner::make_probe_cache_metadata(
            ProbeCorpusKind::DevV3, config, dev,
            "synthetic-model-fingerprint-v1");
    const auto validation_metadata =
        old_school::probe_runner::make_probe_cache_metadata(
            ProbeCorpusKind::ValidationV1, config, validation,
            "synthetic-model-fingerprint-v1");
    expect(
        dev_metadata.schema ==
                old_school::probe_runner::kProbeCacheSchema &&
            validation_metadata.schema ==
                old_school::probe_runner::
                    kProbeValidationCacheSchema &&
            dev_metadata.schema != validation_metadata.schema,
        "validation cache did not receive a distinct schema");
    expect(
        dev_metadata.corpus_id ==
                old_school::probes::kProbeDevV3 &&
            validation_metadata.corpus_id ==
                old_school::probes::kProbeValidationV1 &&
            dev_metadata.information_set_fingerprint !=
                validation_metadata.information_set_fingerprint,
        "validation cache did not receive a distinct corpus identity");
    expect(
        validation_metadata.semantic_revision ==
                old_school::probe_runner::
                    kProbeValidationSemanticRevision &&
            validation_metadata.semantic_revision !=
                dev_metadata.semantic_revision,
            "validation cache did not receive a distinct semantic revision");
    expect(
        dev_metadata.environment_revision ==
                old_school::probe_runner::kProbeEnvironmentRevision &&
            validation_metadata.environment_revision ==
                old_school::probe_runner::kProbeEnvironmentRevision,
        "probe caches did not bind the cleanup-discard environment");
    expect(
        old_school::probe_runner::default_probe_cache_path(
            ProbeCorpusKind::DevV3) !=
            old_school::probe_runner::default_probe_cache_path(
                ProbeCorpusKind::ValidationV1),
        "validation default cache path collides with dev-v3");
    expect(
        old_school::probe_runner::reference_seed_for_probe(
            old_school::probes::kProbeDevV3,
            validation.front().stable_id) !=
            old_school::probe_runner::reference_seed_for_probe(
            old_school::probes::kProbeValidationV1,
                validation.front().stable_id),
        "validation and dev corpus seed domains collide");

    ProbeScoreConfig clustered = config;
    clustered.reference_rollouts_per_world = 2;
    const std::string clustered_error = expect_invalid(
        [&] {
            (void)old_school::probe_runner::
                make_probe_cache_metadata(
                    ProbeCorpusKind::ValidationV1, clustered,
                    validation, "synthetic-model-fingerprint-v1");
        },
        "validation accepted clustered within-world rollouts");
    expect(
        clustered_error.find("exactly one rollout per world") !=
            std::string::npos,
        "validation clustered-rollout rejection was not actionable");

    std::ostringstream unused_progress;
    const std::string default_path_error = expect_invalid(
        [&] {
            (void)old_school::probe_runner::
                score_probe_corpus_with_candidates(
                    ProbeCorpusKind::ValidationV1,
                    ProbeScoreConfig{}, unused_progress, {});
        },
        "validation silently reused or rewrote the dev cache path");
    expect(
        default_path_error.find(
            "cannot use the dev-v3 default cache path") !=
            std::string::npos,
        "validation default-path rejection was not actionable");

    TemporaryDirectory directory;
    const auto dev_path = directory.path() / "dev.tsv";
    const auto validation_path =
        directory.path() / "validation.tsv";
    old_school::probe_runner::write_probe_label_cache_atomic(
        ProbeCorpusKind::DevV3, dev_path, dev_metadata, dev,
        synthetic_samples(dev, 2));
    old_school::probe_runner::write_probe_label_cache_atomic(
        ProbeCorpusKind::ValidationV1, validation_path,
        validation_metadata, validation,
        synthetic_samples(validation, 2));
    {
        std::ifstream cache(validation_path);
        std::string magic;
        std::getline(cache, magic);
        expect(
            magic ==
                "# old-school-probe-validation-label-cache-v2",
            "validation cache did not receive a distinct magic header");
    }
    expect(
        old_school::probe_runner::load_probe_label_cache(
            ProbeCorpusKind::ValidationV1, validation_path,
            validation_metadata, validation)
                .size() == validation.size(),
        "validation cache did not roundtrip");

    const std::string dev_as_validation = expect_invalid(
        [&] {
            (void)old_school::probe_runner::load_probe_label_cache(
                ProbeCorpusKind::ValidationV1, dev_path,
                validation_metadata, validation);
        },
        "dev-v3 cache was accepted as validation-v1");
    expect(
        dev_as_validation.find("unknown magic header") !=
            std::string::npos,
        "cross-corpus dev cache rejection was not fail-closed");
    const std::string validation_as_dev = expect_invalid(
        [&] {
            (void)old_school::probe_runner::load_probe_label_cache(
                ProbeCorpusKind::DevV3, validation_path,
                dev_metadata, dev);
        },
        "validation-v1 cache was accepted as dev-v3");
    expect(
        validation_as_dev.find("unknown magic header") !=
            std::string::npos,
        "cross-corpus validation cache rejection was not fail-closed");
    const std::string wrong_selector_metadata = expect_invalid(
        [&] {
            (void)old_school::probe_runner::load_probe_label_cache(
                ProbeCorpusKind::ValidationV1, validation_path,
                dev_metadata, validation);
        },
        "dev metadata was accepted under validation selector");
    expect(
        wrong_selector_metadata.find(
            "expected metadata does not match") !=
            std::string::npos,
        "selector/metadata mismatch did not fail before cache use");
}

void test_hidden_clone_preserves_information_set() {
    std::vector<DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    DecisionProbe clone_probe = probes.front();
    clone_probe.state =
        old_school::probe_runner::hidden_repartition_clone(
            probes.front());
    std::vector<DecisionProbe> clone_corpus = probes;
    clone_corpus.front() = clone_probe;

    expect(
        old_school::probe_runner::corpus_information_set_fingerprint(
            probes) ==
            old_school::probe_runner::corpus_information_set_fingerprint(
                clone_corpus),
        "opponent hidden repartition changed corpus fingerprint");
    expect(old_school::probes::hidden_clone_is_determinization_invariant(
               probes.front(), 99123),
           "probe clone changed fixed-seed determinization");
}

void test_tiny_reference_is_hidden_clone_invariant() {
    const auto probes = old_school::probes::make_probe_dev_v3();
    ProbeScoreConfig config;
    config.training_games = 1;
    config.training_seed = 777;
    config.reference_worlds = 2;
    config.reference_horizon_turns = 1;
    config.reference_rollouts_per_world = 1;
    const auto model =
        old_school::train_learned_actor_model(
            config.training_games, config.training_seed);
    const auto samples =
        old_school::probe_runner::generate_probe_reference_samples(
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

void test_priority_evaluation_threads_are_bit_identical() {
    const auto controls =
        old_school::probes::make_force_spike_policy_controls_v1();
    const DecisionProbe& live = controls.front();
    const auto model =
        old_school::train_learned_value_champion(
            1, 0x54485245414453ULL);

    std::vector<old_school::PriorityAction> actions;
    actions.reserve(live.candidates.size());
    for (const auto& candidate : live.candidates) {
        const auto* action =
            std::get_if<old_school::PriorityAction>(
                &candidate.action);
        expect(action != nullptr,
               "Force Spike control contains a non-Priority action");
        actions.push_back(*action);
    }

    old_school::LearnedSearchConfig serial{
        .seed =
            old_school::probe_runner::reference_seed_for_probe(
                old_school::probes::
                    kForceSpikePolicyControlsV1,
                live.stable_id),
        .worlds = 8,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .blend_shallow_prior = false,
        .evaluation_threads = 1,
    };
    auto parallel = serial;
    parallel.evaluation_threads = 4;

    const auto score =
        [&](const old_school::GameState& state,
            const old_school::LearnedSearchConfig& config) {
            return old_school::learned_priority_action_samples(
                state, live.original_decks, live.root_player,
                true, live.phase, live.consecutive_passes,
                actions, model, config);
        };
    const old_school::GameState hidden =
        old_school::probe_runner::hidden_repartition_clone(live);
    const LearnedActionSamples serial_original =
        score(live.state, serial);
    const LearnedActionSamples parallel_original =
        score(live.state, parallel);
    const LearnedActionSamples serial_hidden =
        score(hidden, serial);
    const LearnedActionSamples parallel_hidden =
        score(hidden, parallel);

    expect(
        action_samples_are_bit_identical(
            serial_original, parallel_original) &&
            action_samples_are_bit_identical(
                serial_hidden, parallel_hidden) &&
            action_samples_are_bit_identical(
                serial_original, serial_hidden) &&
            action_samples_are_bit_identical(
                parallel_original, parallel_hidden),
        "preindexed four-thread Priority scoring changed raw "
        "samples, counters, order, or hidden-repartition identity");

    auto invalid = serial;
    invalid.evaluation_threads = 0;
    const std::string zero_threads = expect_invalid(
        [&] {
            static_cast<void>(score(live.state, invalid));
        },
        "Priority scorer accepted zero evaluation threads");
    expect(
        zero_threads.find("[1, 64]") != std::string::npos,
        "evaluation-thread validation was not actionable");
    invalid.evaluation_threads = 65;
    static_cast<void>(expect_invalid(
        [&] {
            static_cast<void>(score(live.state, invalid));
        },
        "Priority scorer accepted too many evaluation threads"));
}

void test_value_attack_probe_scores_are_seed_independent() {
    const auto probes = old_school::probes::make_probe_dev_v3();
    const auto value_model =
        old_school::train_learned_value_champion(1, 778);
    for (const DecisionProbe& probe : probes) {
        if (probe.decision_kind != DecisionKind::Attack) {
            continue;
        }
        std::vector<std::vector<old_school::PermanentId>> attack_sets;
        for (const auto& candidate : probe.candidates) {
            const auto& decision =
                std::get<old_school::probes::BinaryAttackDecision>(
                    candidate.action);
            attack_sets.push_back(
                decision.include
                    ? std::vector<old_school::PermanentId>{
                          decision.attacker}
                    : std::vector<old_school::PermanentId>{});
        }
        const std::uint64_t deployed_seed =
            old_school::probe_runner::reference_seed_for_probe(
                old_school::probes::kProbeDevV3, probe.stable_id,
                old_school::probe_runner::kProbeProductionPolicySeed);
        const auto first =
            old_school::learned_value_attack_set_scores(
                probe.state, probe.root_player, attack_sets,
                value_model, deployed_seed);
        const auto second =
            old_school::learned_value_attack_set_scores(
                probe.state, probe.root_player, attack_sets,
                value_model, deployed_seed + 1);
        expect(first.scores == second.scores &&
                   first.selected_candidate ==
                       second.selected_candidate,
               "current exhaustive attack probe depends on production "
               "RNG seed");
    }
}

void test_force_spike_control_gate_and_report_semantics() {
    using old_school::probe_runner::
        ForceSpikePolicyControlReport;

    const ForceSpikePolicyControlReport passing{
        .policy_name = "Value G0",
        .model_fingerprint = "value-g0-fingerprint",
        .worlds = 8,
        .horizon_turns = 4,
        .live = {
            .stable_id =
                "control.blue.force-spike-live-gray-ogre.v1",
            .pass_score = 0.1,
            .force_spike_score = 0.9,
            .selected_keys = {"force-spike-gray-ogre"},
        },
        .payable = {
            .stable_id =
                "control.blue.force-spike-payable-gray-ogre.v1",
            .pass_score = 0.8,
            .force_spike_score = 0.2,
            .selected_keys = {"pass"},
        },
        .hidden_repartition_passed = true,
    };
    expect(
        passing.live_selects_force_spike() &&
            passing.payable_selects_pass() &&
            passing.gate_passed(),
        "Force Spike behavioral gate rejected the intended pair");

    ForceSpikePolicyControlReport tied = passing;
    tied.policy_name = "Value C1";
    tied.live.selected_keys = {
        "force-spike-gray-ogre", "pass"};
    expect(
        !tied.live_selects_force_spike() &&
            tied.payable_selects_pass() &&
            !tied.gate_passed(),
        "Force Spike gate blessed a random exact-max tie");
    ForceSpikePolicyControlReport hidden_failure = passing;
    hidden_failure.hidden_repartition_passed = false;
    expect(!hidden_failure.gate_passed(),
           "Force Spike gate ignored hidden-repartition failure");

    ProbeScoreReport report;
    report.metadata.corpus_id =
        std::string(old_school::probes::kProbeDevV3);
    report.force_spike_controls = {passing, tied};
    const std::string output =
        old_school::probe_runner::format_probe_score_report(report);
    expect(
        output.find(
            "Supplemental Force Spike deployed controls") !=
                std::string::npos &&
            output.find(
                "excluded from balanced metrics, cache identity, "
                "and promotion claims") != std::string::npos &&
            output.find(
                "Value G0: fingerprint value-g0-fingerprint, "
                "K=8/H=4") != std::string::npos &&
            output.find(
                "live: Pass=0.1000, Force Spike=0.9000, "
                "selected {force-spike-gray-ogre} [PASS]") !=
                std::string::npos &&
            output.find(
                "payable: Pass=0.8000, Force Spike=0.2000, "
                "selected {pass} [PASS]") !=
                std::string::npos &&
            output.find("behavioral gate: PASS") !=
                std::string::npos &&
            output.find("behavioral gate: FAIL") !=
                std::string::npos &&
            output.find("Value Priority residual weight") ==
                std::string::npos,
        "Force Spike control report lost exact scores, selections, "
        "gate status, or legacy-zero formatting");
}

void test_force_spike_control_scorer_uses_deployed_value_path() {
    const auto model =
        old_school::train_learned_value_champion(1, 0xF05CEULL);
    const auto first =
        old_school::probe_runner::
            score_value_force_spike_policy_controls(
                model, "Synthetic Value", 2);
    const auto repeated =
        old_school::probe_runner::
            score_value_force_spike_policy_controls(
                model, "Synthetic Value", 2);
    const auto explicit_zero =
        old_school::probe_runner::
            score_value_force_spike_policy_controls(
                model, "Synthetic Value", 2, 0.0, 0.0);

    expect(
        first == repeated && first == explicit_zero,
        "explicit zero changed legacy deployed Force Spike scores");
    expect(
        first.model_fingerprint ==
                old_school::learned_model_fingerprint(model) &&
            first.worlds == 2 &&
            first.horizon_turns == 4 &&
            first.hidden_repartition_passed,
        "Force Spike control scorer did not report deployed K/H or "
        "hidden invariance");
    expect(
        first.live.stable_id ==
                "control.blue.force-spike-live-gray-ogre.v1" &&
            first.payable.stable_id ==
                "control.blue.force-spike-payable-gray-ogre.v1" &&
            std::isfinite(first.live.pass_score) &&
            std::isfinite(first.live.force_spike_score) &&
            std::isfinite(first.payable.pass_score) &&
            std::isfinite(first.payable.force_spike_score) &&
            !first.live.selected_keys.empty() &&
            !first.payable.selected_keys.empty(),
        "Force Spike control scorer omitted a state, score, or "
        "exact-max selection");
    ProbeScoreReport default_report;
    default_report.metadata.corpus_id =
        std::string(old_school::probes::kProbeDevV3);
    default_report.force_spike_controls = {first};
    ProbeScoreReport explicit_zero_report = default_report;
    explicit_zero_report.force_spike_controls = {explicit_zero};
    expect(
        old_school::probe_runner::format_probe_score_report(
            default_report) ==
            old_school::probe_runner::format_probe_score_report(
                explicit_zero_report),
        "explicit zero changed legacy Force Spike report output");

    const std::string world_error = expect_invalid(
        [&] {
            static_cast<void>(
                old_school::probe_runner::
                    score_value_force_spike_policy_controls(
                        model, "Synthetic Value", 1));
        },
        "Force Spike controls accepted an under-sampled K");
    expect(world_error.find("[2, 4096]") != std::string::npos,
           "Force Spike K validation was not actionable");
}

void test_force_spike_residual_uses_trained_priority_head() {
    const auto parent =
        old_school::train_learned_value_champion(
            1, 0x5052494FULL);
    const auto controls =
        old_school::probes::make_force_spike_policy_controls_v1();
    const DecisionProbe& live = controls.front();
    const bool sorcery_actions =
        live.phase == old_school::TurnPhase::FirstMain ||
        live.phase == old_school::TurnPhase::SecondMain;

    std::vector<old_school::PriorityAction> actions;
    std::vector<std::vector<double>> options;
    actions.reserve(live.candidates.size());
    options.reserve(live.candidates.size());
    for (const auto& candidate : live.candidates) {
        const auto* action =
            std::get_if<old_school::PriorityAction>(
                &candidate.action);
        expect(action != nullptr,
               "Force Spike control contains a non-Priority action");
        actions.push_back(*action);
        options.push_back(
            old_school::learned_priority_policy_features(
                live.state, live.root_player, *action,
                sorcery_actions, live.phase,
                live.consecutive_passes));
    }
    expect(options.size() >= 2,
           "Force Spike control lost its policy alternatives");

    std::vector<double> targets(options.size(), 0.0);
    targets.front() = 1.0;
    const old_school::LearnedValuePriorityTrainingExample example{
        .options = options,
        .base_scores =
            std::vector<double>(options.size(), 0.5),
        .target_probabilities = targets,
        .weight = 1.0,
    };
    const auto trained =
        old_school::update_learned_value_priority_head(
            parent, {example},
            {
                .batch_size = 1,
                .epochs = 20,
                .learning_rate = 0.02,
                .beta1 = 0.9,
                .beta2 = 0.999,
                .epsilon = 1.0e-8,
                .global_gradient_norm_clip = 5.0,
                .seed = 0x524553494455414CULL,
                .residual_weight = 0.10,
                .policy_temperature = 0.10,
            });
    const auto logits =
        old_school::learned_policy_head_logits(
            options,
            old_school::LearnedPolicyDecisionKind::Priority,
            trained);
    const auto [minimum_logit, maximum_logit] =
        std::minmax_element(logits.begin(), logits.end());
    expect(
        maximum_logit != logits.end() &&
            minimum_logit != logits.end() &&
            *maximum_logit != *minimum_logit,
        "synthetic Priority-head training remained uniform");

    const auto combined =
        old_school::diagnose_learned_value_priority(
            live.state, live.original_decks, live.root_player,
            sorcery_actions, live.phase,
            live.consecutive_passes, trained, 2,
            0x524553494455414CULL, 0.0, 0.10);
    expect(
        combined.scores.size() == combined.base_scores.size() &&
            combined.scores.size() ==
                combined.priority_residuals.size() &&
            std::all_of(
                combined.scores.begin(), combined.scores.end(),
                [](double score) {
                    return std::isfinite(score);
                }) &&
            std::all_of(
                combined.priority_residuals.begin(),
                combined.priority_residuals.end(),
                [](double residual) {
                    return std::isfinite(residual);
                }) &&
            std::any_of(
                combined.priority_residuals.begin(),
                combined.priority_residuals.end(),
                [](double residual) {
                    return residual != 0.0;
                }),
        "trained nonuniform Priority head did not produce finite "
        "combined Value scores");

    const auto legacy =
        old_school::probe_runner::
            score_value_force_spike_policy_controls(
                trained, "Synthetic residual Value", 2, 0.0, 0.0);
    const auto residual =
        old_school::probe_runner::
            score_value_force_spike_policy_controls(
                trained, "Synthetic residual Value", 2, 0.0, 0.10);
    const bool score_changed =
        legacy.live.pass_score != residual.live.pass_score ||
        legacy.live.force_spike_score !=
            residual.live.force_spike_score ||
        legacy.payable.pass_score != residual.payable.pass_score ||
        legacy.payable.force_spike_score !=
            residual.payable.force_spike_score;
    expect(
        residual.value_priority_residual_weight == 0.10 &&
            residual.hidden_repartition_passed &&
            std::isfinite(residual.live.pass_score) &&
            std::isfinite(residual.live.force_spike_score) &&
            std::isfinite(residual.payable.pass_score) &&
            std::isfinite(residual.payable.force_spike_score) &&
            score_changed,
        "residual Force Spike scoring did not exercise finite "
        "combined scores or hidden repartition");

    ProbeScoreReport report;
    report.metadata.corpus_id =
        std::string(old_school::probes::kProbeDevV3);
    report.force_spike_controls = {residual};
    expect(
        old_school::probe_runner::format_probe_score_report(report)
                .find(
                    "Value Priority residual weight=0.1000") !=
            std::string::npos,
        "Force Spike report did not identify the residual scorer");
}

void test_teacher_audit_ordered_blocks_are_exact() {
    std::vector<double> first(32, 0.6);
    std::vector<double> second(32, 0.4);
    std::fill(first.begin() + 24, first.end(), 0.4);
    const auto three_of_four =
        old_school::probe_runner::summarize_ordered_pair_blocks(
            first, second);
    expect(
        three_of_four.worlds_per_block == 8 &&
            three_of_four.block_count == 4 &&
            three_of_four.correct_block_count == 3 &&
            three_of_four.required_correct_block_count() == 3 &&
            three_of_four.gate_passed(),
        "ordered K=8 blocks did not implement the 75% gate");

    // The fourth block is an exact tie and must not be credited.
    std::fill(first.begin() + 16, first.begin() + 24, 0.4);
    const auto two_of_four =
        old_school::probe_runner::summarize_ordered_pair_blocks(
            first, second);
    expect(
        two_of_four.correct_block_count == 2 &&
            !two_of_four.gate_passed(),
        "ordered block gate credited an incorrect or tied block");

    const std::string shape_error = expect_invalid(
        [] {
            static_cast<void>(
                old_school::probe_runner::
                    summarize_ordered_pair_blocks(
                        {0.5, 0.5}, {0.4}, 1));
        },
        "ordered block scorer accepted mismatched samples");
    expect(
        shape_error.find("equally sized") != std::string::npos,
        "ordered block shape error was not actionable");
    const std::string divisibility_error = expect_invalid(
        [] {
            static_cast<void>(
                old_school::probe_runner::
                    summarize_ordered_pair_blocks(
                        std::vector<double>(9, 0.5),
                        std::vector<double>(9, 0.4)));
        },
        "ordered block scorer accepted a partial K=8 block");
    expect(
        divisibility_error.find("divisible") != std::string::npos,
        "ordered block divisibility error was not actionable");
}

void test_teacher_sufficiency_audit_is_generic_and_hidden_safe() {
    using old_school::probe_runner::TeacherSufficiencyAuditConfig;
    const auto value =
        old_school::train_learned_value_champion(1, 0x7EA0E2ULL);
    const TeacherSufficiencyAuditConfig value_config{
        .worlds = 8,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .blend_shallow_prior = false,
    };
    const auto first =
        old_school::probe_runner::score_teacher_sufficiency_audit(
            value, "Synthetic Value K8/H0", value_config);
    const auto repeated =
        old_school::probe_runner::score_teacher_sufficiency_audit(
            value, "Synthetic Value K8/H0", value_config);
    expect(first == repeated,
           "teacher-sufficiency audit is not deterministic");
    auto parallel_config = value_config;
    parallel_config.evaluation_threads = 4;
    const auto parallel =
        old_school::probe_runner::score_teacher_sufficiency_audit(
            value, "Synthetic Value K8/H0", parallel_config);
    auto serial_with_parallel_identity = first;
    serial_with_parallel_identity.config = parallel_config;
    expect(
        parallel == serial_with_parallel_identity,
        "teacher audit changed a report field when only execution "
        "parallelism changed");
    expect(
        first.model_fingerprint ==
                old_school::learned_model_fingerprint(value) &&
            first.config == value_config &&
            first.hidden_repartition_bit_identical,
        "teacher audit lost its frozen model/config identity or "
        "hidden safety");

    const std::array<
        const old_school::probe_runner::TeacherOptionComparison*, 3>
        comparisons = {
            &first.force_spike_live,
            &first.force_spike_payable,
            &first.disintegrate_x_zero,
        };
    for (const auto* comparison : comparisons) {
        expect(
            comparison->estimate.samples_per_candidate == 8 &&
                comparison->ordered_blocks.worlds_per_block == 8 &&
                comparison->ordered_blocks.block_count == 1 &&
                comparison->ordered_blocks
                        .required_correct_block_count() == 1 &&
                comparison->hidden_repartition_bit_identical &&
                !comparison->selected_keys.empty() &&
                std::isfinite(comparison->estimate.delta_q) &&
                std::isfinite(
                    comparison->estimate.paired_standard_error) &&
                std::isfinite(
                    comparison->estimate.confidence_lower_95) &&
                std::isfinite(
                    comparison->estimate.confidence_upper_95),
            "tiny teacher audit omitted paired statistics, exact "
            "selection, K=8 accounting, or hidden invariance");
        expect(
            comparison->candidate_count != 0 &&
                comparison->expected_evaluations ==
                    comparison->candidate_count * 8 &&
                comparison->recorded_candidate_samples ==
                    comparison->expected_evaluations &&
                comparison->rollout_evaluations ==
                    comparison->expected_evaluations &&
                comparison->terminal_evaluations +
                        comparison->bootstrapped_evaluations ==
                    comparison->rollout_evaluations &&
                comparison->evaluation_accounting_is_exact() &&
                !comparison
                     ->conservative_terminal_bound_satisfied &&
                !comparison->terminal_results_gate_passed(),
            "teacher audit did not retain exact terminal/bootstrap "
            "accounting for its shallow default mode");
    }
    const auto validation =
        old_school::probes::make_probe_validation_v1();
    const auto validation_pass = std::find_if(
        validation.front().candidates.begin(),
        validation.front().candidates.end(),
        [](const old_school::probes::Candidate& candidate) {
            const auto* action =
                std::get_if<old_school::PriorityAction>(
                    &candidate.action);
            return action != nullptr &&
                   action->kind ==
                       old_school::PriorityActionKind::Pass;
        });
    expect(
        first.force_spike_live.estimate.first_key ==
                "force-spike-gray-ogre" &&
            first.force_spike_live.estimate.second_key == "pass" &&
            first.force_spike_payable.estimate.first_key == "pass" &&
            first.force_spike_payable.estimate.second_key ==
                "force-spike-gray-ogre" &&
            validation_pass !=
                validation.front().candidates.end() &&
            first.disintegrate_x_zero.estimate.first_key ==
                validation_pass->descriptor,
        "teacher audit did not orient the three comparisons toward "
        "the preregistered correct action");

    const auto actor =
        old_school::train_learned_actor_model(1, 0x7EA0E2ULL);
    const TeacherSufficiencyAuditConfig actor_config{
        .worlds = 8,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::UnifiedActor,
        .blend_shallow_prior = false,
    };
    const auto actor_report =
        old_school::probe_runner::score_teacher_sufficiency_audit(
            actor, "Synthetic Actor K8/H0", actor_config);
    expect(
        actor_report.config.continuation_variant ==
                old_school::LearnedVariant::UnifiedActor &&
            actor_report.hidden_repartition_bit_identical,
        "generic teacher audit did not support an Actor mirror");

    TeacherSufficiencyAuditConfig blended_config = value_config;
    blended_config.blend_shallow_prior = true;
    const auto blended_report =
        old_school::probe_runner::score_teacher_sufficiency_audit(
            value, "Synthetic Value K8/H0 blended",
            blended_config);
    expect(
        blended_report.config.blend_shallow_prior &&
            blended_report.hidden_repartition_bit_identical &&
            blended_report.force_spike_live.estimate
                    .samples_per_candidate == 8,
        "generic teacher audit did not support a shallow-prior blend");

    const std::string formatted =
        old_school::probe_runner::
            format_teacher_sufficiency_audit_report(
                {first, actor_report});
    expect(
        formatted.find("P16 Search-Teacher Sufficiency Audit") !=
                std::string::npos &&
            formatted.find("no probe-label cache access or mutation") !=
                std::string::npos &&
            formatted.find("[PRIMARY] Synthetic Value K8/H0") !=
                std::string::npos &&
            formatted.find("[DIAGNOSTIC] Synthetic Actor K8/H0") !=
                std::string::npos &&
            formatted.find("oriented delta: Q(") !=
                std::string::npos &&
            formatted.find("paired SE") != std::string::npos &&
            formatted.find("95% CI") != std::string::npos &&
            formatted.find("exact selected keys: {") !=
                std::string::npos &&
            formatted.find("ordered K=8 blocks:") !=
                std::string::npos &&
            formatted.find("Primary teacher gate:") !=
                std::string::npos,
        "teacher audit formatter omitted preregistered evidence");

    const std::string world_error = expect_invalid(
        [&] {
            auto invalid = value_config;
            invalid.worlds = 9;
            static_cast<void>(
                old_school::probe_runner::
                    score_teacher_sufficiency_audit(
                        value, "Invalid K", invalid));
        },
        "teacher audit accepted a partial K=8 block");
    expect(
        world_error.find("multiple of 8") != std::string::npos,
        "teacher audit world validation was not actionable");
    const std::string terminal_bound_error = expect_invalid(
        [&] {
            auto invalid = value_config;
            invalid.require_terminal_results = true;
            static_cast<void>(
                old_school::probe_runner::
                    score_teacher_sufficiency_audit(
                        value, "Terminal horizon too short",
                        invalid));
        },
        "terminal teacher audit accepted an uncertified horizon");
    expect(
        terminal_bound_error.find("conservative bound") !=
                std::string::npos &&
            terminal_bound_error.find(
                "sum of both libraries plus one") !=
                std::string::npos,
        "terminal horizon validation was not actionable");
    const std::string shallow_blend_error = expect_invalid(
        [&] {
            auto invalid = value_config;
            invalid.horizon_turns = 128;
            invalid.blend_shallow_prior = true;
            invalid.require_terminal_results = true;
            static_cast<void>(
                old_school::probe_runner::
                    score_teacher_sufficiency_audit(
                        value, "Terminal shallow blend", invalid));
        },
        "terminal teacher audit accepted a shallow critic blend");
    expect(
        shallow_blend_error.find("cannot blend") !=
            std::string::npos,
        "terminal shallow-blend validation was not actionable");
    const std::string null_error = expect_invalid(
        [&] {
            static_cast<void>(
                old_school::probe_runner::
                    score_teacher_sufficiency_audit(
                        nullptr, "Null", value_config));
        },
        "teacher audit accepted a null model");
    expect(
        null_error.find("frozen model") != std::string::npos,
        "teacher audit null-model validation was not actionable");
}

void test_terminal_credit_gate_is_focused_and_fail_closed() {
    using old_school::probe_runner::TeacherOptionComparison;
    using old_school::probe_runner::TeacherSufficiencyAuditReport;

    const auto passing_comparison =
        [](std::string first, std::string second) {
            TeacherOptionComparison comparison;
            comparison.description = "synthetic terminal pair";
            comparison.estimate.first_key = std::move(first);
            comparison.estimate.second_key = std::move(second);
            comparison.estimate.samples_per_candidate = 32;
            comparison.estimate.delta_q = 0.10;
            comparison.estimate.paired_standard_error = 0.01;
            comparison.estimate.confidence_lower_95 = 0.08;
            comparison.estimate.confidence_upper_95 = 0.12;
            comparison.selected_keys = {
                comparison.estimate.first_key};
            comparison.ordered_blocks = {
                .worlds_per_block = 8,
                .block_count = 4,
                .correct_block_count = 3,
            };
            comparison.hidden_repartition_bit_identical = true;
            comparison.candidate_count = 3;
            comparison.recorded_candidate_samples = 96;
            comparison.expected_evaluations = 96;
            comparison.rollout_evaluations = 96;
            comparison.terminal_evaluations = 96;
            comparison.bootstrapped_evaluations = 0;
            comparison.conservative_terminal_bound_turns = 80;
            comparison.conservative_terminal_bound_satisfied = true;
            return comparison;
        };

    TeacherSufficiencyAuditReport report;
    report.policy_name = "Synthetic full-terminal teacher";
    report.model_fingerprint = "synthetic";
    report.config = {
        .worlds = 32,
        .horizon_turns = 128,
        .continuation_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .blend_shallow_prior = false,
        .require_terminal_results = true,
    };
    report.force_spike_live = passing_comparison(
        "force-spike-gray-ogre", "pass");
    report.force_spike_payable = passing_comparison(
        "pass", "force-spike-gray-ogre");

    // Deliberately make every old X=0 sign/integrity gate fail. It is a
    // diagnostic row in this experiment and must not veto the controlled
    // Force Spike ordering hypothesis.
    report.disintegrate_x_zero =
        passing_comparison("pass", "x-zero");
    report.disintegrate_x_zero.selected_keys = {"cast-orc"};
    report.disintegrate_x_zero.estimate.confidence_lower_95 = -0.2;
    report.disintegrate_x_zero.ordered_blocks.correct_block_count = 0;
    report.disintegrate_x_zero.hidden_repartition_bit_identical =
        false;
    report.disintegrate_x_zero.terminal_evaluations = 95;
    report.disintegrate_x_zero.bootstrapped_evaluations = 1;
    report.hidden_repartition_bit_identical = false;

    expect(
        report.force_spike_live.evaluation_accounting_is_exact() &&
            report.force_spike_live
                .terminal_results_gate_passed() &&
            report.disintegrate_x_zero
                .evaluation_accounting_is_exact() &&
            !report.disintegrate_x_zero
                 .terminal_results_gate_passed() &&
            report.disintegrate_x_zero
                .second_key_excluded_from_selected_set() &&
            old_school::probe_runner::
                terminal_credit_primary_gate_passed(report),
        "terminal-credit primary gate was not focused on the two "
        "Force Spike controls");

    const std::string formatted =
        old_school::probe_runner::
            format_terminal_credit_audit_report(report);
    expect(
        formatted.find("Full-Terminal Credit Audit") !=
                std::string::npos &&
            formatted.find("[PRIMARY 1/2]") !=
                std::string::npos &&
            formatted.find("[PRIMARY 2/2]") !=
                std::string::npos &&
            formatted.find("[DIAGNOSTIC ONLY]") !=
                std::string::npos &&
            formatted.find(
                "X=0 excluded from exact selected-best set: PASS") !=
                std::string::npos &&
            formatted.find(
                "do not enter the primary Force Spike gate") !=
                std::string::npos &&
            formatted.find("bootstrapped 1 [EXACT]") !=
                std::string::npos &&
            formatted.find(
                "Primary terminal-credit gate: PASS") !=
                std::string::npos,
        "terminal-credit formatter omitted focused verdict, "
        "terminal accounting, or diagnostic X=0 exclusion");

    TeacherSufficiencyAuditReport critic_leak = report;
    critic_leak.force_spike_live.terminal_evaluations = 95;
    critic_leak.force_spike_live.bootstrapped_evaluations = 1;
    expect(
        critic_leak.force_spike_live
                .evaluation_accounting_is_exact() &&
            !critic_leak.force_spike_live
                 .terminal_results_gate_passed() &&
            !old_school::probe_runner::
                 terminal_credit_primary_gate_passed(critic_leak),
        "terminal-credit gate did not fail closed on a primary "
        "critic bootstrap");

    TeacherSufficiencyAuditReport bad_bound = report;
    bad_bound.force_spike_payable
        .conservative_terminal_bound_satisfied = false;
    expect(
        !old_school::probe_runner::
             terminal_credit_primary_gate_passed(bad_bound),
        "terminal-credit gate ignored a failed conservative bound");

    TeacherSufficiencyAuditReport shallow = report;
    shallow.config.require_terminal_results = false;
    expect(
        !old_school::probe_runner::
             terminal_credit_primary_gate_passed(shallow),
        "terminal-credit gate accepted a non-terminal audit config");
    TeacherSufficiencyAuditReport blended = report;
    blended.config.blend_shallow_prior = true;
    expect(
        !old_school::probe_runner::
             terminal_credit_primary_gate_passed(blended),
        "terminal-credit gate accepted a shallow critic blend");

    report.disintegrate_x_zero.selected_keys = {"x-zero", "cast-orc"};
    expect(
        !report.disintegrate_x_zero
             .second_key_excluded_from_selected_set() &&
            old_school::probe_runner::
                terminal_credit_primary_gate_passed(report),
        "X=0 selected-set diagnostic leaked into the primary gate");
}

void test_value_decision_detail_respects_ties_and_selectors() {
    const auto label = old_school::probe_eval::make_probe_label(
        "red.synthetic-selection", DeckId::Red,
        {
            {"best", {0.8, 0.8}},
            {"other", {0.4, 0.4}},
        });
    const old_school::probe_eval::ProbePrediction uniform_tie{
        "red.synthetic-selection",
        {{"other", 1.0}, {"best", 1.0}},
        0.7,
    };
    const auto uniform_detail =
        old_school::probe_runner::make_value_probe_decision_detail(
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
    expect(
        !old_school::probe_runner::
             value_decision_uniquely_selects(
                 uniform_detail, "best") &&
            !old_school::probe_runner::
                 value_decision_uniquely_selects(
                     uniform_detail, "other"),
        "multi-action exact tie was mistaken for a unique choice");

    const old_school::probe_eval::ProbePrediction unique_priority{
        "red.synthetic-selection",
        {{"other", 1.0}, {"best", 2.0}},
        0.7,
    };
    const auto unique_priority_detail =
        old_school::probe_runner::make_value_probe_decision_detail(
            label, unique_priority);
    expect(
        unique_priority_detail.selected_keys ==
                std::vector<std::string>{"best"} &&
            !unique_priority_detail.deterministic_selection &&
            old_school::probe_runner::
                value_decision_uniquely_selects(
                    unique_priority_detail, "best") &&
            !old_school::probe_runner::
                 value_decision_uniquely_selects(
                     unique_priority_detail, "other"),
        "singleton Priority argmax did not count as a unique choice");

    const old_school::probe_eval::ProbePrediction deterministic_tie{
        "red.synthetic-selection",
        {{"best", 1.0}, {"other", 1.0}},
        0.5,
        "other",
    };
    const auto deterministic_detail =
        old_school::probe_runner::make_value_probe_decision_detail(
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
    expect(
        old_school::probe_runner::
            value_decision_uniquely_selects(
                deterministic_detail, "other"),
        "deterministic selected key did not count as unique");

    const auto persistent_detail =
        old_school::probe_runner::make_value_probe_decision_detail(
            label, deterministic_tie, &uniform_detail,
            &deterministic_detail);
    expect(
        persistent_detail.selection_changed_from_reference &&
            !persistent_detail.selection_changed_from_previous,
        "persistent G0 disagreement was mistaken for an adjacent "
        "selection change");
}

void test_low_margin_summary_is_actionable() {
    const auto label = old_school::probe_eval::make_probe_label(
        "green.low-margin", DeckId::Green,
        {
            {"best", {0.60, 0.60, 0.60, 0.60}},
            {"near", {0.58, 0.58, 0.58, 0.58}},
            {"far", {0.20, 0.20, 0.20, 0.20}},
        });
    const auto summary =
        old_school::probe_runner::summarize_low_margin_best_pairs(
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

void test_candidate_pair_estimate_reorients_canonical_pair() {
    const auto label = old_school::probe_eval::make_probe_label(
        "ru.synthetic-reversed-pair", DeckId::RUAggro,
        {
            {"x-zero", {0.10, 0.20, 0.30, 0.40}},
            {"pass", {0.80, 0.80, 0.80, 0.80}},
        });
    expect(
        label.pairs.size() == 1 &&
            label.pairs.front().first == "x-zero" &&
            label.pairs.front().second == "pass" &&
            label.pairs.front().delta_q < 0.0,
        "synthetic fixture did not force reverse canonical order");
    const auto estimate =
        old_school::probe_runner::make_candidate_pair_estimate(
            label, "Q(Pass) - Q(X=0)", "pass", "x-zero");
    expect(
        estimate.first_key == "pass" &&
            estimate.second_key == "x-zero" &&
            std::abs(estimate.delta_q - 0.55) < 1.0e-12 &&
            estimate.delta_q == -label.pairs.front().delta_q &&
            estimate.paired_standard_error ==
                label.pairs.front().paired_standard_error,
        "candidate-pair helper did not orient Q(first)-Q(second)");
    const double radius =
        old_school::probe_eval::kNormal95CriticalValue *
        estimate.paired_standard_error;
    expect(
        std::abs(
            estimate.confidence_lower_95 -
            (estimate.delta_q - radius)) < 1.0e-12 &&
            std::abs(
                estimate.confidence_upper_95 -
                (estimate.delta_q + radius)) < 1.0e-12,
        "reoriented pair CI was not centered on its delta");
}

void test_report_contains_required_schema_and_caveats() {
    ProbeScoreReport report;
    report.metadata = {
        .schema =
            std::string(old_school::probe_runner::kProbeCacheSchema),
        .algorithm =
            std::string(old_school::probe_runner::
                            kProbeReferenceAlgorithm),
        .semantic_revision =
            std::string(old_school::probe_runner::
                            kProbeSemanticRevision),
        .environment_revision =
            std::string(old_school::probe_runner::
                            kProbeEnvironmentRevision),
        .corpus_id =
            std::string(old_school::probes::kProbeDevV3),
        .reference_seed =
            old_school::probe_runner::kProbeReferenceSeed,
        .production_policy_seed =
            old_school::probe_runner::kProbeProductionPolicySeed,
        .training_seed = 424242,
        .training_games = 800,
        .worlds = 128,
        .horizon_turns = 12,
        .rollouts_per_world = 1,
        .probe_count = 20,
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
    old_school::probe_eval::ProbeMetricSummary metrics;
    metrics.probe_count = 20;
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
    old_school::probe_eval::CandidateQFitSummary q_fit;
    q_fit.candidate_count = 40;
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
        .probe_count = 20,
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
        old_school::probe_runner::format_probe_score_report(report);
    expect(output.find("actor-model-fingerprint") !=
                   std::string::npos &&
               output.find("actor-candidate-fingerprint") !=
                   std::string::npos,
           "report did not distinguish reference and scoring Actor models");
    expect(output.find(
               "diagnostic only, 4 positions each for "
               "Green/Red/Blue/White/RU Aggro") !=
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
    expect(output.find("Rules environment: " +
                       std::string(old_school::probe_runner::
                                       kProbeEnvironmentRevision)) !=
               std::string::npos,
           "report omitted its rules-environment identity");
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
            std::string(old_school::probe_runner::kProbeCacheSchema),
        .algorithm =
            std::string(old_school::probe_runner::
                            kProbeReferenceAlgorithm),
        .semantic_revision =
            std::string(old_school::probe_runner::
                            kProbeSemanticRevision),
        .environment_revision =
            std::string(old_school::probe_runner::
                            kProbeEnvironmentRevision),
        .corpus_id =
            std::string(old_school::probes::kProbeDevV3),
        .reference_seed =
            old_school::probe_runner::kProbeReferenceSeed,
        .production_policy_seed =
            old_school::probe_runner::kProbeProductionPolicySeed,
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
    old_school::probe_eval::ProbeMetricSummary metrics;
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
            .transition_parent_name = "Value G0",
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
            .transition_parent_name = "Value G1",
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
        old_school::probe_runner::format_probe_score_report(report);
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
        old_school::train_learned_actor_model(1, 0xA11CEULL);
    const auto candidate =
        old_school::train_learned_actor_model(1, 0xB0BULL);
    expect(old_school::learned_model_fingerprint(reference) !=
               old_school::learned_model_fingerprint(candidate),
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
        old_school::probe_runner::score_probe_dev_with_models(
            config, progress, reference, reference, "Actor G0");
    const ProbeScoreReport loaded =
        old_school::probe_runner::score_probe_dev_with_models(
            config, progress, reference, candidate, "Actor G1");

    expect(generated.cache_status == ProbeCacheStatus::Generated &&
               loaded.cache_status == ProbeCacheStatus::Loaded,
           "candidate-only change regenerated reference-owned labels");
    expect(generated.metadata == loaded.metadata &&
               generated.metadata.reference_model_fingerprint ==
                   old_school::learned_model_fingerprint(reference),
           "candidate-only change altered reference cache identity");
    expect(generated.scoring_actor_model_fingerprint ==
                   generated.metadata.reference_model_fingerprint &&
               loaded.scoring_actor_model_fingerprint ==
                   old_school::learned_model_fingerprint(candidate),
           "report did not bind the actual scoring Actor");
    expect(generated.policies.front().name == "Actor G0 raw head" &&
               loaded.policies.front().name == "Actor G1 raw head",
           "candidate policy labels were not generation-specific");
    expect(generated.hidden_repartition.passed &&
               loaded.hidden_repartition.passed,
           "candidate scoring did not enforce hidden repartition");

    const std::string null_error = expect_invalid(
        [&] {
            old_school::probe_runner::score_probe_dev_with_models(
                config, progress, nullptr, candidate, "Actor G1");
        },
        "null reference Actor was accepted");
    expect(null_error.find("reference and scoring") !=
               std::string::npos,
           "null model error was not actionable");
    const std::string name_error = expect_invalid(
        [&] {
            old_school::probe_runner::score_probe_dev_with_models(
                config, progress, reference, candidate,
                "Actor\tG1");
        },
        "unsafe candidate name was accepted");
    expect(name_error.find("contain no tabs/newlines") !=
               std::string::npos,
           "unsafe candidate name error was not actionable");

    const auto reference_value =
        old_school::train_learned_value_champion(
            1, 0xA11CEULL);
    const auto scoring_value =
        old_school::train_learned_value_champion(
            1, 0xB0BULL);
    expect(
        old_school::learned_model_fingerprint(reference_value) !=
            old_school::learned_model_fingerprint(scoring_value),
        "tiny Value candidate unexpectedly aliases reference content");
    const ProbeScoreReport value_loaded =
        old_school::probe_runner::score_probe_dev_with_candidates(
            config, progress,
            {
                .reference_actor_model = reference,
                .scoring_actor_model = reference,
                .scoring_actor_name = "Actor G0",
                .reference_value_model = reference_value,
                .reference_value_name = "Value G0",
                .scoring_value_models = {
                    {"Value G8", scoring_value, "", 0.10},
                },
            });
    expect(
        value_loaded.cache_status == ProbeCacheStatus::Loaded &&
            value_loaded.metadata == generated.metadata,
        "Value candidate changed Actor-owned cache identity");
    expect(
        value_loaded.value_model_fingerprint ==
                old_school::learned_model_fingerprint(reference_value) &&
            value_loaded.scoring_value_model_fingerprint ==
                old_school::learned_model_fingerprint(scoring_value),
        "report did not bind reference and scoring Value models");
    expect(
        value_loaded.policies.size() == 6 &&
            value_loaded.hidden_repartition.policy_count == 6 &&
            value_loaded.hidden_repartition.passed,
        "distinct Value candidate was not hidden-invariant sixth view");
    expect(
        value_loaded.metadata.probe_count == 20 &&
            value_loaded.force_spike_controls.size() == 2 &&
            value_loaded.force_spike_controls[0].policy_name ==
                "Value G0" &&
            value_loaded.force_spike_controls[1].policy_name ==
                "Value G8" &&
            value_loaded.force_spike_controls[0]
                    .value_priority_residual_weight == 0.0 &&
            value_loaded.force_spike_controls[1]
                    .value_priority_residual_weight == 0.10,
        "supplemental Force Spike controls changed balanced "
        "denominators or lost G0/candidate order");
    expect(
        value_loaded.policies[2].name ==
                "Value G0 deployed policy" &&
            value_loaded.policies[3].name ==
                "Value G8 deployed policy" &&
            value_loaded.policies[2].configuration.find(
                "Value Priority residual weight") ==
                std::string::npos &&
            value_loaded.policies[3].configuration.find(
                "Value Priority residual weight=0.1") !=
                std::string::npos &&
            value_loaded.policies.back().name ==
                "Value G0-continuation deep cross-check",
        "Value candidate/reference policy rows were mislabeled");
    const std::string value_output =
        old_school::probe_runner::format_probe_score_report(
            value_loaded);
    expect(
        value_output.find("Reference Value model fingerprint") !=
                std::string::npos &&
            value_output.find("Scoring Value model fingerprint") !=
                std::string::npos &&
            value_output.find("6 policy views") !=
                std::string::npos &&
            value_output.find(
                "Value Priority residual weight=0.1000") !=
                std::string::npos,
        "formatted report did not distinguish the Value candidate");

    const auto dev_corpus =
        old_school::probes::make_probe_dev_v3();
    const auto force_spike_probe = std::find_if(
        dev_corpus.begin(), dev_corpus.end(),
        [](const DecisionProbe& probe) {
            return probe.stable_id ==
                   "blue.force-spike-tapped-out-gray-ogre.v3";
        });
    expect(force_spike_probe != dev_corpus.end(),
           "contextual critic test lost its priority fixture");
    const old_school::LearnedDecisionContext live_context{
        .valid = true,
        .phase = force_spike_probe->phase,
        .decision_player = force_spike_probe->root_player,
        .consecutive_passes =
            force_spike_probe->consecutive_passes,
        .sorcery_actions = true,
    };
    old_school::LearnedDecisionContext alternate_context =
        live_context;
    alternate_context.consecutive_passes = 0;
    auto contextual_value =
        old_school::with_learned_decision_context(
            scoring_value);
    std::vector<
        old_school::LearnedContextualCriticTrainingExample>
        context_examples;
    context_examples.reserve(128);
    for (std::size_t repeat = 0; repeat < 64; ++repeat) {
        context_examples.push_back({
            .features = old_school::learned_observation(
                force_spike_probe->state,
                force_spike_probe->root_player),
            .context_features =
                old_school::learned_decision_context_features(
                    live_context,
                    force_spike_probe->root_player),
            .target = 0.95,
        });
        context_examples.push_back({
            .features = old_school::learned_observation(
                force_spike_probe->state,
                force_spike_probe->root_player),
            .context_features =
                old_school::learned_decision_context_features(
                    alternate_context,
                    force_spike_probe->root_player),
            .target = 0.05,
        });
    }
    contextual_value =
        old_school::update_learned_contextual_value_model(
            contextual_value, context_examples,
            {
                .epochs = 20,
                .learning_rate = 0.02,
                .root_seed = 0xC07E57ULL,
                .member_training_tag = 0xC017E000ULL,
            });
    const double expected_contextual_critic =
        old_school::learned_contextual_critic_value(
            force_spike_probe->state,
            force_spike_probe->root_player,
            live_context, contextual_value);
    expect(
        std::abs(
            expected_contextual_critic -
            old_school::learned_critic_value(
                force_spike_probe->state,
                force_spike_probe->root_player,
                contextual_value)) > 1.0e-3,
        "contextual test model did not separate the live priority root");

    const ProbeScoreReport contextual_loaded =
        old_school::probe_runner::score_probe_dev_with_candidates(
            config, progress,
            {
                .reference_actor_model = reference,
                .scoring_actor_model = reference,
                .scoring_actor_name = "Actor G0",
                .reference_value_model = reference_value,
                .reference_value_name = "Value G0",
                .scoring_value_models = {
                    {"Value State Control", scoring_value,
                     "context-test"},
                    {"Value Context Test", contextual_value,
                     "context-test"},
                },
            });
    const auto context_checkpoint = std::find_if(
        contextual_loaded.value_checkpoints.begin(),
        contextual_loaded.value_checkpoints.end(),
        [](const auto& checkpoint) {
            return checkpoint.name == "Value Context Test";
        });
    expect(
        context_checkpoint !=
            contextual_loaded.value_checkpoints.end(),
        "contextual candidate was omitted from checkpoint detail");
    const auto context_decision = std::find_if(
        context_checkpoint->decisions.begin(),
        context_checkpoint->decisions.end(),
        [&](const auto& decision) {
            return decision.stable_id ==
                   force_spike_probe->stable_id;
        });
    expect(
        context_decision != context_checkpoint->decisions.end() &&
            context_decision->critic_prediction ==
                expected_contextual_critic,
        "probe metrics evaluated a contextual priority critic "
        "without its live phase/priority/pass context");

    const auto second_scoring_value =
        old_school::train_learned_value_champion(
            1, 0xC0DEULL);
    const auto third_scoring_value =
        old_school::train_learned_value_champion(
            1, 0xD00DULL);
    const ProbeScoreReport checkpoint_loaded =
        old_school::probe_runner::score_probe_dev_with_candidates(
            config, progress,
            {
                .reference_actor_model = reference,
                .scoring_actor_model = reference,
                .scoring_actor_name = "Actor G0",
                .reference_value_model = reference_value,
                .reference_value_name = "Value G0",
                .scoring_value_models = {
                    {"Value Mix50 base", scoring_value, "mix50"},
                    {"Value Mix50 G1", second_scoring_value,
                     "mix50"},
                    {"Value Mix50 G2", third_scoring_value,
                     "mix50"},
                    {"Value Mix50 G3", scoring_value, "mix50"},
                    {"Value Mix50 G4", second_scoring_value,
                     "mix50"},
                    {"Value Mix50 G5", third_scoring_value,
                     "mix50"},
                    {"Value Mix50 G6", scoring_value, "mix50"},
                    {"Value Mix50 G7", second_scoring_value,
                     "mix50"},
                    {"Value Mix50 G8", third_scoring_value,
                     "mix50"},
                    {"Value Challenger C16", scoring_value,
                     "challenger-c16", 0.10},
                },
            });
    expect(
        checkpoint_loaded.cache_status == ProbeCacheStatus::Loaded &&
            checkpoint_loaded.metadata == generated.metadata &&
            checkpoint_loaded.metadata.reference_model_fingerprint ==
                old_school::learned_model_fingerprint(reference),
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
            checkpoint_loaded.hidden_repartition.policy_count == 15,
        "multi-checkpoint scoring did not keep five full views and "
        "verify every compact row");
    expect(
        checkpoint_loaded.value_checkpoints.size() == 11 &&
            checkpoint_loaded.value_checkpoints[0].name ==
                "Value G0" &&
            checkpoint_loaded.value_checkpoints[1].name ==
                "Value Mix50 base" &&
            checkpoint_loaded.value_checkpoints[2].name ==
                "Value Mix50 G1" &&
            checkpoint_loaded.value_checkpoints[9].name ==
                "Value Mix50 G8" &&
            checkpoint_loaded.value_checkpoints[10].name ==
                "Value Challenger C16" &&
            checkpoint_loaded.value_checkpoints[1]
                    .transition_parent_name == "Value G0" &&
            checkpoint_loaded.value_checkpoints[2]
                    .transition_parent_name ==
                "Value Mix50 base" &&
            checkpoint_loaded.value_checkpoints[9]
                    .transition_parent_name ==
                "Value Mix50 G7" &&
            checkpoint_loaded.value_checkpoints[10]
                    .transition_parent_name == "Value G0" &&
            checkpoint_loaded.value_checkpoints[0]
                    .value_priority_residual_weight == 0.0 &&
            checkpoint_loaded.value_checkpoints[10]
                    .value_priority_residual_weight == 0.10,
        "Value checkpoint rows did not preserve exact caller order");
    const std::array<std::string, 11> expected_fingerprints{
        old_school::learned_model_fingerprint(reference_value),
        old_school::learned_model_fingerprint(scoring_value),
        old_school::learned_model_fingerprint(second_scoring_value),
        old_school::learned_model_fingerprint(third_scoring_value),
        old_school::learned_model_fingerprint(scoring_value),
        old_school::learned_model_fingerprint(second_scoring_value),
        old_school::learned_model_fingerprint(third_scoring_value),
        old_school::learned_model_fingerprint(scoring_value),
        old_school::learned_model_fingerprint(second_scoring_value),
        old_school::learned_model_fingerprint(third_scoring_value),
        old_school::learned_model_fingerprint(scoring_value),
    };
    for (std::size_t checkpoint = 0;
         checkpoint < checkpoint_loaded.value_checkpoints.size();
         ++checkpoint) {
        const auto& row =
            checkpoint_loaded.value_checkpoints[checkpoint];
        expect(row.fingerprint == expected_fingerprints[checkpoint],
               "Value checkpoint fingerprint order changed");
        expect(row.decisions.size() == 20,
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
        old_school::probe_runner::format_probe_score_report(
            checkpoint_loaded);
    expect(
        checkpoint_output.find(
            "Value checkpoint transitions (compact)") !=
            std::string::npos,
        "formatted checkpoint report omitted its heading");
    const std::array<std::string_view, 11> expected_names{
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
        "Value Challenger C16",
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
        checkpoint_output.find(
            "Value Challenger C16: fingerprint ") !=
                std::string::npos &&
            checkpoint_output.find(
                "Value Priority residual weight=0.1000") !=
                std::string::npos &&
            checkpoint_output.find(
                "transition parent Value G0") !=
                std::string::npos &&
            checkpoint_output.find(
                "Value Mix50 G8 -> Value Challenger C16") ==
                std::string::npos,
        "standalone challenger inherited a cross-family transition");
    expect(
        old_school::probe_runner::format_probe_score_report(generated)
                .find("Value checkpoint transitions (compact)") ==
            std::string::npos,
        "legacy five-view output gained a checkpoint section");
}

void test_validation_scoring_reports_pass_x_zero_pair() {
    TemporaryDirectory directory;
    const auto actor =
        old_school::train_learned_actor_model(
            1, 0x5156414CULL);
    const auto value =
        old_school::train_learned_value_champion(
            1, 0x5156414CULL);
    const auto candidate_a =
        old_school::train_learned_value_champion(
            1, 0x5156414DULL);
    const auto candidate_b =
        old_school::train_learned_value_champion(
            1, 0x5156414EULL);
    const ProbeScoreConfig config{
        .training_games = 1,
        .training_seed = 0x5156414CULL,
        .reference_worlds = 2,
        .reference_horizon_turns = 0,
        .reference_rollouts_per_world = 1,
        .scoring_value_worlds = 2,
        .cache_path =
            directory.path() / "validation-v1.labels.tsv",
        .refresh_cache = false,
    };
    std::ostringstream progress;
    const auto models = old_school::probe_runner::ProbeScoringModels{
        .reference_actor_model = actor,
        .scoring_actor_model = actor,
        .scoring_actor_name = "Actor validation",
        .reference_value_model = value,
        .reference_value_name = "Value validation",
        .scoring_value_models = {
            {"Synthetic Value A", candidate_a, ""},
            {"Synthetic Value B", candidate_b, ""},
        },
    };
    const ProbeScoreReport generated =
        old_school::probe_runner::
            score_probe_corpus_with_candidates(
                ProbeCorpusKind::ValidationV1, config, progress,
                models);
    const ProbeScoreReport loaded =
        old_school::probe_runner::
            score_probe_corpus_with_candidates(
                ProbeCorpusKind::ValidationV1, config, progress,
                models);

    expect(
        generated.cache_status == ProbeCacheStatus::Generated &&
            loaded.cache_status == ProbeCacheStatus::Loaded &&
            generated.metadata == loaded.metadata,
        "validation scorer did not reuse its reference-owned cache");
    expect(
        generated.corpus_kind ==
                ProbeCorpusKind::ValidationV1 &&
            !generated.promotion_eligible &&
            generated.metadata.schema ==
                old_school::probe_runner::
                    kProbeValidationCacheSchema &&
            generated.metadata.corpus_id ==
                old_school::probes::kProbeValidationV1 &&
            generated.cache_path == config.cache_path,
        "validation report lost its non-promotion corpus identity");
    expect(
        generated.hidden_repartition.passed &&
            generated.hidden_repartition.probe_count == 1 &&
            generated.reference_samples_per_candidate == 2,
        "validation scoring did not use hidden-safe common worlds");
    expect(
        generated.candidate_pairs.size() == 1 &&
            loaded.candidate_pairs.size() == 1,
        "validation report omitted its Actor-reference pair");
    expect(
        generated.value_candidate_pairs.size() == 3 &&
            loaded.value_candidate_pairs.size() == 3,
        "validation report omitted a Value policy pair");

    const auto& estimate = generated.candidate_pairs.front();
    const auto corpus =
        old_school::probes::make_probe_validation_v1();
    const auto& probe = corpus.front();
    std::string pass_key;
    std::string x_zero_key;
    for (const auto& candidate : probe.candidates) {
        const auto* action =
            std::get_if<old_school::PriorityAction>(
                &candidate.action);
        if (action == nullptr) {
            continue;
        }
        if (action->kind ==
            old_school::PriorityActionKind::Pass) {
            pass_key = candidate.descriptor;
        }
        if (action->kind ==
                old_school::PriorityActionKind::CastDisintegrate &&
            action->x_value == 0 &&
            action->target.has_value() &&
            !action->target->creature.has_value() &&
            action->target->player == 1 - probe.root_player) {
            x_zero_key = candidate.descriptor;
        }
    }
    expect(
        !pass_key.empty() && !x_zero_key.empty() &&
            estimate.name ==
                "Actor reference Q(Pass) - Q(X=0)" &&
            estimate.first_key == pass_key &&
            estimate.second_key == x_zero_key &&
            estimate.stable_id == probe.stable_id &&
            estimate.root_deck == DeckId::RUAggro &&
            estimate.samples_per_candidate == 2,
        "focused pair is not explicitly oriented Pass minus X=0");
    const auto labels =
        old_school::probe_runner::load_probe_label_cache(
            ProbeCorpusKind::ValidationV1, config.cache_path,
            generated.metadata, corpus);
    const auto& label = labels.front();
    const auto pair = std::find_if(
        label.pairs.begin(), label.pairs.end(),
        [&pass_key, &x_zero_key](const auto& candidate_pair) {
            return (candidate_pair.first == pass_key &&
                    candidate_pair.second == x_zero_key) ||
                   (candidate_pair.first == x_zero_key &&
                    candidate_pair.second == pass_key);
        });
    expect(pair != label.pairs.end(),
           "cache label omitted the Pass-versus-X=0 pair");
    const double expected_delta =
        pair->first == pass_key ? pair->delta_q : -pair->delta_q;
    expect(
        estimate.delta_q == expected_delta &&
            estimate.paired_standard_error ==
                pair->paired_standard_error,
        "focused estimate did not preserve the oriented cached pair");
    const double radius =
        old_school::probe_eval::kNormal95CriticalValue *
        estimate.paired_standard_error;
    expect(
        std::abs(
            estimate.confidence_lower_95 -
            (estimate.delta_q - radius)) < 1.0e-12 &&
            std::abs(
                estimate.confidence_upper_95 -
                (estimate.delta_q + radius)) < 1.0e-12,
        "focused pair confidence interval is not paired 95%");
    expect(
        estimate.delta_q ==
                loaded.candidate_pairs.front().delta_q &&
            estimate.paired_standard_error ==
                loaded.candidate_pairs.front()
                    .paired_standard_error,
        "focused pair changed after cache reload");
    expect(
        generated.value_candidate_pairs[0].name ==
                "Value validation Q(Pass) - Q(X=0)" &&
            generated.value_candidate_pairs[1].name ==
                "Synthetic Value A Q(Pass) - Q(X=0)" &&
            generated.value_candidate_pairs[2].name ==
                "Synthetic Value B Q(Pass) - Q(X=0)" &&
            generated.value_candidate_pairs[0]
                    .samples_per_candidate == 2 &&
            generated.value_candidate_pairs[1]
                    .samples_per_candidate == 2 &&
            generated.value_candidate_pairs[2]
                    .samples_per_candidate == 2,
        "Value pair rows lost caller order or K=2 accounting");
    expect(
        generated.value_candidate_pairs[1].delta_q !=
            generated.value_candidate_pairs[2].delta_q,
        "two distinct synthetic Value policies produced an "
        "indistinguishable focused pair");

    ProbeScoreConfig wider = config;
    wider.scoring_value_worlds = 3;
    const ProbeScoreReport wider_scoring =
        old_school::probe_runner::
            score_probe_corpus_with_candidates(
                ProbeCorpusKind::ValidationV1, wider, progress,
                models);
    expect(
        wider_scoring.cache_status == ProbeCacheStatus::Loaded &&
            wider_scoring.metadata == generated.metadata &&
            wider_scoring.candidate_pairs.front().delta_q ==
                generated.candidate_pairs.front().delta_q &&
            wider_scoring.candidate_pairs.front()
                    .paired_standard_error ==
                generated.candidate_pairs.front()
                    .paired_standard_error,
        "candidate K changed or regenerated the Actor-owned labels");
    expect(
        wider_scoring.value_candidate_pairs.size() == 3 &&
            std::all_of(
                wider_scoring.value_candidate_pairs.begin(),
                wider_scoring.value_candidate_pairs.end(),
                [](const auto& pair) {
                    return pair.samples_per_candidate == 3;
                }),
        "K=3 did not change Value candidate sample accounting");

    ProbeScoreConfig exploratory = config;
    exploratory.scoring_value_continuation_epsilon = 1.0;
    const ProbeScoreReport exploratory_first =
        old_school::probe_runner::
            score_probe_corpus_with_candidates(
                ProbeCorpusKind::ValidationV1, exploratory,
                progress, models);
    const ProbeScoreReport exploratory_repeated =
        old_school::probe_runner::
            score_probe_corpus_with_candidates(
                ProbeCorpusKind::ValidationV1, exploratory,
                progress, models);
    expect(
        exploratory_first.cache_status ==
                ProbeCacheStatus::Loaded &&
            exploratory_repeated.cache_status ==
                ProbeCacheStatus::Loaded &&
            exploratory_first.metadata == generated.metadata &&
            exploratory_repeated.metadata == generated.metadata,
        "continuation epsilon changed or regenerated the "
        "Actor-owned label cache");
    expect(
        exploratory_first.hidden_repartition.passed &&
            exploratory_repeated.hidden_repartition.passed,
        "continuation epsilon broke hidden-zone repartition "
        "invariance");
    expect(
        old_school::probe_runner::format_probe_score_report(
            exploratory_first) ==
            old_school::probe_runner::format_probe_score_report(
                exploratory_repeated),
        "nonzero continuation epsilon scoring was not deterministic");
    const std::string exploratory_output =
        old_school::probe_runner::format_probe_score_report(
            exploratory_first);
    expect(
        exploratory_output.find("continuation epsilon=1") !=
            std::string::npos,
        "nonzero continuation epsilon was omitted from probe policy "
        "configuration");

    const std::string output =
        old_school::probe_runner::format_probe_score_report(
            wider_scoring);
    expect(
        output.find("Probe Validation-v1 Offline Score") !=
                std::string::npos &&
            output.find("cannot be used for policy promotion") !=
                std::string::npos &&
            output.find(
                "Focused cached Actor-reference candidate pairs") !=
                std::string::npos &&
            output.find("Focused Value-policy candidate pairs") !=
                std::string::npos &&
            output.find(
                "Synthetic Value A Q(Pass) - Q(X=0)") !=
                std::string::npos &&
            output.find("K=3") !=
                std::string::npos &&
            output.find("paired SE") != std::string::npos &&
            output.find("95% CI") != std::string::npos &&
            output.find("Probe Dev-v3 Offline Score") ==
                std::string::npos,
        "validation report omitted pair uncertainty or promotion caveat");
}

void test_validation_corpus_identity_is_golden() {
    const auto corpus =
        old_school::probes::make_probe_validation_v1();
    expect(
        old_school::probe_runner::
                corpus_information_set_fingerprint(
                    ProbeCorpusKind::ValidationV1, corpus) ==
            "9bb67ff6e8b476b2",
        "validation-v1 information-set fingerprint drifted");
    expect(corpus.size() == 1 &&
               corpus.front().harvest.has_value(),
           "validation-v1 golden probe lost harvest provenance");
    const auto& harvest = *corpus.front().harvest;
    expect(
        harvest.turn_number == 5 &&
            harvest.priority_decision_ordinal == 10,
        "validation-v1 harvest turn/decision ordinal drifted");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run("stable seed and corpus fingerprint",
               test_seed_and_fingerprint_ignore_iteration_order);
    runner.run("Old School public-state fingerprint",
               test_old_school_fingerprint_covers_new_public_state);
    runner.run("candidate descriptor mapping",
               test_candidate_mapping_is_descriptor_safe);
    runner.run("reference resource bounds",
               test_reference_resource_bounds_reject_early);
    runner.run("cache roundtrip and stale rejection",
               test_cache_roundtrip_and_stale_rejection);
    runner.run("validation cache identity separation",
               test_validation_cache_identity_is_fail_closed);
    runner.run("hidden clone information set",
               test_hidden_clone_preserves_information_set);
    runner.run("tiny hidden-safe reference",
               test_tiny_reference_is_hidden_clone_invariant);
    runner.run("Priority evaluation thread identity",
               test_priority_evaluation_threads_are_bit_identical);
    runner.run("deployed Value attack seed independence",
               test_value_attack_probe_scores_are_seed_independent);
    runner.run("Force Spike control gate and report",
               test_force_spike_control_gate_and_report_semantics);
    runner.run("Force Spike deployed Value scorer",
               test_force_spike_control_scorer_uses_deployed_value_path);
    runner.run("Force Spike trained Value residual scorer",
               test_force_spike_residual_uses_trained_priority_head);
    runner.run("teacher audit ordered K8 blocks",
               test_teacher_audit_ordered_blocks_are_exact);
    runner.run("teacher audit generic hidden-safe scorer",
               test_teacher_sufficiency_audit_is_generic_and_hidden_safe);
    runner.run("terminal-credit focused fail-closed gate",
               test_terminal_credit_gate_is_focused_and_fail_closed);
    runner.run("Value selection detail semantics",
               test_value_decision_detail_respects_ties_and_selectors);
    runner.run("actionable low-margin summary",
               test_low_margin_summary_is_actionable);
    runner.run("oriented candidate-pair estimate",
               test_candidate_pair_estimate_reorients_canonical_pair);
    runner.run("report summary schema",
               test_report_contains_required_schema_and_caveats);
    runner.run("compact checkpoint attribution",
               test_compact_checkpoint_report_shows_actionable_transitions);
    runner.run("candidate cache ownership",
               test_candidate_scoring_reuses_reference_owned_cache);
    runner.run("validation Pass-vs-X0 scoring",
               test_validation_scoring_reports_pass_x_zero_pair);
    runner.run("validation golden corpus identity",
               test_validation_corpus_identity_is_golden);
    return runner.finish();
}
