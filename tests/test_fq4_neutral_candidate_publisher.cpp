#include "old_school/fq4_neutral_candidate_publisher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace artifact =
    old_school::fq4_dev_candidate_artifact;
namespace bundle = old_school::fq4_dev_bundle;
namespace dev = old_school::fq4_dev_evaluator;
namespace evaluator =
    old_school::fq4_neutral_evaluator;
namespace neutral =
    old_school::fq4_neutral_supplement;
namespace publisher =
    old_school::fq4_neutral_candidate_publisher;

class TestRunner {
  public:
    void run(
        std::string_view name,
        const std::function<void()>& test) {
        try {
            test();
            ++passed_;
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr
                << "FAIL " << name << ": "
                << error.what() << '\n';
        }
    }

    int finish() const {
        if (failed_ != 0) {
            std::cerr
                << failed_ << " test(s) failed; "
                << passed_ << " passed\n";
            return 1;
        }
        std::cout
            << passed_
            << " FQ4 anchored candidate publisher tests passed\n";
        return 0;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_throws_contains(
    Function&& function, std::string_view expected) {
    try {
        std::forward<Function>(function)();
    } catch (const std::exception& error) {
        if (std::string_view(error.what()).find(expected) !=
            std::string_view::npos) {
            return;
        }
        throw std::runtime_error(
            "exception did not contain '" +
            std::string(expected) + "': " +
            error.what());
    }
    throw std::runtime_error(
        "operation did not throw '" +
        std::string(expected) + "'");
}

std::string digest(char character) {
    return std::string(64, character);
}

artifact::Contract literal_production_contract() {
    return {
        .family = "FQ4-DEV5-NEUTRAL-ANCHORED",
        .environment =
            "old-school-environment-v3-cleanup-discard;"
            "dev1-bytes=2250909;"
            "dev1-sha256="
            "0911fc2eb8b14ddc9165543eb1e4c4ed"
            "b0b058256a58dedf61f6c4ea4ca859df;"
            "neutral-bytes=661475;"
            "neutral-sha256="
            "47d94823f043971f6f9f0aa5f552bfae"
            "210af9615d8f6dc7392e52dad3eaa105",
        .parent = {
            .artifact_bytes = 3'111'437,
            .artifact_sha256 =
                "53aeb904bd87311b37201859317f05ab0"
                "66bdfe134c72460cf94bff6d1f944ca",
            .model_fingerprint =
                "68126afc5a3e3757eb1d510a056585aa9"
                "74c4f54ce1b4a789ff430f1c7413e2f",
            .components = {
                .critic =
                    "2982b155a02a4a2a3ce8442ae28f6d8c"
                    "f7829103e538c60f0625b3332502e568",
                .priority =
                    "32dc6688a5c970e3eda4325bea5ee4190"
                    "77027e160697899e3b00c963fa1bb22",
                .attack =
                    "dfd3aaa16755bee5d0c2c40956851b94"
                    "ef5676a271a602eb23a57719f7358b01",
                .block =
                    "d64e40796bd1587958b7386996e6a1e5"
                    "660778d40ec7b40b0ee6324b8e39adbb",
                .damage_order =
                    "f0a84ed549bbf95197dd00c13ab04c0a"
                    "4f6b1771f14bdb30a7dca937d2d79c76",
            },
            .training_games = 800,
            .training_seed = 424242,
            .generation = 16,
        },
        .corpus = {
            .artifact_bytes = 2'250'909,
            .artifact_sha256 =
                "0911fc2eb8b14ddc9165543eb1e4c4ed"
                "b0b058256a58dedf61f6c4ea4ca859df",
        },
        .fit = {
            .input_sha256 =
                "a13c2bca589a42d020fcb7abfa1826fa"
                "e5a9745be41602442fa7e7bc1d768fef",
            .examples = 248,
            .options = 987,
            .check_examples = 0,
            .background_only_examples = 0,
            .optimizer_calls = 1,
            .optimizer = {
                .batch_size = 64,
                .epochs = 16,
                .learning_rate = 0.001,
                .beta1 = 0.9,
                .beta2 = 0.999,
                .epsilon = 1.0e-8,
                .global_gradient_norm_clip = 5.0,
                .seed = 202607280212ULL,
                .residual_weight = 0.10,
                .policy_temperature = 0.10,
            },
        },
        .candidate_model_fingerprint =
            "22834a951e8338568be93561a34c6b1df"
            "588faa71feb9d184ab62021b03b2171",
        .priority_hidden_count = 32,
        .priority_feature_count = 893,
        .priority_parameter_count = 29'534,
        .deployment = {
            .variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .training_games = 800,
            .worlds_per_action = 8,
            .horizon_turns = 4,
            .rollouts_per_world = 1,
            .root_search_depth = 1,
            .shallow_prior = true,
            .root_exploration = 0.0,
            .continuation_epsilon = 0.0,
            .priority_residual_weight = 0.10,
            .pass_dominance = false,
            .continuation_controller =
                old_school::
                    LearnedContinuationController::Legacy,
            .max_turns = 500,
        },
    };
}

old_school::artifact_integrity::RegularFileSnapshot
snapshot(
    const std::filesystem::path& path,
    std::uint64_t bytes, const std::string& sha256) {
    return {
        .path = path.string(),
        .physical_path = path.string(),
        .byte_size = bytes,
        .sha256 = sha256,
        .device = 7,
        .inode = 11,
        .link_count = 1,
        .modification_seconds = 13,
        .modification_nanoseconds = 17,
        .change_seconds = 19,
        .change_nanoseconds = 23,
    };
}

std::uint64_t parameter_count(
    const old_school::LearnedPriorityHeadParameters&
        parameters) {
    return
        static_cast<std::uint64_t>(
            parameters.input_hidden.size()) *
            static_cast<std::uint64_t>(
                parameters.direct.size()) +
        static_cast<std::uint64_t>(
            parameters.hidden_bias.size()) +
        static_cast<std::uint64_t>(
            parameters.hidden_output.size()) +
        static_cast<std::uint64_t>(
            parameters.direct.size()) +
        1U;
}

struct SyntheticModels {
    std::shared_ptr<const old_school::LearnedModel> parent;
    std::shared_ptr<const old_school::LearnedModel> positive;
    std::shared_ptr<const old_school::LearnedModel> anchored;
    std::shared_ptr<const old_school::LearnedModel> alternate;
    std::shared_ptr<const old_school::LearnedModel>
        signed_zero_alternate;
};

const SyntheticModels& synthetic_models() {
    static const SyntheticModels models = [] {
        const auto parent =
            old_school::train_learned_value_challenger(
                1, 0xF405ULL, 1);
        auto parameters =
            old_school::learned_priority_head_parameters(
                parent);
        parameters.output_bias += 0.03125;
        const auto positive =
            old_school::
                with_learned_priority_head_parameters(
                    parent, parameters);
        parameters.output_bias += 0.0625;
        parameters.direct.at(0) = 0.0;
        const auto anchored =
            old_school::
                with_learned_priority_head_parameters(
                    parent, parameters);
        parameters.direct.at(0) = -0.0;
        const auto signed_zero_alternate =
            old_school::
                with_learned_priority_head_parameters(
                    parent, parameters);
        parameters.direct.at(0) = 0.0;
        parameters.output_bias += 0.125;
        const auto alternate =
            old_school::
                with_learned_priority_head_parameters(
                    parent, parameters);
        return SyntheticModels{
            .parent = parent,
            .positive = positive,
            .anchored = anchored,
            .alternate = alternate,
            .signed_zero_alternate =
                signed_zero_alternate,
        };
    }();
    return models;
}

artifact::Contract synthetic_contract() {
    const auto& models = synthetic_models();
    const auto parent_parameters =
        old_school::learned_priority_head_parameters(
            models.parent);
    return {
        .family = "synthetic-dev5",
        .environment = "synthetic-neutral-environment",
        .parent = {
            .artifact_bytes = 101,
            .artifact_sha256 = digest('a'),
            .model_fingerprint =
                old_school::learned_model_fingerprint(
                    models.parent),
            .components =
                old_school::
                    learned_model_component_fingerprints(
                        models.parent),
            .training_games = 1,
            .training_seed = 0xF405ULL,
            .generation = 1,
        },
        .corpus = {
            .artifact_bytes = 103,
            .artifact_sha256 = digest('b'),
        },
        .fit = {
            .input_sha256 = digest('c'),
            .examples = 248,
            .options = 987,
            .check_examples = 0,
            .background_only_examples = 0,
            .optimizer_calls = 1,
            .optimizer = dev::kOptimizer,
        },
        .candidate_model_fingerprint =
            old_school::learned_model_fingerprint(
                models.anchored),
        .priority_hidden_count =
            static_cast<std::uint32_t>(
                parent_parameters.input_hidden.size()),
        .priority_feature_count =
            static_cast<std::uint32_t>(
                parent_parameters.direct.size()),
        .priority_parameter_count =
            parameter_count(parent_parameters),
        .deployment = {
            .variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .training_games = 1,
            .worlds_per_action = 8,
            .horizon_turns = 4,
            .rollouts_per_world = 1,
            .root_search_depth = 1,
            .shallow_prior = true,
            .priority_residual_weight = 0.10,
            .continuation_controller =
                old_school::
                    LearnedContinuationController::Legacy,
            .max_turns = 500,
        },
    };
}

evaluator::TrainingBatch control_batch() {
    evaluator::TrainingBatch batch;
    batch.accounting = {
        .fit_examples =
            evaluator::kPositiveFitExamples,
        .fit_options =
            evaluator::kPositiveFitOptions,
        .check_examples = 0,
        .background_only_examples = 0,
        .optimizer_calls = 1,
        .training_input_sha256 =
            std::string(
                evaluator::
                    kRequiredPositiveOnlyTrainingInputSha256),
        .optimizer = dev::kOptimizer,
    };
    batch.positive_examples =
        evaluator::kPositiveFitExamples;
    batch.positive_options =
        evaluator::kPositiveFitOptions;
    batch.examples.resize(
        evaluator::kPositiveFitExamples);
    return batch;
}

evaluator::TrainingBatch anchored_batch(
    const artifact::Contract& contract) {
    evaluator::TrainingBatch batch;
    batch.accounting = {
        .fit_examples =
            static_cast<std::size_t>(
                contract.fit.examples),
        .fit_options =
            static_cast<std::size_t>(
                contract.fit.options),
        .check_examples = 0,
        .background_only_examples = 0,
        .optimizer_calls = 1,
        .training_input_sha256 =
            contract.fit.input_sha256,
        .optimizer = contract.fit.optimizer,
    };
    batch.positive_examples =
        evaluator::kPositiveFitExamples;
    batch.positive_options =
        evaluator::kPositiveFitOptions;
    batch.neutral_examples =
        evaluator::kNeutralRowsPerSplit;
    batch.neutral_options =
        static_cast<std::size_t>(
            contract.fit.options) -
        evaluator::kPositiveFitOptions;
    batch.examples.resize(
        static_cast<std::size_t>(
            contract.fit.examples));
    batch.neutral_examples_by_deck.fill(
        evaluator::kNeutralRowsPerDeck);
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        batch.neutral_loss_mass_by_deck[deck] =
            static_cast<double>(
                evaluator::
                    kPositiveFitExamplesByDeck[deck]);
    }
    return batch;
}

dev::ModelEvaluationReport model_evaluation_report(
    std::shared_ptr<const old_school::LearnedModel>
        candidate) {
    dev::ModelEvaluationReport report;
    report.parent_fingerprint =
        old_school::learned_model_fingerprint(
            synthetic_models().parent);
    report.candidate_fingerprint =
        old_school::learned_model_fingerprint(candidate);
    report.parent_components =
        old_school::learned_model_component_fingerprints(
            synthetic_models().parent);
    report.candidate_components =
        old_school::learned_model_component_fingerprints(
            candidate);
    report.parent_immutable = true;
    report.nonpriority_components_identical = true;
    report.metrics.fit.positive_roots =
        evaluator::kPositiveFitExamples;
    report.metrics.fit.positive_options =
        evaluator::kPositiveFitOptions;
    report.metrics.check.positive_roots =
        evaluator::kPositiveCheckExamples;
    report.metrics.check.positive_options =
        evaluator::kPositiveCheckOptions;
    report.metrics.parent_anchors_exact = true;
    return report;
}

evaluator::NeutralDriftMetrics neutral_check_metrics() {
    evaluator::NeutralDriftMetrics metrics;
    constexpr std::array<std::size_t, bundle::kDeckCount>
        kOptions{82, 86, 80, 103, 87};
    metrics.rows = evaluator::kNeutralRowsPerSplit;
    metrics.options = 438;
    metrics.finite_probabilities = true;
    metrics.baseline_equal_deck_kl = 0.5;
    metrics.anchored_equal_deck_kl = 0.125;
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        metrics.decks[deck].rows =
            evaluator::kNeutralRowsPerDeck;
        metrics.decks[deck].options = kOptions[deck];
        metrics.decks[deck]
            .baseline_parent_to_candidate_kl = 0.5;
        metrics.decks[deck]
            .anchored_parent_to_candidate_kl = 0.125;
    }
    return metrics;
}

