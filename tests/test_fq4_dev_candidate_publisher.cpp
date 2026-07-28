#include "old_school/fq4_dev_candidate_publisher.hpp"

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
namespace evaluator =
    old_school::fq4_dev_evaluator;
namespace publisher =
    old_school::fq4_dev_candidate_publisher;

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
                  << " FQ4 DEV1 candidate publisher tests passed\n";
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
void expect_throws(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("operation did not throw");
}

std::string digest(char character) {
    return std::string(64, character);
}

old_school::artifact_integrity::RegularFileSnapshot
snapshot(
    const std::filesystem::path& path,
    std::uintmax_t bytes, const std::string& sha256) {
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

struct SyntheticModels {
    std::shared_ptr<const old_school::LearnedModel> parent;
    std::shared_ptr<const old_school::LearnedModel> candidate;
    std::shared_ptr<const old_school::LearnedModel>
        alternate_candidate;
};

const SyntheticModels& synthetic_models() {
    static const SyntheticModels models = [] {
        const auto parent =
            old_school::train_learned_value_challenger(
                1, 0xF401ULL, 1);
        auto parameters =
            old_school::learned_priority_head_parameters(
                parent);
        parameters.output_bias += 0.125;
        const auto candidate =
            old_school::
                with_learned_priority_head_parameters(
                    parent, parameters);
        parameters.output_bias += 0.125;
        const auto alternate =
            old_school::
                with_learned_priority_head_parameters(
                    parent, parameters);
        return SyntheticModels{
            .parent = parent,
            .candidate = candidate,
            .alternate_candidate = alternate,
        };
    }();
    return models;
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

struct Fixture {
    std::filesystem::path executable = "synthetic-publisher";
    std::filesystem::path corpus = "synthetic-corpus";
    std::filesystem::path parent = "synthetic-parent";
    std::filesystem::path destination =
        "synthetic-candidate.fq4candidate";
    std::filesystem::path temporary =
        artifact::temporary_path_for(destination);
    artifact::Contract contract;
    artifact::Report artifact_report;
    evaluator::FitAccounting fit_accounting;

    bool destination_exists = false;
    bool temporary_exists = false;
    bool drift_corpus = false;
    bool mismatch_second_fit = false;
    std::size_t executable_snapshots = 0;
    std::size_t corpus_snapshots = 0;
    std::size_t parent_snapshots = 0;
    std::size_t artifact_snapshots = 0;
    std::size_t corpus_loads = 0;
    std::size_t parent_loads = 0;
    std::size_t fit_calls = 0;
    std::size_t publish_calls = 0;
    std::size_t reload_calls = 0;

    Fixture() {
        const auto& models = synthetic_models();
        const auto parameters =
            old_school::learned_priority_head_parameters(
                models.parent);
        const auto parent_components =
            old_school::learned_model_component_fingerprints(
                models.parent);
        const auto candidate_components =
            old_school::learned_model_component_fingerprints(
                models.candidate);
        contract = {
            .family = std::string(artifact::kSchema),
            .environment = "synthetic-test-environment",
            .parent =
                {
                    .artifact_bytes = 101,
                    .artifact_sha256 = digest('a'),
                    .model_fingerprint =
                        old_school::learned_model_fingerprint(
                            models.parent),
                    .components = parent_components,
                    .training_games = 1,
                    .training_seed = 0xF401ULL,
                    .generation = 1,
                },
            .corpus =
                {
                    .artifact_bytes = 103,
                    .artifact_sha256 = digest('b'),
                },
            .fit =
                {
                    .input_sha256 = digest('c'),
                    .examples = 2,
                    .options = 5,
                    .check_examples = 0,
                    .background_only_examples = 0,
                    .optimizer_calls = 1,
                    .optimizer = {},
                },
            .candidate_model_fingerprint =
                old_school::learned_model_fingerprint(
                    models.candidate),
            .priority_hidden_count =
                static_cast<std::uint32_t>(
                    parameters.input_hidden.size()),
            .priority_feature_count =
                static_cast<std::uint32_t>(
                    parameters.direct.size()),
            .priority_parameter_count =
                parameter_count(parameters),
            .deployment = {},
        };
        fit_accounting = {
            .fit_examples = 2,
            .fit_options = 5,
            .check_examples = 0,
            .background_only_examples = 0,
            .optimizer_calls = 1,
            .training_input_sha256 = digest('c'),
            .optimizer = {},
        };
        artifact_report = {
            .artifact =
                {
                    .bytes = 107,
                    .sha256 = digest('d'),
                },
            .manifest =
                {
                    .contract = contract,
                    .candidate_components =
                        candidate_components,
                    .tensors =
                        {
                            .hidden_count =
                                contract
                                    .priority_hidden_count,
                            .feature_count =
                                contract
                                    .priority_feature_count,
                            .parameter_count =
                                contract
                                    .priority_parameter_count,
                            .parent_sha256 = digest('e'),
                            .candidate_sha256 = digest('f'),
                            .xor_delta_sha256 = digest('1'),
                        },
                },
        };
    }

    publisher::testing::Recipe recipe() const {
        return {
            .executable_path = executable,
            .corpus_path = corpus,
            .parent_path = parent,
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
                    if (path == corpus) {
                        ++corpus_snapshots;
                        return snapshot(
                            path,
                            contract.corpus.artifact_bytes,
                            drift_corpus &&
                                    corpus_snapshots > 1
                                ? digest('8')
                                : contract.corpus
                                      .artifact_sha256);
                    }
                    if (path == parent) {
                        ++parent_snapshots;
                        return snapshot(
                            path,
                            contract.parent.artifact_bytes,
                            contract.parent
                                .artifact_sha256);
                    }
                    if (path == destination &&
                        destination_exists) {
                        ++artifact_snapshots;
                        return snapshot(
                            path,
                            artifact_report.artifact.bytes,
                            artifact_report.artifact.sha256);
                    }
                    throw std::runtime_error(
                        "unexpected synthetic snapshot");
                },
            .path_absent =
                [this](const std::filesystem::path& path) {
                    if (path == destination) {
                        return !destination_exists;
                    }
                    if (path == temporary) {
                        return !temporary_exists;
                    }
                    throw std::runtime_error(
                        "unexpected synthetic absence check");
                },
            .load_corpus =
                [this] {
                    ++corpus_loads;
                    return evaluator::PreparedCorpus{};
                },
            .load_parent =
                [this] {
                    ++parent_loads;
                    return synthetic_models().parent;
                },
            .fit_candidate =
                [this](
                    const evaluator::PreparedCorpus&,
                    std::shared_ptr<
                        const old_school::LearnedModel>) {
                    ++fit_calls;
                    return evaluator::CandidateFit{
                        .model =
                            mismatch_second_fit &&
                                    fit_calls == 2
                                ? synthetic_models()
                                      .alternate_candidate
                                : synthetic_models().candidate,
                        .accounting = fit_accounting,
                    };
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
                        "publisher received the wrong recipe");
                    ++publish_calls;
                    destination_exists = true;
                    return artifact_report;
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
                                artifact_report.artifact,
                        "reloader received the wrong identity");
                    ++reload_calls;
                    return publisher::testing::
                        ReloadedCandidate{
                            .model =
                                synthetic_models().candidate,
                            .report = artifact_report,
                        };
                },
        };
    }
};

