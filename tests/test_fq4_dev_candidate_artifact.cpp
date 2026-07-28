#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_dev_candidate_artifact.hpp"
#include "old_school/game.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace artifact =
    old_school::fq4_dev_candidate_artifact;
namespace integrity = old_school::artifact_integrity;

class TestRunner {
  public:
    template <typename Function>
    void run(std::string_view name, Function function) {
        try {
            function();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cout << "[FAIL] " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " passed, " << failed_
                  << " failed\n";
        return failed_ == 0 ? 0 : 1;
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
void expect_rejected(
    Function function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        std::string pattern =
            (std::filesystem::temp_directory_path() /
             "old-school-fq4-candidate-XXXXXX")
                .string();
        std::vector<char> buffer(
            pattern.begin(), pattern.end());
        buffer.push_back('\0');
        char* const created = ::mkdtemp(buffer.data());
        if (created == nullptr) {
            throw std::system_error(
                errno, std::generic_category(),
                "cannot create temporary directory");
        }
        path_ = created;
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(
        const TemporaryDirectory&) = delete;

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

std::string read_file(
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
    std::string_view bytes) {
    std::ofstream output(
        path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write test artifact");
    }
    output.write(
        bytes.data(),
        static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("test artifact write failed");
    }
}

artifact::FileIdentity identity(
    const std::filesystem::path& path) {
    const auto snapshot =
        integrity::snapshot_regular_file(path);
    return {
        .bytes =
            static_cast<std::uint64_t>(
                snapshot.byte_size),
        .sha256 = snapshot.sha256,
    };
}

std::uint32_t read_u32(
    std::string_view bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("test u32 read overflow");
    }
    std::uint32_t result = 0;
    for (std::size_t byte = 0; byte < 4; ++byte) {
        result |=
            static_cast<std::uint32_t>(
                static_cast<unsigned char>(
                    bytes[offset + byte]))
            << (8U * byte);
    }
    return result;
}

std::uint64_t read_u64(
    std::string_view bytes, std::size_t offset) {
    if (offset + 8 > bytes.size()) {
        throw std::runtime_error("test u64 read overflow");
    }
    std::uint64_t result = 0;
    for (std::size_t byte = 0; byte < 8; ++byte) {
        result |=
            static_cast<std::uint64_t>(
                static_cast<unsigned char>(
                    bytes[offset + byte]))
            << (8U * byte);
    }
    return result;
}

void write_u32(
    std::string& bytes, std::size_t offset,
    std::uint32_t value) {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("test u32 write overflow");
    }
    for (std::size_t byte = 0; byte < 4; ++byte) {
        bytes[offset + byte] = static_cast<char>(
            (value >> (8U * byte)) & 0xffU);
    }
}

void write_u64(
    std::string& bytes, std::size_t offset,
    std::uint64_t value) {
    if (offset + 8 > bytes.size()) {
        throw std::runtime_error("test u64 write overflow");
    }
    for (std::size_t byte = 0; byte < 8; ++byte) {
        bytes[offset + byte] = static_cast<char>(
            (value >> (8U * byte)) & 0xffU);
    }
}

std::string raw_sha256(std::string_view hexadecimal) {
    expect(hexadecimal.size() == 64, "bad test SHA-256");
    const auto nibble = [](char character) {
        return static_cast<unsigned char>(
            character <= '9'
                ? character - '0'
                : character - 'a' + 10);
    };
    std::string raw(32, '\0');
    for (std::size_t byte = 0; byte < 32; ++byte) {
        raw[byte] = static_cast<char>(
            (nibble(hexadecimal[2 * byte]) << 4U) |
            nibble(hexadecimal[2 * byte + 1]));
    }
    return raw;
}

struct Framing {
    std::size_t schema_offset = 0;
    std::size_t payload_size_offset = 0;
    std::size_t payload_hash_offset = 0;
    std::size_t payload_offset = 0;
    std::size_t payload_size = 0;
};

Framing framing(std::string_view bytes) {
    constexpr std::size_t kPrefix = 8 + 4 + 4;
    const std::uint32_t schema_size =
        read_u32(bytes, kPrefix);
    Framing result;
    result.schema_offset = kPrefix + 4;
    result.payload_size_offset =
        result.schema_offset + schema_size;
    result.payload_hash_offset =
        result.payload_size_offset + 8;
    result.payload_offset =
        result.payload_hash_offset + 32;
    result.payload_size = static_cast<std::size_t>(
        read_u64(bytes, result.payload_size_offset));
    expect(
        result.payload_offset + result.payload_size ==
            bytes.size(),
        "bad test framing");
    return result;
}

void refresh_payload_sha256(std::string& bytes) {
    const Framing frame = framing(bytes);
    const std::string hash = raw_sha256(
        integrity::sha256_string(
            std::string_view(bytes).substr(
                frame.payload_offset,
                frame.payload_size)));
    bytes.replace(
        frame.payload_hash_offset, hash.size(), hash);
}

std::size_t find_unique(
    std::string_view haystack,
    std::string_view needle) {
    const std::size_t position = haystack.find(needle);
    expect(position != std::string_view::npos,
           "test pattern is absent");
    expect(
        haystack.find(needle, position + 1) ==
            std::string_view::npos,
        "test pattern is not unique");
    return position;
}

std::string little_endian_u64(std::uint64_t value) {
    std::string result(8, '\0');
    write_u64(result, 0, value);
    return result;
}

struct Fixture {
    std::shared_ptr<const old_school::LearnedModel> parent;
    std::shared_ptr<const old_school::LearnedModel> candidate;
    artifact::Contract contract;
    std::string parent_fingerprint;
    old_school::LearnedModelComponentFingerprints
        parent_components;
    old_school::LearnedModelComponentFingerprints
        candidate_components;
};

const Fixture& fixture() {
    static const Fixture value = [] {
        Fixture result;
        result.parent =
            old_school::train_learned_value_champion(
                1, 0xF4CA7D1DULL);
        result.parent_fingerprint =
            old_school::learned_model_fingerprint(
                result.parent);
        result.parent_components =
            old_school::learned_model_component_fingerprints(
                result.parent);
        auto parameters =
            old_school::learned_priority_head_parameters(
                result.parent);
        expect(
            parameters.input_hidden.size() == 32,
            "unexpected Priority hidden count");
        expect(
            parameters.direct.size() == 893,
            "unexpected Priority feature count");
        const std::size_t parameter_count =
            parameters.input_hidden.size() *
                parameters.direct.size() +
            parameters.hidden_bias.size() +
            parameters.hidden_output.size() +
            parameters.direct.size() + 1;
        expect(
            parameter_count == 29'534,
            "unexpected Priority parameter count");
        parameters.input_hidden[0][0] =
            std::bit_cast<double>(
                UINT64_C(0x3fd123456789abcd));
        parameters.hidden_bias[0] =
            std::bit_cast<double>(
                UINT64_C(0xbfc23456789abcde));
        parameters.hidden_output[0] =
            std::bit_cast<double>(
                UINT64_C(0x0010000000000001));
        parameters.direct[0] =
            std::bit_cast<double>(
                UINT64_C(0x8000000000000000));
        parameters.output_bias =
            std::bit_cast<double>(
                UINT64_C(0x3fe3456789abcdef));
        result.candidate =
            old_school::
                with_learned_priority_head_parameters(
                    result.parent, parameters);
        result.candidate_components =
            old_school::learned_model_component_fingerprints(
                result.candidate);

        result.contract = {
            .family = "synthetic-fq4-candidate",
            .environment = "synthetic-five-deck",
            .parent = {
                .artifact_bytes = 123,
                .artifact_sha256 =
                    integrity::sha256_string(
                        "synthetic parent artifact"),
                .model_fingerprint =
                    result.parent_fingerprint,
                .components =
                    result.parent_components,
                .training_games = 1,
                .training_seed = 0xF4CA7D1DULL,
                .generation = 1,
            },
            .corpus = {
                .artifact_bytes = 456,
                .artifact_sha256 =
                    integrity::sha256_string(
                        "synthetic corpus"),
            },
            .fit = {
                .input_sha256 =
                    integrity::sha256_string(
                        "synthetic FIT input"),
                .examples = 2,
                .options = 5,
                .check_examples = 0,
                .background_only_examples = 0,
                .optimizer_calls = 1,
                .optimizer = {
                    .batch_size = 2,
                    .epochs = 3,
                    .learning_rate = 0.001,
                    .beta1 = 0.9,
                    .beta2 = 0.999,
                    .epsilon = 1.0e-8,
                    .global_gradient_norm_clip = 5.0,
                    .seed = 999,
                    .residual_weight = 0.10,
                    .policy_temperature = 0.10,
                },
            },
            .candidate_model_fingerprint =
                old_school::learned_model_fingerprint(
                    result.candidate),
            .priority_hidden_count = 32,
            .priority_feature_count = 893,
            .priority_parameter_count = 29'534,
            .deployment = {
                .variant =
                    old_school::LearnedVariant::
                        ValueSearchChampion,
                .training_games = 1,
                .worlds_per_action = 2,
                .horizon_turns = 1,
                .rollouts_per_world = 1,
                .root_search_depth = 1,
                .shallow_prior = true,
                .root_exploration = 0.0,
                .continuation_epsilon = 0.0,
                .priority_residual_weight = 0.10,
                .pass_dominance = false,
                .continuation_controller =
                    old_school::
                        LearnedContinuationController::
                            Legacy,
                .max_turns = 12,
            },
        };
        return result;
    }();
    return value;
}

artifact::Report publish(
    const std::filesystem::path& path) {
    const Fixture& test = fixture();
    return artifact::publish_atomic_no_replace(
        path, test.parent, test.candidate, test.contract);
}

void test_production_contract_is_complete_and_exact() {
    const auto& contract = artifact::production_contract();
    expect(contract.family == "FQ4-DEV1",
           "production family drifted");
    expect(
        contract.environment ==
            "old-school-environment-v3-cleanup-discard",
        "production environment drifted");
    expect(contract.parent.artifact_bytes == 3'111'437,
           "parent byte count drifted");
    expect(
        contract.parent.artifact_sha256 ==
            "53aeb904bd87311b37201859317f05ab0"
            "66bdfe134c72460cf94bff6d1f944ca",
        "parent artifact hash drifted");
    expect(
        contract.parent.model_fingerprint ==
            "68126afc5a3e3757eb1d510a056585aa9"
            "74c4f54ce1b4a789ff430f1c7413e2f",
        "parent fingerprint drifted");
    expect(
        contract.parent.training_games == 800 &&
            contract.parent.training_seed == 424242 &&
            contract.parent.generation == 16,
        "parent T/S/g drifted");
    expect(contract.corpus.artifact_bytes == 2'250'909,
           "corpus byte count drifted");
    expect(
        contract.corpus.artifact_sha256 ==
            "0911fc2eb8b14ddc9165543eb1e4c4ed"
            "b0b058256a58dedf61f6c4ea4ca859df",
        "corpus hash drifted");
    expect(
        contract.fit.input_sha256 ==
            "586b121c3c9bdb1a61305cac86882cd2"
            "0b5d2ba332b4d5a54defc2c7756393a1",
        "FIT input hash drifted");
    expect(
        contract.fit.examples == 88 &&
            contract.fit.options == 548 &&
            contract.fit.check_examples == 0 &&
            contract.fit.background_only_examples == 0 &&
            contract.fit.optimizer_calls == 1,
        "FIT boundary counts drifted");
    const auto& optimizer = contract.fit.optimizer;
    expect(
        optimizer.batch_size == 64 &&
            optimizer.epochs == 16 &&
            optimizer.learning_rate == 0.001 &&
            optimizer.beta1 == 0.9 &&
            optimizer.beta2 == 0.999 &&
            optimizer.epsilon == 1.0e-8 &&
            optimizer.global_gradient_norm_clip == 5.0 &&
            optimizer.seed == 202607280212ULL &&
            optimizer.residual_weight == 0.10 &&
            optimizer.policy_temperature == 0.10,
        "optimizer recipe drifted");
    expect(
        contract.candidate_model_fingerprint ==
            "712600783152e89ff1a53394149764db2"
            "27e55289a656530342226b7e1ee6151",
        "candidate fingerprint drifted");
    expect(
        contract.priority_hidden_count == 32 &&
            contract.priority_feature_count == 893 &&
            contract.priority_parameter_count == 29'534,
        "Priority layout drifted");
    const auto& deployment = contract.deployment;
    expect(
        deployment.variant ==
                old_school::LearnedVariant::
                    ValueSearchChampion &&
            deployment.training_games == 800 &&
            deployment.worlds_per_action == 8 &&
            deployment.horizon_turns == 4 &&
            deployment.rollouts_per_world == 1 &&
            deployment.root_search_depth == 1 &&
            deployment.shallow_prior &&
            deployment.root_exploration == 0.0 &&
            deployment.continuation_epsilon == 0.0 &&
            deployment.priority_residual_weight == 0.10 &&
            !deployment.pass_dominance &&
            deployment.continuation_controller ==
                old_school::
                    LearnedContinuationController::Legacy &&
            deployment.max_turns == 500,
        "deployment recipe drifted");
    expect(
        artifact::production_artifact_path() ==
            std::filesystem::path(
                artifact::kProductionArtifactPath),
        "production path drifted");
    expect(
        artifact::production_temporary_path() ==
            artifact::temporary_path_for(
                artifact::production_artifact_path()),
        "production temporary path drifted");
}

void test_deterministic_publish_and_exact_reload() {
    TemporaryDirectory directory;
    const auto first_path =
        directory.path() / "first.fq4candidate";
    const auto second_path =
        directory.path() / "second.fq4candidate";
    const Fixture& test = fixture();
    const std::string parent_before =
        old_school::learned_model_fingerprint(test.parent);
    const auto parent_components_before =
        old_school::learned_model_component_fingerprints(
            test.parent);

    const artifact::Report first = publish(first_path);
    const artifact::Report second = publish(second_path);
    const std::string first_bytes = read_file(first_path);
    const std::string second_bytes = read_file(second_path);
    expect(first_bytes == second_bytes,
           "publication bytes are nondeterministic");
    expect(first.artifact == second.artifact,
           "publication identity is nondeterministic");
    expect(first.manifest == second.manifest,
           "publication manifest is nondeterministic");
    expect(first.artifact == identity(first_path),
           "reported file identity is wrong");
    expect(
        !std::filesystem::exists(
            artifact::temporary_path_for(first_path)),
        "successful publication left its temporary");
    expect(
        first.manifest.candidate_components ==
            test.candidate_components,
        "report omitted candidate components");
    expect(
        first.manifest.tensors.hidden_count == 32 &&
            first.manifest.tensors.feature_count == 893 &&
            first.manifest.tensors.parameter_count == 29'534,
        "manifest tensor layout is wrong");
    expect(
        first.manifest.tensors.parent_sha256 !=
            first.manifest.tensors.candidate_sha256,
        "tensor hashes do not distinguish candidate");

    const auto loaded = artifact::load(
        first_path, test.parent, test.contract,
        first.artifact);
    expect(
        old_school::learned_model_fingerprint(
            loaded.model()) ==
            test.contract.candidate_model_fingerprint,
        "reloaded model fingerprint changed");
    expect(
        old_school::learned_model_component_fingerprints(
            loaded.model()) == test.candidate_components,
        "reloaded components changed");
    expect(loaded.manifest() == first.manifest,
           "reloaded manifest changed");
    expect(loaded.report() == first,
           "reloaded report changed");
    expect(
        old_school::learned_model_fingerprint(test.parent) ==
                parent_before &&
            old_school::learned_model_component_fingerprints(
                test.parent) == parent_components_before,
        "publish or load mutated parent");

    const Framing frame = framing(first_bytes);
    expect(
        static_cast<unsigned char>(first_bytes[12]) == 0x04U &&
            static_cast<unsigned char>(first_bytes[13]) ==
                0x03U &&
            static_cast<unsigned char>(first_bytes[14]) ==
                0x02U &&
            static_cast<unsigned char>(first_bytes[15]) ==
                0x01U,
        "endian marker is not little-endian");
    expect(
        first_bytes.substr(
            frame.schema_offset,
            artifact::kSchema.size()) ==
            artifact::kSchema,
        "wire schema text is wrong");
}

void test_no_replace_destination_temporary_and_symlink() {
    TemporaryDirectory directory;
    const auto path =
        directory.path() / "candidate.fq4candidate";
    const artifact::Report report = publish(path);
    const std::string original = read_file(path);
    expect_rejected(
        [&] { static_cast<void>(publish(path)); },
        "existing destination was replaced");
    expect(read_file(path) == original,
           "no-replace failure mutated destination");

    const auto stale_path =
        directory.path() / "stale.fq4candidate";
    const auto stale_temporary =
        artifact::temporary_path_for(stale_path);
    write_file(stale_temporary, "stale temporary");
    expect_rejected(
        [&] { static_cast<void>(publish(stale_path)); },
        "stale deterministic temporary was accepted");
    expect(!std::filesystem::exists(stale_path),
           "stale temporary failure published destination");
    expect(read_file(stale_temporary) == "stale temporary",
           "stale temporary was mutated");

    const auto referent = directory.path() / "referent";
    const auto symlink_path =
        directory.path() / "symlink.fq4candidate";
    write_file(referent, "referent bytes");
    std::filesystem::create_symlink(
        referent, symlink_path);
    expect_rejected(
        [&] { static_cast<void>(publish(symlink_path)); },
        "destination symlink was followed");
    expect(read_file(referent) == "referent bytes",
           "symlink rejection mutated referent");
    expect(std::filesystem::is_symlink(symlink_path),
           "symlink rejection replaced link");
    expect_rejected(
        [&] {
            static_cast<void>(artifact::load(
                symlink_path, fixture().parent,
                fixture().contract, report.artifact));
        },
        "loader followed a final symlink");
}

void test_wire_integrity_and_bounds_fail_closed() {
    TemporaryDirectory directory;
    const auto good_path =
        directory.path() / "good.fq4candidate";
    const artifact::Report good = publish(good_path);
    const std::string original = read_file(good_path);
    const Framing original_frame = framing(original);
    std::size_t case_index = 0;
    const auto rejected_bytes =
        [&](std::string bytes, std::string_view message) {
            const auto path =
                directory.path() /
                ("corrupt-" +
                 std::to_string(case_index++) +
                 ".fq4candidate");
            write_file(path, bytes);
            const auto corrupted_identity = identity(path);
            expect_rejected(
                [&] {
                    static_cast<void>(artifact::load(
                        path, fixture().parent,
                        fixture().contract,
                        corrupted_identity));
                },
                message);
        };

    auto corrupt = original;
    corrupt.pop_back();
    rejected_bytes(corrupt, "truncation was accepted");
    corrupt = original;
    corrupt.push_back('\0');
    rejected_bytes(corrupt, "trailing byte was accepted");
    corrupt = original;
    corrupt[0] ^= 0x01;
    rejected_bytes(corrupt, "bad magic was accepted");
    corrupt = original;
    write_u32(corrupt, 8, 2);
    rejected_bytes(corrupt, "bad schema version was accepted");
    corrupt = original;
    write_u32(corrupt, 12, 0x04030201U);
    rejected_bytes(corrupt, "bad endian marker was accepted");
    corrupt = original;
    corrupt[original_frame.schema_offset] ^= 0x01;
    rejected_bytes(corrupt, "bad schema name was accepted");
    corrupt = original;
    corrupt.back() ^= 0x01;
    rejected_bytes(
        corrupt, "payload corruption was accepted");

    corrupt = original;
    write_u32(
        corrupt, original_frame.payload_offset, 257);
    refresh_payload_sha256(corrupt);
    rejected_bytes(
        corrupt, "oversized text length was accepted");

    corrupt = original;
    const std::string learning_rate =
        little_endian_u64(
            std::bit_cast<std::uint64_t>(0.001));
    const std::size_t learning_rate_offset =
        find_unique(corrupt, learning_rate);
    write_u64(
        corrupt, learning_rate_offset,
        UINT64_C(0x7ff8000000000001));
    refresh_payload_sha256(corrupt);
    rejected_bytes(
        corrupt, "nonfinite binary64 was accepted");

    corrupt = original;
    const std::size_t masks_bytes =
        static_cast<std::size_t>(
            good.manifest.tensors.parameter_count) *
        sizeof(std::uint64_t);
    const std::size_t delta_count_offset =
        corrupt.size() - masks_bytes -
        sizeof(std::uint64_t);
    write_u64(
        corrupt, delta_count_offset, 1'100'001);
    refresh_payload_sha256(corrupt);
    rejected_bytes(
        corrupt, "oversized delta count was accepted");

    corrupt = original;
    const std::string candidate_priority =
        raw_sha256(
            good.manifest.candidate_components.priority);
    const std::size_t candidate_component_offset =
        find_unique(corrupt, candidate_priority);
    corrupt[candidate_component_offset] ^= 0x01;
    refresh_payload_sha256(corrupt);
    rejected_bytes(
        corrupt,
        "stored candidate component mismatch was accepted");

    auto wrong_hash = good.artifact;
    wrong_hash.sha256[0] =
        wrong_hash.sha256[0] == '0' ? '1' : '0';
    expect_rejected(
        [&] {
            static_cast<void>(artifact::load(
                good_path, fixture().parent,
                fixture().contract, wrong_hash));
        },
        "wrong externally pinned hash was accepted");
    auto wrong_bytes = good.artifact;
    ++wrong_bytes.bytes;
    expect_rejected(
        [&] {
            static_cast<void>(artifact::load(
                good_path, fixture().parent,
                fixture().contract, wrong_bytes));
        },
        "wrong externally pinned byte count was accepted");
}

void test_contract_parent_candidate_and_fingerprints_fail_closed() {
    TemporaryDirectory directory;
    const auto good_path =
        directory.path() / "good.fq4candidate";
    const artifact::Report good = publish(good_path);
    const Fixture& test = fixture();

    auto wrong_contract = test.contract;
    wrong_contract.environment = "different-environment";
    expect_rejected(
        [&] {
            static_cast<void>(artifact::load(
                good_path, test.parent, wrong_contract,
                good.artifact));
        },
        "wrong expected contract was accepted");

    const auto wrong_parent =
        old_school::train_learned_value_champion(
            1, 0xF4CA7D1EULL);
    expect_rejected(
        [&] {
            static_cast<void>(artifact::load(
                good_path, wrong_parent, test.contract,
                good.artifact));
        },
        "wrong parent was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(artifact::load(
                good_path, nullptr, test.contract,
                good.artifact));
        },
        "null parent was accepted by loader");

    auto bad = test.contract;
    bad.candidate_model_fingerprint =
        integrity::sha256_string("wrong candidate");
    const auto bad_path =
        directory.path() / "bad-fingerprint.fq4candidate";
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::publish_atomic_no_replace(
                    bad_path, test.parent, test.candidate,
                    bad));
        },
        "wrong candidate fingerprint was accepted");
    expect(!std::filesystem::exists(bad_path),
           "rejected fingerprint published a file");

    bad = test.contract;
    bad.fit.optimizer.learning_rate =
        std::numeric_limits<double>::infinity();
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::publish_atomic_no_replace(
                    directory.path() / "nonfinite",
                    test.parent, test.candidate, bad));
        },
        "nonfinite contract was accepted");
    bad = test.contract;
    ++bad.priority_parameter_count;
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::publish_atomic_no_replace(
                    directory.path() / "bad-layout",
                    test.parent, test.candidate, bad));
        },
        "inconsistent tensor dimensions were accepted");
    bad = test.contract;
    bad.deployment.root_search_depth = 0;
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::publish_atomic_no_replace(
                    directory.path() / "bad-depth",
                    test.parent, test.candidate, bad));
        },
        "zero root search depth was accepted");

    const auto unrelated_candidate =
        old_school::train_learned_value_champion(
            1, 0xF4CA7D1FULL);
    bad = test.contract;
    bad.candidate_model_fingerprint =
        old_school::learned_model_fingerprint(
            unrelated_candidate);
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::publish_atomic_no_replace(
                    directory.path() / "nonisolated",
                    test.parent, unrelated_candidate, bad));
        },
        "non-Priority model changes were accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::publish_atomic_no_replace(
                    directory.path() / "null-parent",
                    nullptr, test.candidate, test.contract));
        },
        "null publisher parent was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::publish_atomic_no_replace(
                    directory.path() / "null-candidate",
                    test.parent, nullptr, test.contract));
        },
        "null publisher candidate was accepted");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "production contract is complete and exact",
        test_production_contract_is_complete_and_exact);
    runner.run(
        "deterministic publish and exact reload",
        test_deterministic_publish_and_exact_reload);
    runner.run(
        "no replace destination temporary and symlink",
        test_no_replace_destination_temporary_and_symlink);
    runner.run(
        "wire integrity and bounds fail closed",
        test_wire_integrity_and_bounds_fail_closed);
    runner.run(
        "contract parent candidate and fingerprints fail closed",
        test_contract_parent_candidate_and_fingerprints_fail_closed);
    return runner.finish();
}