evaluator::Report evaluation_report(
    const artifact::Contract& contract,
    std::shared_ptr<const old_school::LearnedModel>
        anchored) {
    evaluator::Report report;
    report.anchored_candidate = anchored;
    report.parent_fingerprint =
        contract.parent.model_fingerprint;
    report.positive_only_candidate_fingerprint =
        old_school::learned_model_fingerprint(
            synthetic_models().positive);
    report.anchored_candidate_fingerprint =
        old_school::learned_model_fingerprint(anchored);
    report.parent_components =
        old_school::learned_model_component_fingerprints(
            synthetic_models().parent);
    report.positive_only_candidate_components =
        old_school::learned_model_component_fingerprints(
            synthetic_models().positive);
    report.anchored_candidate_components =
        old_school::learned_model_component_fingerprints(
            anchored);
    report.omitted_neutral_control =
        control_batch();
    report.anchored_training =
        anchored_batch(contract);
    report.positive_only_evaluation =
        model_evaluation_report(
            synthetic_models().positive);
    report.anchored_evaluation =
        model_evaluation_report(anchored);
    report.neutral_check = neutral_check_metrics();
    report.isolation = {
        .parent_immutable = true,
        .positive_only_candidate_exact = true,
        .omitted_neutral_control_exact = true,
        .parent_anchors_exact = true,
        .hidden_repartition_contract_exact = true,
        .fit_check_isolated = true,
        .nonpriority_components_identical = true,
        .priority_component_changed = true,
    };
    report.gate = {
        .baseline_positive_contract_exact = true,
        .check_positive_clean = true,
        .fit_positive_preserved = true,
        .neutral_baseline_nonzero = true,
        .neutral_per_deck_nonworsening = true,
        .neutral_kl_halved = true,
        .neutral_support_changes_halved = true,
        .isolation_exact = true,
    };
    return report;
}