void test_bad_arguments_do_not_start_publisher() {
    std::size_t calls = 0;
    const publisher::testing::FixedPublisher fixed =
        [&calls](const std::filesystem::path&) {
            ++calls;
            throw std::runtime_error(
                "publisher must not be called");
            return publisher::RunReport{};
        };
    char program[] = "publisher";
    char extra[] = "unexpected";
    char* argv[] = {program, extra};
    std::ostringstream output;
    std::ostringstream error;
    const int code = publisher::testing::run_cli(
        2, argv, output, error, fixed);
    expect(code == 2, "bad arguments did not return 2");
    expect(calls == 0, "bad arguments started a fit");
    expect(output.str().empty(),
           "bad arguments wrote success output");
    expect(
        error.str().find("Usage:") != std::string::npos,
        "bad arguments did not print generic usage");
}

void test_production_contract_is_exact() {
    publisher::testing::validate_frozen_contract(
        artifact::production_contract());
    artifact::Contract drifted =
        artifact::production_contract();
    drifted.deployment.root_search_depth = 0;
    expect_throws([&] {
        publisher::testing::validate_frozen_contract(
            drifted);
    });
    drifted = artifact::production_contract();
    drifted.deployment.priority_residual_weight = 0.0;
    expect_throws([&] {
        publisher::testing::validate_frozen_contract(
            drifted);
    });
}

