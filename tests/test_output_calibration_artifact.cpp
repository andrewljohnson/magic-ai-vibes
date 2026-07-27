#include "old_school/artifact_integrity.hpp"
#include "old_school/output_calibration_artifact.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace oc = old_school::output_calibration;

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
                  << " output-calibration-artifact tests passed\n";
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
            std::string(expected) + "': " + error.what());
    }
    throw std::runtime_error(
        "operation did not throw '" +
        std::string(expected) + "'");
}

std::shared_ptr<const old_school::LearnedModel>
test_parent();

std::string digest(char fill) {
    return std::string(64, fill);
}

oc::CollectionConfig test_collection_config() {
    return {
        .seed = 0x0C1F17C011EC7ULL,
        .generation = 3,
        .balanced_blocks = 1,
        .max_game_turns = 1,
        .pilot_training_games = 1,
        .worker_count = 1,
    };
}

inline constexpr std::uint64_t kTestParentSeed =
    0x0C1A471FAC7ULL;

struct ParentFixture {
    std::filesystem::path path;
    std::shared_ptr<const old_school::LearnedModel> model;
    oc::ParentArtifactIdentity identity;
    oc::VerifiedParentArtifact verified;
};

const ParentFixture& parent_fixture() {
    static const ParentFixture fixture = [] {
        const std::filesystem::path path =
            "build/test-output-calibration-artifact/"
            "verified-parent.bin";
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        const auto trained =
            old_school::
                train_learned_value_challenger_artifact(
                    1, kTestParentSeed, 1);
        old_school::
            write_learned_value_challenger_artifact_atomic(
                path.string(), trained);
        const auto snapshot =
            old_school::artifact_integrity::
                snapshot_regular_file(path);
        const oc::ParentArtifactIdentity identity{
            .byte_size = snapshot.byte_size,
            .sha256 = snapshot.sha256,
            .model_fingerprint =
                old_school::learned_model_fingerprint(
                    trained.model()),
            .training_games = 1,
            .training_seed = kTestParentSeed,
            .generations = 1,
        };
        auto verified =
            oc::verify_output_calibration_parent(
                path.string(), trained.model(), identity);
        return ParentFixture{
            .path = path,
            .model = trained.model(),
            .identity = identity,
            .verified = std::move(verified),
        };
    }();
    return fixture;
}

std::shared_ptr<const old_school::LearnedModel>
test_parent() {
    return parent_fixture().model;
}

const oc::VerifiedParentArtifact& verified_parent() {
    return parent_fixture().verified;
}

const oc::ParentArtifactIdentity& parent_identity() {
    return parent_fixture().identity;
}

const oc::TrainingCorpus& fit_corpus() {
    static const oc::TrainingCorpus corpus =
        oc::collect_training_corpus(
            test_parent(), test_collection_config());
    return corpus;
}

const oc::OutputCalibrationArtifact& test_artifact() {
    static const oc::OutputCalibrationArtifact artifact =
        oc::make_output_calibration_artifact(
            verified_parent(), fit_corpus());
    return artifact;
}

std::vector<std::uint8_t> read_file(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read test artifact");
    }
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

void write_file(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(
        path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write test artifact");
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error(
            "short write of test artifact");
    }
}

std::filesystem::path test_directory() {
    return "build/test-output-calibration-artifact";
}

void remove_test_file(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void test_roundtrip_and_no_overwrite() {
    const auto& artifact = test_artifact();
    const std::filesystem::path path =
        test_directory() / "roundtrip.bin";
    remove_test_file(path);

    oc::write_output_calibration_artifact_atomic_no_replace(
        path.string(), artifact);
    const auto first_bytes = read_file(path);
    const auto loaded = oc::load_output_calibration_artifact(
        path.string(), verified_parent(),
        test_collection_config());
    expect(
        loaded.report() == artifact.report(),
        "roundtrip changed artifact report");
    expect(
        old_school::learned_model_fingerprint(
            loaded.model()) ==
            artifact.report().candidate_fingerprint,
        "roundtrip changed candidate bits");
    expect(
        old_school::learned_output_calibration_parameters(
            loaded.model()) ==
            artifact.report().output_parameters.after,
        "roundtrip changed output parameters");

    expect_throws_contains(
        [&] {
            oc::
                write_output_calibration_artifact_atomic_no_replace(
                    path.string(), artifact);
        },
        "already exists");
    expect(
        read_file(path) == first_bytes,
        "no-overwrite publication modified existing bytes");

    expect_throws_contains(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_challenger_artifact(
                        path.string(), 1,
                        parent_identity().training_seed,
                        1));
        },
        "wrong magic");
    remove_test_file(path);
}