struct Fixture {
    std::filesystem::path executable =
        "synthetic-dev5-publisher";
    publisher::testing::FrozenInput dev1{
        .path = "synthetic-dev1",
        .bytes = 103,
        .sha256 = digest('b'),
    };
    publisher::testing::FrozenInput parent{
        .path = "synthetic-parent",
        .bytes = 101,
        .sha256 = digest('a'),
    };
    publisher::testing::FrozenInput positive{
        .path = "synthetic-positive",
        .bytes = 107,
        .sha256 = digest('d'),
    };
    publisher::testing::FrozenInput neutral_source{
        .path = "synthetic-neutral",
        .bytes = 109,
        .sha256 = digest('e'),
    };
    std::filesystem::path destination =
        "synthetic-anchored.fq4candidate";
    std::filesystem::path temporary =
        artifact::temporary_path_for(destination);
    artifact::Contract contract =
        synthetic_contract();
    artifact::Contract positive_contract =
        synthetic_contract();
    artifact::Report positive_report;
    artifact::Report published_report;

    bool destination_exists = false;
    bool temporary_exists = false;
    bool drift_neutral = false;
    bool nondeterministic = false;
    bool signed_zero_parameter_nondeterministic = false;
    bool positive_metric_nondeterministic = false;
    bool neutral_metric_nondeterministic = false;
    bool batch_nondeterministic = false;
    bool gate_failure = false;
    bool silent_gate_failure = false;
    bool isolation_failure = false;
    bool gate_failure_text_nondeterministic = false;
    bool occupy_destination_after_evaluation = false;
    bool wrong_publication_parent = false;
    bool wrong_reload_parent = false;
    std::size_t destination_absence_checks = 0;
    std::size_t executable_snapshots = 0;
    std::size_t dev1_snapshots = 0;
    std::size_t parent_snapshots = 0;
    std::size_t positive_snapshots = 0;
    std::size_t neutral_snapshots = 0;
    std::size_t artifact_snapshots = 0;
    std::size_t dev1_loads = 0;
    std::size_t parent_loads = 0;
    std::size_t positive_loads = 0;
    std::size_t neutral_loads = 0;
    std::size_t evaluations = 0;
    std::size_t publications = 0;
    std::size_t reloads = 0;

