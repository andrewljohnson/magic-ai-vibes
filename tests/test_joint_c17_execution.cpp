#include "old_school/artifact_integrity.hpp"
#include "old_school/joint_c17_execution.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace execution = old_school::joint_c17_execution;
namespace integrity = old_school::artifact_integrity;

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
                  << " joint-C17 execution tests passed\n";
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

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        static std::size_t sequence = 0;
        path_ =
            std::filesystem::temp_directory_path() /
            ("old-school-joint-c17-execution-tests-" +
             std::to_string(++sequence));
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        if (!std::filesystem::create_directories(path_)) {
            throw std::runtime_error(
                "could not create temporary test directory");
        }
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

void write_bytes(
    const std::filesystem::path& path,
    std::string_view bytes) {
    std::ofstream output(
        path, std::ios::binary | std::ios::trunc);
    output.write(
        bytes.data(),
        static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) {
        throw std::runtime_error(
            "could not write temporary artifact");
    }
}

execution::testing::ExpectedRegularFile identity_for(
    const std::filesystem::path& path) {
    const auto snapshot =
        integrity::snapshot_regular_file(path);
    return {
        .byte_size = snapshot.byte_size,
        .sha256 = snapshot.sha256,
    };
}

execution::testing::CanonicalArtifactLocations
fixture_locations(const TemporaryDirectory& directory) {
    const auto bundle = directory.path() / "joint.bin";
    const auto parent = directory.path() / "parent.bin";
    const auto labels = directory.path() / "labels.tsv";
    write_bytes(bundle, "not a joint C17 bundle");
    write_bytes(parent, "not a frozen C16 model");
    write_bytes(labels, "not a probe label cache");
    return {
        .bundle = bundle,
        .parent = parent,
        .label_cache = labels,
    };
}

void expect_load_error(
    const execution::testing::CanonicalArtifactLocations&
        locations,
    std::string_view needle) {
    std::ostringstream progress;
    try {
        static_cast<void>(
            execution::testing::
                load_canonical_joint_c17_context(
                    locations, progress));
    } catch (const std::exception& error) {
        expect(
            std::string_view(error.what()).find(needle) !=
                std::string_view::npos,
            "load failed for an unexpected reason");
        return;
    }
    throw std::runtime_error(
        "unrelated artifact set unexpectedly loaded");
}

void test_canonical_label_contract_is_exact() {
    expect(
        execution::kCanonicalLabelWorlds == 64,
        "label worlds");
    expect(
        execution::kCanonicalLabelHorizonTurns == 8,
        "label horizon");
    expect(
        execution::kCanonicalLabelRolloutsPerWorld == 1,
        "label rollouts");
    expect(
        execution::
                kCanonicalLabelReferenceModelFingerprint
            ==
            "dd58d3814f46d6661d40690f6ad7ac73226c2160137b2e42bfadf3e6ac7a1b72",
        "label reference fingerprint");
    expect(
        execution::
                kCanonicalLabelInformationSetFingerprint
            == "cf4729a535378a12",
        "label corpus fingerprint");
}

void test_unrelated_parent_bytes_cannot_bind() {
    TemporaryDirectory directory;
    auto locations = fixture_locations(directory);
    locations.bundle_identity =
        identity_for(locations.bundle);
    locations.label_cache_identity =
        identity_for(locations.label_cache);
    expect_load_error(locations, "frozen C16 parent");
}

void test_unrelated_label_bytes_cannot_bind() {
    TemporaryDirectory directory;
    auto locations = fixture_locations(directory);
    locations.bundle_identity =
        identity_for(locations.bundle);
    locations.parent_identity =
        identity_for(locations.parent);
    expect_load_error(
        locations, "frozen Dev-v3 label cache");
}

void test_unrelated_bundle_bytes_cannot_bind() {
    TemporaryDirectory directory;
    auto locations = fixture_locations(directory);
    locations.bundle_identity =
        identity_for(locations.bundle);
    locations.parent_identity =
        identity_for(locations.parent);
    locations.label_cache_identity =
        identity_for(locations.label_cache);
    const auto bundle_before =
        integrity::snapshot_regular_file(locations.bundle);
    const auto parent_before =
        integrity::snapshot_regular_file(locations.parent);
    const auto labels_before =
        integrity::snapshot_regular_file(
            locations.label_cache);
    expect_load_error(locations, "joint C17 artifact");
    expect(
        integrity::snapshot_regular_file(locations.bundle) ==
            bundle_before,
        "failed load changed bundle bytes or identity");
    expect(
        integrity::snapshot_regular_file(locations.parent) ==
            parent_before,
        "failed load changed parent bytes or identity");
    expect(
        integrity::snapshot_regular_file(
            locations.label_cache) == labels_before,
        "failed load changed label bytes or identity");
}

void test_prepinned_wrong_bundle_digest_cannot_bind() {
    TemporaryDirectory directory;
    auto locations = fixture_locations(directory);
    const auto actual = identity_for(locations.bundle);
    locations.bundle_identity =
        execution::testing::ExpectedRegularFile{
            .byte_size = actual.byte_size,
            .sha256 = std::string(64, '0'),
        };
    locations.parent_identity =
        identity_for(locations.parent);
    locations.label_cache_identity =
        identity_for(locations.label_cache);
    expect_load_error(locations, "joint bundle SHA-256");
}

void test_production_path_mode_rejects_relocation() {
    TemporaryDirectory directory;
    auto locations = fixture_locations(directory);
    locations.bundle_identity =
        identity_for(locations.bundle);
    locations.parent_identity =
        identity_for(locations.parent);
    locations.label_cache_identity =
        identity_for(locations.label_cache);
    locations.require_canonical_path_spellings = true;
    expect_load_error(
        locations, "production artifact path spelling");
}

void test_final_component_symlink_is_rejected() {
    TemporaryDirectory directory;
    auto locations = fixture_locations(directory);
    const auto symlink =
        directory.path() / "joint-link.bin";
    std::filesystem::create_symlink(
        locations.bundle.filename(), symlink);
    locations.bundle = symlink;
    expect_load_error(locations, "symlink");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "canonical label contract",
        test_canonical_label_contract_is_exact);
    tests.run(
        "unrelated parent rejected",
        test_unrelated_parent_bytes_cannot_bind);
    tests.run(
        "unrelated labels rejected",
        test_unrelated_label_bytes_cannot_bind);
    tests.run(
        "unrelated bundle rejected",
        test_unrelated_bundle_bytes_cannot_bind);
    tests.run(
        "prepinned bundle digest rejected",
        test_prepinned_wrong_bundle_digest_cannot_bind);
    tests.run(
        "production path relocation rejected",
        test_production_path_mode_rejects_relocation);
    tests.run(
        "final symlink rejected",
        test_final_component_symlink_is_rejected);
    return tests.finish();
}