void test_preexisting_coordinates_fail_before_fit() {
    for (const bool occupy_destination : {true, false}) {
        Fixture fixture;
        fixture.destination_exists = occupy_destination;
        fixture.temporary_exists = !occupy_destination;
        auto dependencies = fixture.dependencies();
        expect_throws([&] {
            static_cast<void>(
                publisher::testing::publish_candidate(
                    fixture.recipe(), dependencies));
        });
        expect(
            fixture.corpus_loads == 0 &&
                fixture.parent_loads == 0 &&
                fixture.fit_calls == 0 &&
                fixture.publish_calls == 0,
            "occupied coordinate was checked after work began");
    }
}

void test_repeat_fit_mismatch_publishes_nothing() {
    Fixture fixture;
    fixture.mismatch_second_fit = true;
    auto dependencies = fixture.dependencies();
    expect_throws([&] {
        static_cast<void>(
            publisher::testing::publish_candidate(
                fixture.recipe(), dependencies));
    });
    expect(
        fixture.fit_calls == 2,
        "repeat-fit mismatch was not measured twice");
    expect(
        fixture.publish_calls == 0 &&
            !fixture.destination_exists,
        "repeat-fit mismatch published an artifact");
}

void test_source_drift_publishes_nothing() {
    Fixture fixture;
    fixture.drift_corpus = true;
    auto dependencies = fixture.dependencies();
    expect_throws([&] {
        static_cast<void>(
            publisher::testing::publish_candidate(
                fixture.recipe(), dependencies));
    });
    expect(
        fixture.fit_calls == 2,
        "source drift was not checked after both fits");
    expect(
        fixture.publish_calls == 0 &&
            !fixture.destination_exists,
        "source drift published an artifact");
}

void test_success_reproduces_publishes_and_reloads() {
    Fixture fixture;
    auto dependencies = fixture.dependencies();
    const publisher::RunReport report =
        publisher::testing::publish_candidate(
            fixture.recipe(), dependencies);
    expect(
        fixture.corpus_loads == 1 &&
            fixture.parent_loads == 1,
        "success did not load each frozen source once");
    expect(
        fixture.fit_calls == 2,
        "success did not reproduce the fit exactly twice");
    expect(
        fixture.publish_calls == 1 &&
            fixture.reload_calls == 1,
        "success did not publish and reload exactly once");
    expect(
        fixture.executable_snapshots == 3 &&
            fixture.corpus_snapshots == 3 &&
            fixture.parent_snapshots == 3,
        "success did not snapshot every source at all boundaries");
    expect(
        fixture.artifact_snapshots == 2,
        "success did not hold published bytes across reload");
    expect(
        report.executable_before ==
                report.executable_after &&
            report.corpus_before ==
                report.corpus_after &&
            report.parent_before ==
                report.parent_after,
        "success did not hold sources immutable");
    expect(
        report.artifact ==
                fixture.artifact_report &&
            report.artifact_published ==
                report.artifact_reloaded,
        "success report lost artifact identity");
    expect(
        report.first_fit == fixture.fit_accounting &&
            report.second_fit == fixture.fit_accounting,
        "success report lost repeated-fit accounting");
    expect(
        fixture.destination_exists &&
            !fixture.temporary_exists,
        "success left the publication coordinates invalid");
}

void test_cli_suppresses_exception_details() {
    const publisher::testing::FixedPublisher fixed =
        [](const std::filesystem::path&) ->
            publisher::RunReport {
            throw std::runtime_error(
                "sensitive synthetic detail");
        };
    char program[] = "publisher";
    char* argv[] = {program};
    std::ostringstream output;
    std::ostringstream error;
    const int code = publisher::testing::run_cli(
        1, argv, output, error, fixed);
    expect(code == 2, "publisher failure did not return 2");
    expect(output.str().empty(),
           "publisher failure wrote success output");
    expect(
        error.str() ==
            "result=ERROR"
            " reason=fixed_candidate_publication_failed\n",
        "publisher failure leaked exception detail");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "bad arguments do not start publisher",
        test_bad_arguments_do_not_start_publisher);
    runner.run(
        "production contract is exact",
        test_production_contract_is_exact);
    runner.run(
        "preexisting coordinates fail before fit",
        test_preexisting_coordinates_fail_before_fit);
    runner.run(
        "repeat-fit mismatch publishes nothing",
        test_repeat_fit_mismatch_publishes_nothing);
    runner.run(
        "source drift publishes nothing",
        test_source_drift_publishes_nothing);
    runner.run(
        "success reproduces publishes and reloads",
        test_success_reproduces_publishes_and_reloads);
    runner.run(
        "CLI suppresses exception details",
        test_cli_suppresses_exception_details);
    return runner.finish();
}