    Fixture() {
        positive_contract.candidate_model_fingerprint =
            old_school::learned_model_fingerprint(
                synthetic_models().positive);
        positive_report = {
            .artifact = {
                .bytes = positive.bytes,
                .sha256 = positive.sha256,
            },
            .manifest = {
                .contract = positive_contract,
                .candidate_components =
                    old_school::
                        learned_model_component_fingerprints(
                            synthetic_models().positive),
                .tensors = {
                    .hidden_count =
                        positive_contract
                            .priority_hidden_count,
                    .feature_count =
                        positive_contract
                            .priority_feature_count,
                    .parameter_count =
                        positive_contract
                            .priority_parameter_count,
                    .parent_sha256 = digest('1'),
                    .candidate_sha256 = digest('2'),
                    .xor_delta_sha256 = digest('3'),
                },
            },
        };
        published_report = {
            .artifact = {
                .bytes = 113,
                .sha256 = digest('f'),
            },
            .manifest = {
                .contract = contract,
                .candidate_components =
                    old_school::
                        learned_model_component_fingerprints(
                            synthetic_models().anchored),
                .tensors = {
                    .hidden_count =
                        contract.priority_hidden_count,
                    .feature_count =
                        contract.priority_feature_count,
                    .parameter_count =
                        contract.priority_parameter_count,
                    .parent_sha256 = digest('4'),
                    .candidate_sha256 = digest('5'),
                    .xor_delta_sha256 = digest('6'),
                },
            },
        };
    }

