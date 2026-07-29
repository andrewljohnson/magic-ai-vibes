#include "old_school/decision_boundary_critic.hpp"
#include "old_school/decision_boundary_critic_gate.hpp"

#include "old_school/artifact_integrity.hpp"

#include <exception>
#include <algorithm>
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
constexpr std::string_view kCachePath =
    "build/model-cache/"
    "old-school-aq10-dbc1-owner-safe-corpus-v1.bin";
constexpr std::string_view kExpectedDbc1CandidateFingerprint =
    "91ecdf2e47f0f1d94127d5bc2a33a71e52c5a8ecb6e0ef9c01d039e562815efb";

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

bool exact_dbc1_replication(
    const dbc::RunReport& report) {
    const auto all_true =
        [](const auto& values) {
            return std::all_of(
                values.begin(), values.end(),
                [](bool value) { return value; });
        };
    const auto& gate = report.gate;
    return
        report.corpus.census.parent_fingerprint ==
            dbc::kRequiredParentFingerprint &&
        report.corpus.census.subset_hash ==
            dbc::kFrozenSubsetHash &&
        report.corpus.digest ==
            dbc::kFrozenCorpusDigest &&
        report.corpus.train.size() ==
            dbc::kExpectedRootsPerSplit &&
        report.corpus.dev.size() ==
            dbc::kExpectedRootsPerSplit &&
        dbc::training_examples(report.corpus).size() ==
            1824 &&
        report.fit.candidate_fingerprint ==
            kExpectedDbc1CandidateFingerprint &&
        report.fit.changed_output_parameters ==
            dbc::kOutputParameterCount &&
        report.fit.optimizer.example_count == 1824 &&
        report.fit.optimizer.leaf_count ==
            old_school::kLearnedOutputCalibrationLeafCount &&
        report.fit.optimizer.iterations == 3 &&
        report.fit.parent_immutable &&
        report.fit.repeated_fit_bit_identical &&
        report.fit.parameter_replay_bit_identical &&
        report.fit.only_output_layer_changed &&
        gate.repeated_collection_bit_identical &&
        gate.hidden_repartition_bit_identical &&
        gate.source_and_subset_exact &&
        gate.accounting_exact &&
        gate.parent_immutable &&
        gate.repeated_fit_bit_identical &&
        gate.exact_output_component_isolation &&
        gate.train_bce_strictly_improved &&
        !gate.train_regret_strictly_improved &&
        !gate.dev_bce_strictly_improved &&
        !gate.dev_regret_strictly_improved &&
        !gate.dev_top_one_non_decreasing &&
        all_true(gate.parent_train_regret_nonzero) &&
        all_true(gate.parent_dev_regret_nonzero) &&
        std::count(
            gate.dev_deck_regret_guard.begin(),
            gate.dev_deck_regret_guard.end(),
            false) == 1 &&
        !gate.dev_deck_regret_guard[
            static_cast<std::size_t>(
                old_school::DeckId::RUAggro)] &&
        gate.failures.size() == 5 &&
        !gate.passed();
}

void publish_cache(
    std::ostream& output,
    std::shared_ptr<const old_school::LearnedModel> parent,
    const dbc::RunReport& report) {
    if (!exact_dbc1_replication(report)) {
        throw std::runtime_error(
            "AQ10-DBC1 cache replication did not reproduce "
            "the frozen rejected run");
    }

    const auto parent_train_predictions =
        dbc::score(report.corpus.train, parent);
    const auto parent_dev_predictions =
        dbc::score(report.corpus.dev, parent);
    const auto parent_train_metrics =
        dbc::evaluate(
            report.corpus.train,
            parent_train_predictions);
    const auto parent_dev_metrics =
        dbc::evaluate(
            report.corpus.dev,
            parent_dev_predictions);
    const dbc::Corpus decoded =
        dbc::roundtrip_corpus_cache(
            report.corpus, parent);
    const auto decoded_train_predictions =
        dbc::score(decoded.train, parent);
    const auto decoded_dev_predictions =
        dbc::score(decoded.dev, parent);
    if (dbc::training_examples(decoded) !=
            dbc::training_examples(report.corpus) ||
        decoded_train_predictions !=
            parent_train_predictions ||
        decoded_dev_predictions !=
            parent_dev_predictions ||
        dbc::evaluate(
            decoded.train,
            decoded_train_predictions) !=
            parent_train_metrics ||
        dbc::evaluate(
            decoded.dev,
            decoded_dev_predictions) !=
            parent_dev_metrics) {
        throw std::runtime_error(
            "AQ10-DBC1 cache prepublication roundtrip drifted");
    }

    dbc::write_corpus_cache_atomic(
        std::string(kCachePath), decoded);
    const dbc::Corpus loaded =
        dbc::load_corpus_cache(
            std::string(kCachePath), parent);
    const auto loaded_train_predictions =
        dbc::score(loaded.train, parent);
    const auto loaded_dev_predictions =
        dbc::score(loaded.dev, parent);
    if (loaded != decoded ||
        dbc::training_examples(loaded) !=
            dbc::training_examples(decoded) ||
        loaded_train_predictions !=
            decoded_train_predictions ||
        loaded_dev_predictions !=
            decoded_dev_predictions ||
        dbc::evaluate(
            loaded.train,
            loaded_train_predictions) !=
            parent_train_metrics ||
        dbc::evaluate(
            loaded.dev,
            loaded_dev_predictions) !=
            parent_dev_metrics) {
        throw std::runtime_error(
            "AQ10-DBC1 published cache roundtrip drifted");
    }
    const auto snapshot =
        old_school::artifact_integrity::
            snapshot_regular_file(
                std::string(kCachePath));
    output
        << "cache_result=PUBLISHED path=" << kCachePath
        << " bytes=" << snapshot.byte_size
        << " sha256=" << snapshot.sha256
        << " corpus_digest=" << loaded.digest
        << " train_roots=" << loaded.train.size()
        << " dev_roots=" << loaded.dev.size()
        << " train_examples="
        << dbc::training_examples(loaded).size()
        << " state_serialized=0 hidden_identity_serialized=0\n";
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
    const bool cache_mode =
        arguments.size() == 1 &&
        arguments.front() == "--cache";
    if (!census_mode && !run_mode && !cache_mode) {
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
        if (cache_mode) {
            publish_cache(
                std::cout, parent, offline);
            require_parent_artifact_unchanged(
                parent_artifact);
            return 0;
        }
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
