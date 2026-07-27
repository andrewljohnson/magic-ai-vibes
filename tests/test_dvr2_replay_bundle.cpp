#include "old_school/artifact_integrity.hpp"
#include "old_school/dvr2_replay_bundle.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace replay = old_school::dvr2_replay_bundle;

namespace {

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
            std::cerr << "[FAIL] " << name << ": "
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
             "old-school-dvr2-replay-XXXXXX")
                .string();
        std::vector<char> buffer(
            pattern.begin(), pattern.end());
        buffer.push_back('\0');
        char* const created = ::mkdtemp(buffer.data());
        if (created == nullptr) {
            throw std::system_error(
                errno, std::generic_category(),
                "cannot create DVR2 replay test directory");
        }
        path_ = created;
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) =
        delete;

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

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open test artifact");
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
        throw std::runtime_error("cannot open DVR2 test file");
    }
    output.write(
        bytes.data(),
        static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("cannot write DVR2 test file");
    }
}

void append_text(
    std::string& output, std::string_view name,
    std::string_view value) {
    output += name;
    output.push_back('\t');
    output += std::to_string(value.size());
    output.push_back(':');
    output += value;
    output.push_back('\n');
}

std::string payload_from_bundle(std::string_view bundle) {
    const std::string marker = "payload\t";
    const std::size_t field = bundle.find(marker);
    if (field == std::string_view::npos) {
        throw std::runtime_error("test bundle has no payload");
    }
    const std::size_t length_begin = field + marker.size();
    const std::size_t colon =
        bundle.find(':', length_begin);
    if (colon == std::string_view::npos) {
        throw std::runtime_error(
            "test bundle has no payload length");
    }
    const std::size_t length = static_cast<std::size_t>(
        std::stoull(std::string(
            bundle.substr(
                length_begin, colon - length_begin))));
    const std::size_t begin = colon + 1;
    if (length > bundle.size() - begin) {
        throw std::runtime_error(
            "test bundle payload is truncated");
    }
    return std::string(bundle.substr(begin, length));
}

std::string make_bundle(std::string_view payload) {
    std::string result;
    append_text(result, "schema", replay::kBundleSchema);
    append_text(
        result, "payload_sha256",
        old_school::artifact_integrity::sha256_string(
            payload));
    append_text(result, "payload", payload);
    return result;
}

void replace_once_same_size(
    std::string& bytes, std::string_view before,
    std::string_view after) {
    if (before.size() != after.size()) {
        throw std::runtime_error(
            "test mutation changes byte count");
    }
    const std::size_t position = bytes.find(before);
    if (position == std::string::npos ||
        bytes.find(before, position + 1) !=
            std::string::npos) {
        throw std::runtime_error(
            "test mutation marker is absent or ambiguous");
    }
    bytes.replace(position, before.size(), after);
}

void replace_once(
    std::string& bytes, std::string_view before,
    std::string_view after) {
    const std::size_t position = bytes.find(before);
    if (position == std::string::npos ||
        bytes.find(before, position + 1) !=
            std::string::npos) {
        throw std::runtime_error(
            "test mutation marker is absent or ambiguous");
    }
    bytes.replace(position, before.size(), after);
}

void mutate_framed_numeric_field(
    std::string& bytes, std::string_view name) {
    const std::string marker = std::string(name) + "\t";
    const std::size_t field = bytes.find(marker);
    if (field == std::string::npos ||
        bytes.find(marker, field + 1) !=
            std::string::npos) {
        throw std::runtime_error(
            "numeric mutation field is absent or ambiguous");
    }
    const std::size_t colon =
        bytes.find(':', field + marker.size());
    if (colon == std::string::npos) {
        throw std::runtime_error(
            "numeric mutation field is not framed");
    }
    const std::size_t length = static_cast<std::size_t>(
        std::stoull(bytes.substr(
            field + marker.size(),
            colon - field - marker.size())));
    const std::size_t begin = colon + 1;
    if (length == 0 || length > bytes.size() - begin) {
        throw std::runtime_error(
            "numeric mutation length is invalid");
    }
    const std::size_t decimal = bytes.find('.', begin);
    const std::size_t digit =
        decimal != std::string::npos &&
                decimal + 1 < begin + length
            ? decimal + 1
            : begin;
    char& changed = bytes[digit];
    if (changed < '0' || changed > '9') {
        throw std::runtime_error(
            "numeric mutation value has no mutable digit");
    }
    changed =
        changed == '9' ? '8' : static_cast<char>(changed + 1);
}