    publisher::testing::Recipe recipe() const {
        return {
            .executable_path = executable,
            .producer_commit =
                std::string(40, '7'),
            .dev1 = dev1,
            .parent = parent,
            .positive_candidate = positive,
            .neutral = neutral_source,
            .destination_path = destination,
            .temporary_path = temporary,
            .contract = contract,
        };
    }

    publisher::testing::Dependencies dependencies() {
        return {
            .snapshot =
                [this](const std::filesystem::path& path) {
                    if (path == executable) {
                        ++executable_snapshots;
                        return snapshot(
                            path, 97, digest('9'));
                    }
                    if (path == dev1.path) {
                        ++dev1_snapshots;
                        return snapshot(
                            path, dev1.bytes, dev1.sha256);
                    }
                    if (path == parent.path) {
                        ++parent_snapshots;
                        return snapshot(
                            path, parent.bytes,
                            parent.sha256);
                    }
                    if (path == positive.path) {
                        ++positive_snapshots;
                        return snapshot(
                            path, positive.bytes,
                            positive.sha256);
                    }
                    if (path == neutral_source.path) {
                        ++neutral_snapshots;
                        return snapshot(
                            path, neutral_source.bytes,
                            drift_neutral &&
                                    neutral_snapshots > 1
                                ? digest('8')
                                : neutral_source.sha256);
                    }
                    if (path == destination &&
                        destination_exists) {
                        ++artifact_snapshots;
                        return snapshot(
                            path,
                            published_report
                                .artifact.bytes,
                            published_report
                                .artifact.sha256);
                    }
                    throw std::runtime_error(
                        "unexpected snapshot path");
                },
            .path_absent =
                [this](const std::filesystem::path& path) {
                    if (path == destination) {
                        ++destination_absence_checks;
                        if (occupy_destination_after_evaluation &&
                            evaluations == 2) {
                            destination_exists = true;
                        }
                        return !destination_exists;
                    }
                    if (path == temporary) {
                        return !temporary_exists;
                    }
                    throw std::runtime_error(
                        "unexpected absence path");
                },
            .load_dev1 =
                [this] {
                    ++dev1_loads;
                    return bundle::Bundle{};
                },
            .prepare_dev1 =
                [](const bundle::Bundle&) {
                    return dev::PreparedCorpus{};
                },
            .load_parent =
                [this] {
                    ++parent_loads;
                    if ((wrong_publication_parent &&
                         parent_loads == 2) ||
                        (wrong_reload_parent &&
                         parent_loads == 3)) {
                        return synthetic_models().positive;
                    }
                    return synthetic_models().parent;
                },
            .load_positive_candidate =
                [this](
                    std::shared_ptr<
                        const old_school::LearnedModel>) {
                    ++positive_loads;
                    return publisher::testing::
                        LoadedPositiveCandidate{
                            .model =
                                synthetic_models().positive,
                            .report = positive_report,
                        };
                },
            .load_neutral =
                [this](const bundle::Manifest&) {
                    ++neutral_loads;
                    return publisher::testing::LoadedNeutral{
                        .artifact = neutral::Artifact{},
                        .identity = {
                            .bytes =
                                neutral_source.bytes,
                            .sha256 =
                                neutral_source.sha256,
                        },
                    };
                },
            .evaluate =
                [this](
                    const dev::PreparedCorpus&,
                    const neutral::Artifact&,
                    std::shared_ptr<
                        const old_school::LearnedModel>,
                    std::shared_ptr<
                        const old_school::LearnedModel>) {
                    ++evaluations;
                    auto report = evaluation_report(
                        contract,
                        nondeterministic &&
                                evaluations == 2
                            ? synthetic_models().alternate
                            : signed_zero_parameter_nondeterministic &&
                                      evaluations == 2
                                ? synthetic_models()
                                      .signed_zero_alternate
                            : synthetic_models().anchored);
                    if (positive_metric_nondeterministic &&
                        evaluations == 2) {
                        report.anchored_evaluation.metrics
                            .check.decks[0]
                            .target_to_candidate_kl = -0.0;
                    }
                    if (neutral_metric_nondeterministic &&
                        evaluations == 2) {
                        report.neutral_check.decks[0]
                            .anchored_parent_to_candidate_kl =
                            -0.0;
                    }
                    if (batch_nondeterministic &&
                        evaluations == 2) {
                        report.anchored_training.examples[0]
                            .weight = -0.0;
                    }
                    if (gate_failure) {
                        report.gate.failures.push_back(
                            "private gate detail");
                    }
                    if (silent_gate_failure) {
                        report.gate
                            .baseline_positive_contract_exact =
                            false;
                    }
                    if (isolation_failure) {
                        report.isolation.parent_immutable =
                            false;
                    }
                    if (gate_failure_text_nondeterministic &&
                        evaluations == 2) {
                        report.gate.failures.push_back(
                            "second-run-only detail");
                    }
                    return report;
                },
            .publish =
                [this](
                    const std::filesystem::path& path,
                    std::shared_ptr<
                        const old_school::LearnedModel>,
                    std::shared_ptr<
                        const old_school::LearnedModel>,
                    const artifact::Contract& supplied) {
                    expect(
                        path == destination &&
                            supplied == contract,
                        "publish received the wrong recipe");
                    ++publications;
                    destination_exists = true;
                    return published_report;
                },
            .reload =
                [this](
                    const std::filesystem::path& path,
                    std::shared_ptr<
                        const old_school::LearnedModel>,
                    const artifact::Contract& supplied,
                    const artifact::FileIdentity& identity) {
                    expect(
                        path == destination &&
                            supplied == contract &&
                            identity ==
                                published_report.artifact,
                        "reload received the wrong identity");
                    ++reloads;
                    return publisher::testing::
                        ReloadedCandidate{
                            .model =
                                synthetic_models().anchored,
                            .report = published_report,
                        };
                },
        };
    }
};