void test_corruption_trailing_and_wrong_family_fail() {
    const auto& artifact = test_artifact();
    const auto config = test_collection_config();
    const std::filesystem::path original =
        test_directory() / "source.bin";
    const std::filesystem::path corrupt =
        test_directory() / "corrupt.bin";
    const std::filesystem::path trailing =
        test_directory() / "trailing.bin";
    const std::filesystem::path family =
        test_directory() / "family.bin";
    for (const auto& path :
         {original, corrupt, trailing, family}) {
        remove_test_file(path);
    }
    oc::write_output_calibration_artifact_atomic_no_replace(
        original.string(), artifact);
    const auto original_bytes = read_file(original);

    auto changed = original_bytes;
    changed.back() ^= 0x01U;
    write_file(corrupt, changed);
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::load_output_calibration_artifact(
                    corrupt.string(), verified_parent(), config));
        },
        "checksum");

    changed = original_bytes;
    changed.push_back(0U);
    write_file(trailing, changed);
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::load_output_calibration_artifact(
                    trailing.string(), verified_parent(), config));
        },
        "trailing");

    changed = original_bytes;
    changed[4] = 'X';
    write_file(family, changed);
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::load_output_calibration_artifact(
                    family.string(), verified_parent(), config));
        },
        "wrong family");

    for (const auto& path :
         {original, corrupt, trailing, family}) {
        remove_test_file(path);
    }
}

void test_exact_parent_and_configuration_are_required() {
    const auto& artifact = test_artifact();
    const auto parent = test_parent();
    const std::filesystem::path path =
        test_directory() / "expectations.bin";
    remove_test_file(path);
    oc::write_output_calibration_artifact_atomic_no_replace(
        path.string(), artifact);

    auto wrong_identity = parent_identity();
    wrong_identity.sha256 = digest('b');
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::verify_output_calibration_parent(
                    parent_fixture().path.string(), parent,
                    wrong_identity));
        },
        "size/SHA-256");

    auto wrong_config = test_collection_config();
    ++wrong_config.generation;
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::load_output_calibration_artifact(
                    path.string(), verified_parent(),
                    wrong_config));
        },
        "fit configuration");

    old_school::LearnedOutputCalibrationConfig wrong_optimizer;
    wrong_optimizer.max_iterations = 31;
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::load_output_calibration_artifact(
                    path.string(), verified_parent(),
                    test_collection_config(),
                    wrong_optimizer));
        },
        "optimizer configuration");

    const auto other_parent =
        old_school::train_learned_value_champion(
            1, 0x0C1A471FAC8ULL);
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::verify_output_calibration_parent(
                    parent_fixture().path.string(),
                    other_parent, parent_identity()));
        },
        "fingerprints do not match");
    remove_test_file(path);
}

void rewrite_payload_checksum(
    std::vector<std::uint8_t>& bytes) {
    constexpr std::size_t kPayloadOffset =
        8 + 4 + 8 + 64;
    const auto payload = std::span<const std::uint8_t>(
        bytes.data() + kPayloadOffset,
        bytes.size() - kPayloadOffset);
    const std::string checksum =
        old_school::artifact_integrity::sha256_bytes(
            std::as_bytes(payload));
    std::copy(
        checksum.begin(), checksum.end(),
        bytes.begin() + 8 + 4 + 8);
}

