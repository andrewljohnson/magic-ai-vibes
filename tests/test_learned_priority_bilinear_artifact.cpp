#include "old_school/artifact_integrity.hpp"
#include "old_school/learned_priority_bilinear_artifact.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>

namespace {

namespace artifact =
    old_school::learned_priority_bilinear_artifact;

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
            std::cerr << failed_ << " failed, "
                      << passed_ << " passed\n";
            return 1;
        }
        std::cout << passed_
                  << " AQ19 artifact tests passed\n";
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
        path_ =
            std::filesystem::temp_directory_path() /
            ("old-school-aq19-artifact-tests-" +
             std::to_string(
                 static_cast<unsigned long long>(
                     ::getpid())));
        if (!std::filesystem::create_directory(path_)) {
            throw std::runtime_error(
                "temporary test directory already exists");
        }
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(
        const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code error;
        for (const auto& entry :
             std::filesystem::directory_iterator(
                 path_, error)) {
            std::filesystem::remove(
                entry.path(), error);
            error.clear();
        }
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

old_school::LearnedPriorityBilinearParameters
parameters() {
    old_school::LearnedPriorityBilinearParameters result;
    result.delta_u[0][0] = 0.125;
    result.delta_u[1][17] = -0.25;
    result.v[0][3] = 0.5;
    result.v[1][9] = -0.75;
    return result;
}

artifact::Contract contract_for(
    const old_school::
        LearnedPriorityBilinearParameters& value) {
    return {
        .parent_fingerprint =
            std::string(
                old_school::
                    kLearnedPriorityBilinearRequiredFingerprint),
        .parameter_sha256 =
            old_school::artifact_integrity::sha256_string(
                old_school::
                    learned_priority_bilinear_canonical_bytes(
                        value)),
    };
}

void test_production_contract_is_pinned() {
    const auto& contract =
        artifact::production_contract();
    expect(
        contract.parent_fingerprint ==
                old_school::
                    kLearnedPriorityBilinearRequiredFingerprint &&
            contract.parameter_sha256 ==
                artifact::
                    kProductionParameterSha256 &&
            contract.parameter_sha256 ==
                "3114c898085375b7c39a8d8a7add5b0ab87dc70916d676deccd28d45e0942194" &&
            artifact::kProductionArtifactBytes ==
                14502 &&
            artifact::kProductionFileSha256 ==
                "445f93435aebafbafc16cda4d1faa9e4d56dc12a25196f79c1334fcc84d22c1a",
        "AQ19 production artifact contract drifted");
}

void test_roundtrip_and_no_replace() {
    TemporaryDirectory directory;
    const auto fitted = parameters();
    const auto contract = contract_for(fitted);
    const auto path =
        directory.path() / "candidate.bin";
    const artifact::Identity published =
        artifact::publish_atomic_no_replace(
            path, fitted, contract);
    const artifact::Loaded loaded =
        artifact::load(path, contract);
    expect(
        published == loaded.identity &&
            published.bytes > 0 &&
            published.file_sha256.size() == 64 &&
            published.parameter_sha256 ==
                contract.parameter_sha256 &&
            loaded.residual &&
            loaded.residual->parameters() == fitted,
        "AQ19 artifact did not round-trip exactly");
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::publish_atomic_no_replace(
                    path, fitted, contract));
        },
        "AQ19 artifact publication replaced an existing file");
}

void test_corruption_and_contract_drift_fail_closed() {
    TemporaryDirectory directory;
    const auto fitted = parameters();
    const auto contract = contract_for(fitted);
    const auto source =
        directory.path() / "source.bin";
    static_cast<void>(
        artifact::publish_atomic_no_replace(
            source, fitted, contract));

    auto wrong_contract = contract;
    wrong_contract.parameter_sha256[0] =
        wrong_contract.parameter_sha256[0] == '0'
            ? '1'
            : '0';
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::load(
                    source, wrong_contract));
        },
        "AQ19 artifact accepted the wrong expected digest");

    const auto corrupted =
        directory.path() / "corrupted.bin";
    std::filesystem::copy_file(source, corrupted);
    std::fstream stream(
        corrupted,
        std::ios::binary |
            std::ios::in |
            std::ios::out);
    stream.seekg(-1, std::ios::end);
    char final_byte = '\0';
    stream.get(final_byte);
    stream.seekp(-1, std::ios::end);
    final_byte =
        static_cast<char>(
            static_cast<unsigned char>(final_byte) ^
            0x01U);
    stream.put(final_byte);
    stream.close();
    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::load(
                    corrupted, contract));
        },
        "AQ19 artifact accepted corrupted parameter bytes");

    expect_rejected(
        [&] {
            static_cast<void>(
                artifact::load(
                    directory.path() / "missing.bin",
                    contract));
        },
        "AQ19 artifact accepted a missing file");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "production contract is pinned",
        test_production_contract_is_pinned);
    runner.run(
        "round-trip and no-replace publication",
        test_roundtrip_and_no_replace);
    runner.run(
        "corruption and contract drift fail closed",
        test_corruption_and_contract_drift_fail_closed);
    return runner.finish();
}