void test_production_contract_and_coordinates() {
    const artifact::Contract& contract =
        publisher::production_contract();
    publisher::testing::validate_frozen_contract(
        contract);
    expect(
        contract == literal_production_contract() &&
            contract.family == publisher::kFamily &&
            contract.environment ==
                publisher::kEnvironment &&
            contract.environment.size() == 238 &&
            contract.environment ==
                "old-school-environment-v3-cleanup-discard;"
                "dev1-bytes=2250909;"
                "dev1-sha256="
                "0911fc2eb8b14ddc9165543eb1e4c4ed"
                "b0b058256a58dedf61f6c4ea4ca859df;"
                "neutral-bytes=661475;"
                "neutral-sha256="
                "47d94823f043971f6f9f0aa5f552bfae"
                "210af9615d8f6dc7392e52dad3eaa105" &&
            contract.fit.input_sha256 ==
                publisher::kFitInputSha256 &&
            contract.fit.examples == 248 &&
            contract.fit.options == 987 &&
            contract.fit.check_examples == 0 &&
            contract.fit.background_only_examples == 0 &&
            contract.fit.optimizer_calls == 1 &&
            contract.candidate_model_fingerprint ==
                publisher::kCandidateFingerprint &&
            publisher::production_artifact_path() ==
                std::filesystem::path(
                    publisher::kArtifactPath) &&
            publisher::production_temporary_path() ==
                artifact::temporary_path_for(
                    publisher::production_artifact_path()),
        "production contract or coordinates drifted");

    artifact::Contract drifted = contract;
    drifted.family = "wrong-family";
    expect_throws_contains(
        [&] {
            publisher::testing::validate_frozen_contract(
                drifted);
        },
        "production_contract_mismatch");
    drifted = contract;
    drifted.fit.options = 986;
    expect_throws_contains(
        [&] {
            publisher::testing::validate_frozen_contract(
                drifted);
        },
        "production_contract_mismatch");
    drifted = contract;
    drifted.environment += ";";
    expect_throws_contains(
        [&] {
            publisher::testing::validate_frozen_contract(
                drifted);
        },
        "production_contract_mismatch");
    drifted = contract;
    drifted.deployment.root_exploration = -0.0;
    expect_throws_contains(
        [&] {
            publisher::testing::validate_frozen_contract(
                drifted);
        },
        "production_contract_mismatch");
    drifted = contract;
    drifted.deployment.continuation_epsilon = -0.0;
    expect_throws_contains(
        [&] {
            publisher::testing::validate_frozen_contract(
                drifted);
        },
        "production_contract_mismatch");
}

void test_preexisting_coordinate_stops_before_load() {
    for (const bool destination : {true, false}) {
        Fixture fixture;
        fixture.destination_exists = destination;
        fixture.temporary_exists = !destination;
        expect_throws_contains(
            [&] {
                static_cast<void>(
                    publisher::testing::
                        publish_candidate(
                            fixture.recipe(),
                            fixture.dependencies()));
            },
            "publication_coordinate_not_new");
        expect(
            fixture.dev1_loads == 0 &&
                fixture.parent_loads == 0 &&
                fixture.evaluations == 0 &&
                fixture.publications == 0,
            "occupied coordinate allowed scientific work");
    }
}