void test_rechecks_tensor_ledger_after_valid_checksum() {
    const auto& artifact = test_artifact();
    const std::filesystem::path source =
        test_directory() / "ledger-source.bin";
    const std::filesystem::path tampered =
        test_directory() / "ledger-tampered.bin";
    remove_test_file(source);
    remove_test_file(tampered);
    oc::write_output_calibration_artifact_atomic_no_replace(
        source.string(), artifact);
    auto bytes = read_file(source);

    const std::string needle =
        artifact.report().parent_tensors.input_hidden;
    std::vector<std::size_t> occurrences;
    auto begin = bytes.begin();
    while (true) {
        const auto found = std::search(
            begin, bytes.end(),
            needle.begin(), needle.end());
        if (found == bytes.end()) {
            break;
        }
        occurrences.push_back(
            static_cast<std::size_t>(
                std::distance(bytes.begin(), found)));
        begin = std::next(found);
    }
    expect(
        occurrences.size() >= 2,
        "expected parent and candidate tensor digests");
    std::uint8_t& digit =
        bytes[occurrences.back()];
    digit = digit == '0' ? '1' : '0';
    rewrite_payload_checksum(bytes);
    write_file(tampered, bytes);
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::load_output_calibration_artifact(
                    tampered.string(), verified_parent(),
                    test_collection_config()));
        },
        "fingerprint mismatch");
    remove_test_file(source);
    remove_test_file(tampered);
}

void test_internal_family_metadata_is_rechecked() {
    const auto& artifact = test_artifact();
    const std::filesystem::path source =
        test_directory() / "metadata-source.bin";
    const std::filesystem::path tampered =
        test_directory() / "metadata-tampered.bin";
    remove_test_file(source);
    remove_test_file(tampered);
    oc::write_output_calibration_artifact_atomic_no_replace(
        source.string(), artifact);
    auto bytes = read_file(source);
    const std::string family(oc::kArtifactFamily);
    const auto found = std::search(
        bytes.begin(), bytes.end(),
        family.begin(), family.end());
    expect(
        found != bytes.end(),
        "serialized family metadata not found");
    *found = *found == 'x' ? 'y' : 'x';
    rewrite_payload_checksum(bytes);
    write_file(tampered, bytes);
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::load_output_calibration_artifact(
                    tampered.string(), verified_parent(),
                    test_collection_config()));
        },
        "family/schema/recipe");
    remove_test_file(source);
    remove_test_file(tampered);
}

void test_builder_reproduces_corpus_and_owns_fit() {
    const auto& artifact = test_artifact();
    const auto examples =
        oc::training_examples(fit_corpus());
    const auto expected =
        old_school::calibrate_learned_value_output_layer(
            test_parent(), examples);
    expect(
        artifact.report().optimizer_diagnostics ==
            expected.diagnostics,
        "artifact did not record the internally computed "
        "optimizer diagnostics");
    expect(
        old_school::learned_model_fingerprint(
            artifact.model()) ==
            old_school::learned_model_fingerprint(
                expected.model),
        "artifact did not publish the internally computed "
        "candidate");

    auto fake_hash = fit_corpus();
    fake_hash.hashes.features = digest('0');
    if (fake_hash.hashes.features ==
        fit_corpus().hashes.features) {
        fake_hash.hashes.features = digest('1');
    }
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::make_output_calibration_artifact(
                    verified_parent(), fake_hash));
        },
        "reproduced fit corpus");

    auto fake_order = fit_corpus();
    expect(
        fake_order.records.size() > 1,
        "tiny real corpus has too few records");
    std::swap(
        fake_order.records[0],
        fake_order.records[1]);
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::make_output_calibration_artifact(
                    verified_parent(), fake_order));
        },
        "reproduced fit corpus");
}

std::array<std::uint8_t, 8> encoded_real(double value) {
    const std::uint64_t bits =
        std::bit_cast<std::uint64_t>(value);
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0; index < bytes.size();
         ++index) {
        bytes[index] = static_cast<std::uint8_t>(
            (bits >> (index * 8U)) & 0xffU);
    }
    return bytes;
}

std::size_t find_payload_value(
    const std::vector<std::uint8_t>& bytes,
    double value) {
    constexpr std::size_t kPayloadOffset =
        8 + 4 + 8 + 64;
    const auto needle = encoded_real(value);
    const auto found = std::search(
        bytes.begin() +
            static_cast<std::ptrdiff_t>(kPayloadOffset),
        bytes.end(), needle.begin(), needle.end());
    if (found == bytes.end()) {
        throw std::runtime_error(
            "serialized real value was not found");
    }
    return static_cast<std::size_t>(
        std::distance(bytes.begin(), found));
}