void test_loads_exact_sealed_bundle() {
    const replay::ReplayBundle bundle = replay::load();
    expect(
        bundle.artifact_sha256 == replay::kArtifactSha256 &&
            bundle.payload_sha256 ==
                replay::kPayloadSha256 &&
            bundle.model_fingerprint ==
                replay::kModelFingerprint &&
            bundle.selected_roots == replay::kRootCount &&
            bundle.stable_disagreements == 4 &&
            bundle.stable_agreements == 43 &&
            bundle.unstable_best_sets == 5 &&
            bundle.invalid_invariance == 0 &&
            bundle.replays.size() == replay::kReplayCount,
        "sealed DVR2 metadata or root census changed");

    const std::vector<std::size_t> expected_indices = {
        16, 19, 46, 50};
    std::vector<std::size_t> actual_indices;
    for (const replay::ReplayRecord& record :
         bundle.replays) {
        actual_indices.push_back(record.root_index);
        const auto validation =
            old_school::probes::validate_probe(
                record.probe,
                record.dvr1.reference_seed_base);
        expect(
            validation.ok() &&
                record.dvr1.production_action_descriptor ==
                    record.production_action_descriptor &&
                record.dvr1.reference_best_actions ==
                    std::vector<std::string>{
                        "kind-0.card-0.x-0"} &&
                record.dvr1.legal_action_descriptors.size() ==
                    record.reference_score.action_count &&
                record.reference_score.scout_worlds == 64 &&
                record.reference_score.confirmation_worlds ==
                    64 &&
                record.reference_score.horizon_turns == 8 &&
                record.reference_score
                    .hidden_repartition_bit_identical,
            "DVR2 replay lost a valid complete K64/H8 "
            "stable-disagreement root");
    }
    expect(
        actual_indices == expected_indices,
        "DVR2 replay root identities changed");
}

void test_exact_copy_loads_and_mutations_fail_closed() {
    const std::string bytes =
        read_file(std::filesystem::path(replay::kArtifactPath));
    TemporaryDirectory temporary;
    const auto copy = temporary.path() / "copy.dvr2";
    write_file(copy, bytes);
    const replay::ReplayBundle copied = replay::load(copy);
    expect(
        copied.artifact_sha256 == replay::kArtifactSha256 &&
            copied.replays.size() == replay::kReplayCount,
        "exact DVR2 copy did not load");

    write_file(copy, bytes + "trailing");
    expect_rejected(
        [&] { static_cast<void>(replay::load(copy)); },
        "sealed loader accepted trailing artifact bytes");

    const auto symlink = temporary.path() / "link.dvr2";
    const auto exact = temporary.path() / "exact.dvr2";
    write_file(exact, bytes);
    if (::symlink(exact.c_str(), symlink.c_str()) != 0) {
        throw std::system_error(
            errno, std::generic_category(),
            "cannot create DVR2 replay test symlink");
    }
    expect_rejected(
        [&] { static_cast<void>(replay::load(symlink)); },
        "sealed loader followed a final-component symlink");
}

void test_outer_parser_rejects_duplicates_and_lengths() {
    const std::string canonical =
        read_file(std::filesystem::path(replay::kArtifactPath));
    std::string duplicate;
    append_text(
        duplicate, "schema", replay::kBundleSchema);
    append_text(
        duplicate, "schema", replay::kBundleSchema);
    const std::string payload =
        payload_from_bundle(canonical);
    append_text(
        duplicate, "payload_sha256",
        old_school::artifact_integrity::sha256_string(
            payload));
    append_text(duplicate, "payload", payload);
    expect_rejected(
        [&] {
            static_cast<void>(
                replay::testing::
                    decode_structurally_valid_bundle(
                        duplicate));
        },
        "outer parser accepted a duplicate field");

    std::string noncanonical = canonical;
    replace_once_same_size(
        noncanonical,
        "schema\t25:old-school-dvr2-bundle-v1",
        "schema\t+5:old-school-dvr2-bundle-v1");
    expect_rejected(
        [&] {
            static_cast<void>(
                replay::testing::
                    decode_structurally_valid_bundle(
                        noncanonical));
        },
        "outer parser accepted a noncanonical length");
}