void test_broken_symlink_is_an_occupied_coordinate() {
    int address_anchor = 0;
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("old-school-fq4-publisher-test-" +
         std::to_string(
             reinterpret_cast<std::uintptr_t>(
                 &address_anchor)));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{directory};
    std::filesystem::create_directory(directory);
    const std::filesystem::path missing =
        directory / "missing-target";
    const std::filesystem::path broken =
        directory / "broken-coordinate";
    std::filesystem::create_symlink(missing, broken);
    expect(
        publisher::testing::
                publication_coordinate_absent(missing) &&
            !publisher::testing::
                publication_coordinate_absent(broken),
        "broken symlink was treated as an absent coordinate");
}

void test_invalid_commit_stops_before_work() {
    for (const std::string& commit : {
             std::string{},
             std::string(39, '7'),
             std::string(40, '0'),
             std::string(40, 'A')}) {
        Fixture fixture;
        auto recipe = fixture.recipe();
        recipe.producer_commit = commit;
        expect_throws_contains(
            [&] {
                static_cast<void>(
                    publisher::testing::publish_candidate(
                        recipe, fixture.dependencies()));
            },
            "invalid_publisher_recipe");
        expect(
            fixture.executable_snapshots == 0 &&
                fixture.dev1_loads == 0 &&
                fixture.parent_loads == 0 &&
                fixture.evaluations == 0,
            "invalid producer commit allowed work");
    }
}

void test_source_drift_publishes_nothing() {
    Fixture fixture;
    fixture.drift_neutral = true;
    expect_throws_contains(
        [&] {
            static_cast<void>(
                publisher::testing::publish_candidate(
                    fixture.recipe(),
                    fixture.dependencies()));
        },
        "source_changed_during_publication");
    expect(
        fixture.evaluations == 2 &&
            fixture.publications == 0 &&
            !fixture.destination_exists,
        "source drift published a candidate");
}

void test_nondeterminism_and_gate_failure_publish_nothing() {
    const auto rejected =
        [](const std::function<void(Fixture&)>& mutate,
           std::string_view message) {
        Fixture fixture;
        mutate(fixture);
        expect_throws_contains(
            [&] {
                static_cast<void>(
                    publisher::testing::
                        publish_candidate(
                            fixture.recipe(),
                            fixture.dependencies()));
            },
            "evaluation_reproduction_or_gate_mismatch");
        expect(
            fixture.evaluations == 2 &&
                fixture.publications == 0,
            message);
    };
    rejected(
        [](Fixture& fixture) {
            fixture.nondeterministic = true;
        },
        "model nondeterminism published");
    rejected(
        [](Fixture& fixture) {
            fixture.signed_zero_parameter_nondeterministic =
                true;
        },
        "signed-zero parameter drift published");
    rejected(
        [](Fixture& fixture) {
            fixture.positive_metric_nondeterministic = true;
        },
        "signed-zero positive metric drift published");
    rejected(
        [](Fixture& fixture) {
            fixture.neutral_metric_nondeterministic = true;
        },
        "neutral metric drift published");
    rejected(
        [](Fixture& fixture) {
            fixture.batch_nondeterministic = true;
        },
        "training batch drift published");
    rejected(
        [](Fixture& fixture) {
            fixture.gate_failure = true;
        },
        "failed scientific gate published");
    rejected(
        [](Fixture& fixture) {
            fixture.silent_gate_failure = true;
        },
        "empty-failure false gate published");
    rejected(
        [](Fixture& fixture) {
            fixture.isolation_failure = true;
        },
        "false isolation check published");
    rejected(
        [](Fixture& fixture) {
            fixture.gate_failure_text_nondeterministic =
                true;
        },
        "gate failure text drift published");
}

void test_coordinate_race_publishes_nothing() {
    Fixture fixture;
    fixture.occupy_destination_after_evaluation = true;
    expect_throws_contains(
        [&] {
            static_cast<void>(
                publisher::testing::publish_candidate(
                    fixture.recipe(),
                    fixture.dependencies()));
        },
        "publication_coordinate_not_new");
    expect(
        fixture.evaluations == 2 &&
            fixture.destination_absence_checks == 2 &&
            fixture.publications == 0,
        "late coordinate occupation published");
}

void test_fresh_parent_loads_are_exact() {
    {
        Fixture fixture;
        fixture.wrong_publication_parent = true;
        expect_throws_contains(
            [&] {
                static_cast<void>(
                    publisher::testing::publish_candidate(
                        fixture.recipe(),
                        fixture.dependencies()));
            },
            "loaded_parent_contract_mismatch");
        expect(
            fixture.parent_loads == 2 &&
                fixture.publications == 0,
            "wrong publication parent reached publish");
    }
    {
        Fixture fixture;
        fixture.wrong_reload_parent = true;
        expect_throws_contains(
            [&] {
                static_cast<void>(
                    publisher::testing::publish_candidate(
                        fixture.recipe(),
                        fixture.dependencies()));
            },
            "loaded_parent_contract_mismatch");
        expect(
            fixture.parent_loads == 3 &&
                fixture.publications == 1 &&
                fixture.reloads == 0,
            "wrong reload parent reached reload");
    }
}