void test_parameters_and_diagnostics_cannot_be_invented() {
    const auto& artifact = test_artifact();
    const std::filesystem::path source =
        test_directory() / "owned-fit-source.bin";
    const std::filesystem::path parameter_path =
        test_directory() / "owned-fit-parameter.bin";
    const std::filesystem::path diagnostic_path =
        test_directory() / "owned-fit-diagnostic.bin";
    for (const auto& path :
         {source, parameter_path, diagnostic_path}) {
        remove_test_file(path);
    }
    oc::write_output_calibration_artifact_atomic_no_replace(
        source.string(), artifact);
    const auto source_bytes = read_file(source);

    std::size_t changed_leaf = 0;
    std::size_t changed_parameter = 0;
    bool found_change = false;
    for (std::size_t leaf = 0;
         leaf <
         artifact.report().output_parameters.after.leaves.size();
         ++leaf) {
        for (std::size_t parameter = 0;
             parameter < artifact.report()
                             .output_parameters.after
                             .leaves[leaf]
                             .size();
             ++parameter) {
            if (std::bit_cast<std::uint64_t>(
                    artifact.report()
                        .output_parameters.before
                        .leaves[leaf][parameter]) !=
                std::bit_cast<std::uint64_t>(
                    artifact.report()
                        .output_parameters.after
                        .leaves[leaf][parameter])) {
                changed_leaf = leaf;
                changed_parameter = parameter;
                found_change = true;
                break;
            }
        }
        if (found_change) {
            break;
        }
    }
    expect(
        found_change,
        "tiny real calibration made no output change");

    auto changed = source_bytes;
    const double stored_parameter =
        artifact.report().output_parameters.after
            .leaves[changed_leaf][changed_parameter];
    changed[find_payload_value(
        changed, stored_parameter)] ^= 0x01U;
    rewrite_payload_checksum(changed);
    write_file(parameter_path, changed);
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::load_output_calibration_artifact(
                    parameter_path.string(),
                    verified_parent(),
                    test_collection_config()));
        },
        "fingerprint mismatch");

    changed = source_bytes;
    changed[find_payload_value(
        changed,
        artifact.report()
            .optimizer_diagnostics
            .before_weighted_bce)] ^= 0x01U;
    rewrite_payload_checksum(changed);
    write_file(diagnostic_path, changed);
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::load_output_calibration_artifact(
                    diagnostic_path.string(),
                    verified_parent(),
                    test_collection_config()));
        },
        "hidden-refit evidence");

    for (const auto& path :
         {source, parameter_path, diagnostic_path}) {
        remove_test_file(path);
    }
}

void test_cache_path_is_exact() {
    expect(
        oc::output_calibration_cache_path(
            800, 424242, 111222333ULL) ==
            "build/model-cache/"
            "old-school-value-output-calibration-v1-c16-"
            "t800-p424242-f111222333.bin",
        "OC1 cache path changed");
    expect_throws_contains(
        [] {
            static_cast<void>(
                oc::output_calibration_cache_path(
                    0, 1, 2));
        },
        "positive");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "roundtrip and no overwrite",
        test_roundtrip_and_no_overwrite);
    runner.run(
        "corruption trailing and family rejection",
        test_corruption_trailing_and_wrong_family_fail);
    runner.run(
        "exact load expectations",
        test_exact_parent_and_configuration_are_required);
    runner.run(
        "tensor ledger is rechecked",
        test_rechecks_tensor_ledger_after_valid_checksum);
    runner.run(
        "family metadata is rechecked",
        test_internal_family_metadata_is_rechecked);
    runner.run(
        "builder owns and reproduces fit",
        test_builder_reproduces_corpus_and_owns_fit);
    runner.run(
        "parameters and diagnostics cannot be invented",
        test_parameters_and_diagnostics_cannot_be_invented);
    runner.run(
        "cache path",
        test_cache_path_is_exact);
    return runner.finish();
}