void test_payload_parser_rejects_duplicate_and_trailing_fields() {
    const std::string canonical =
        read_file(std::filesystem::path(replay::kArtifactPath));
    std::string duplicate =
        payload_from_bundle(canonical);
    replace_once_same_size(
        duplicate,
        "gate.source_balance\t1\n",
        "gate.model_identity\t1\n");
    expect_rejected(
        [&] {
            const std::string bundle = make_bundle(duplicate);
            static_cast<void>(
                replay::testing::
                    decode_structurally_valid_bundle(bundle));
        },
        "payload parser accepted a duplicate field");

    std::string truncated =
        payload_from_bundle(canonical);
    expect(
        !truncated.empty() && truncated.back() == '\n',
        "canonical payload does not end in newline");
    truncated.back() = 'x';
    expect_rejected(
        [&] {
            const std::string bundle = make_bundle(truncated);
            static_cast<void>(
                replay::testing::
                    decode_structurally_valid_bundle(bundle));
        },
        "payload parser accepted trailing unframed bytes");
}

void test_schema_and_root_manifest_reject_ignored_mutations() {
    const std::string canonical =
        read_file(std::filesystem::path(replay::kArtifactPath));
    std::string renamed =
        payload_from_bundle(canonical);
    replace_once_same_size(
        renamed,
        "coverage.0.0.considered\t",
        "coverage.0.0.considerex\t");
    expect_rejected(
        [&] {
            const std::string bundle = make_bundle(renamed);
            static_cast<void>(
                replay::testing::
                    decode_structurally_valid_bundle(bundle));
        },
        "schema boundary accepted a renamed ignored field");

    std::string synchronized =
        payload_from_bundle(canonical);
    replace_once(
        synchronized,
        "root.0.classification\t16:stable-agreement\n",
        "root.0.classification\t17:unstable-best-set\n");
    replace_once(
        synchronized,
        "root.4.classification\t17:unstable-best-set\n",
        "root.4.classification\t16:stable-agreement\n");
    expect(
        synchronized.size() ==
            payload_from_bundle(canonical).size(),
        "classification-swap mutation changed payload size");
    expect_rejected(
        [&] {
            const std::string bundle =
                make_bundle(synchronized);
            static_cast<void>(
                replay::testing::
                    decode_structurally_valid_bundle(bundle));
        },
        "root manifest accepted a classification swap that "
        "preserved every summary cross-sum");
}

void test_reference_derived_statistics_are_recomputed() {
    const std::string canonical =
        read_file(std::filesystem::path(replay::kArtifactPath));
    for (const std::string_view field :
         {"root.16.reference.confirmation_actual_mean",
          "root.16.reference.confirmation_best_mean",
          "root.16.reference.confirmation_regret",
          "root.16.reference.paired_lower_95"}) {
        std::string payload =
            payload_from_bundle(canonical);
        mutate_framed_numeric_field(payload, field);
        expect_rejected(
            [&] {
                const std::string bundle =
                    make_bundle(payload);
                static_cast<void>(
                    replay::testing::
                        decode_structurally_valid_bundle(
                            bundle));
            },
            "reference parser accepted a mutated derived "
            "statistic");
    }
}

void test_dvr1_corruption_is_rejected_after_rechecksum() {
    const std::string canonical =
        read_file(std::filesystem::path(replay::kArtifactPath));
    std::string payload = payload_from_bundle(canonical);
    constexpr std::string_view before =
        "production_model_fingerprint\t64:"
        "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
    constexpr std::string_view after =
        "production_model_fingerprint\t64:"
        "78126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
    const std::size_t position = payload.find(before);
    expect(
        position != std::string::npos,
        "DVR1 mutation marker is absent");
    payload.replace(position, before.size(), after);
    expect_rejected(
        [&] {
            const std::string bundle = make_bundle(payload);
            static_cast<void>(
                replay::testing::
                    decode_structurally_valid_bundle(bundle));
        },
        "DVR2 extractor accepted a rechecksummed corrupt DVR1");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "exact sealed bundle and four replay roots",
        test_loads_exact_sealed_bundle);
    runner.run(
        "exact copy and fail-closed file identity",
        test_exact_copy_loads_and_mutations_fail_closed);
    runner.run(
        "strict outer framing",
        test_outer_parser_rejects_duplicates_and_lengths);
    runner.run(
        "strict payload framing",
        test_payload_parser_rejects_duplicate_and_trailing_fields);
    runner.run(
        "frozen schema and root manifest",
        test_schema_and_root_manifest_reject_ignored_mutations);
    runner.run(
        "recomputed reference statistics",
        test_reference_derived_statistics_are_recomputed);
    runner.run(
        "strict DVR1 decoding and identity",
        test_dvr1_corruption_is_rejected_after_rechecksum);
    return runner.finish();
}