void test_success_publishes_reloads_and_holds_sources() {
    Fixture fixture;
    const publisher::RunReport report =
        publisher::testing::publish_candidate(
            fixture.recipe(), fixture.dependencies());
    expect(
        fixture.dev1_loads == 1 &&
            fixture.parent_loads == 3 &&
            fixture.positive_loads == 1 &&
            fixture.neutral_loads == 1 &&
            fixture.evaluations == 2 &&
            fixture.publications == 1 &&
            fixture.reloads == 1,
        "success used the wrong operation counts");
    expect(
        fixture.executable_snapshots == 3 &&
            fixture.dev1_snapshots == 3 &&
            fixture.parent_snapshots == 3 &&
            fixture.positive_snapshots == 3 &&
            fixture.neutral_snapshots == 3 &&
            fixture.artifact_snapshots == 2,
        "success omitted an immutability boundary");
    expect(
        report.executable_before ==
                report.executable_after &&
            report.dev1_before == report.dev1_after &&
            report.parent_before ==
                report.parent_after &&
            report.positive_candidate_before ==
                report.positive_candidate_after &&
            report.neutral_before ==
                report.neutral_after &&
            report.artifact_published ==
                report.artifact_reloaded &&
            report.artifact == fixture.published_report &&
            report.first_evaluation.gate.passed() &&
            report.second_evaluation.gate.passed() &&
            fixture.destination_exists &&
            !fixture.temporary_exists,
        "success report lost publication evidence");
}

void test_cli_no_args_and_redaction() {
    char program[] = "anchored-publisher";
    char extra[] = "--override";
    char* invalid[] = {program, extra};
    bool called = false;
    std::ostringstream output;
    std::ostringstream error;
    expect(
        publisher::testing::run_cli(
            2, invalid, output, error,
            std::string(40, '7'),
            [&called](
                const std::filesystem::path&,
                std::string_view) {
                called = true;
                return publisher::RunReport{};
            }) == 2 &&
            !called &&
            output.str().empty() &&
            error.str().find("Usage:") !=
                std::string::npos,
        "CLI accepted an override");

    char* valid[] = {program};
    for (const std::string& invalid_commit : {
             std::string{},
             std::string(39, '7'),
             std::string(40, '0'),
             std::string(40, 'A')}) {
        called = false;
        output.str("");
        output.clear();
        error.str("");
        error.clear();
        expect(
            publisher::testing::run_cli(
                1, valid, output, error,
                invalid_commit,
                [&called](
                    const std::filesystem::path&,
                    std::string_view) {
                    called = true;
                    return publisher::RunReport{};
                }) == 2 &&
                !called &&
                output.str().empty() &&
                error.str().find("Usage:") !=
                    std::string::npos,
            "CLI accepted an invalid producer commit");
    }
    output.str("");
    output.clear();
    error.str("");
    error.clear();
    expect(
        publisher::testing::run_cli(
            1, valid, output, error,
            std::string(40, '7'),
            [](const std::filesystem::path&,
               std::string_view) ->
                publisher::RunReport {
                throw std::runtime_error(
                    "private artifact path and gate detail");
            }) == 2 &&
            output.str().empty() &&
            error.str() ==
                "result=ERROR"
                " reason=fixed_anchored_candidate_"
                "publication_failed\n" &&
            error.str().find("private") ==
                std::string::npos,
        "CLI leaked exception details");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "production contract and coordinates",
        test_production_contract_and_coordinates);
    tests.run(
        "preexisting coordinate",
        test_preexisting_coordinate_stops_before_load);
    tests.run(
        "broken symlink coordinate",
        test_broken_symlink_is_an_occupied_coordinate);
    tests.run(
        "producer commit preflight",
        test_invalid_commit_stops_before_work);
    tests.run(
        "source drift",
        test_source_drift_publishes_nothing);
    tests.run(
        "nondeterminism and gate failure",
        test_nondeterminism_and_gate_failure_publish_nothing);
    tests.run(
        "coordinate race",
        test_coordinate_race_publishes_nothing);
    tests.run(
        "fresh parent loads",
        test_fresh_parent_loads_are_exact);
    tests.run(
        "publication reload",
        test_success_publishes_reloads_and_holds_sources);
    tests.run(
        "CLI no args and redaction",
        test_cli_no_args_and_redaction);
    return tests.finish();
}
