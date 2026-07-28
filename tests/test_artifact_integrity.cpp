#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace {

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

void expect_equal(
    std::string_view actual, std::string_view expected,
    std::string_view message) {
    if (actual != expected) {
        throw std::runtime_error(
            std::string(message) + ": expected '" +
            std::string(expected) + "', got '" +
            std::string(actual) + "'");
    }
}

template <typename Function>
void expect_rejected(Function function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

template <typename Function>
void expect_logic_error(Function function, std::string_view message) {
    try {
        function();
    } catch (const std::logic_error&) {
        return;
    } catch (const std::exception& error) {
        throw std::runtime_error(
            std::string(message) +
            ": wrong exception type: " + error.what());
    }
    throw std::runtime_error(std::string(message));
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        std::string pattern =
            (std::filesystem::temp_directory_path() /
             "old-school-artifact-integrity-XXXXXX")
                .string();
        std::array<char, 256> buffer{};
        if (pattern.size() >= buffer.size()) {
            throw std::runtime_error(
                "temporary directory template is too long");
        }
        std::copy(pattern.begin(), pattern.end(), buffer.begin());
        char* const created = ::mkdtemp(buffer.data());
        if (created == nullptr) {
            throw std::system_error(
                errno, std::generic_category(),
                "cannot create temporary directory");
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

void write_file(
    const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open test file");
    }
    output.write(
        contents.data(),
        static_cast<std::streamsize>(contents.size()));
    if (!output) {
        throw std::runtime_error("cannot write test file");
    }
}

void test_standard_sha256_vectors() {
    expect_equal(
        integrity::sha256_string(""),
        "e3b0c44298fc1c149afbf4c8996fb924"
        "27ae41e4649b934ca495991b7852b855",
        "empty SHA-256 vector");
    expect_equal(
        integrity::sha256_string("abc"),
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad",
        "abc SHA-256 vector");

    constexpr std::string_view long_vector =
        "abcdbcdecdefdefgefghfghighijhijk"
        "ijkljklmklmnlmnomnopnopq";
    const auto bytes =
        std::as_bytes(std::span(long_vector));
    expect_equal(
        integrity::sha256_bytes(bytes),
        "248d6a61d20638b8e5c026930c3e6039"
        "a33ce45964ff2167f6ecedd419db06c1",
        "long SHA-256 vector");

    const std::string million_a(1'000'000, 'a');
    expect_equal(
        integrity::sha256_string(million_a),
        "cdc76e5c9914fb9281a1c7e284d73e67"
        "f1809a48a497200e046d39ccc7112cd0",
        "million-byte SHA-256 vector");

    integrity::Sha256Accumulator incremental;
    incremental.update(std::string_view("a"));
    incremental.update(std::as_bytes(
        std::span(std::string_view("b"))));
    incremental.update(std::string_view("c"));
    expect_equal(
        incremental.finish(),
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad",
        "incremental abc SHA-256 vector");
}

std::string patterned_bytes(std::size_t size) {
    std::string result;
    result.reserve(size);
    for (std::size_t index = 0; index < size; ++index) {
        result.push_back(static_cast<char>(
            (index * 131U + 29U) & 0xffU));
    }
    return result;
}

void test_incremental_sha256_chunk_boundaries() {
    constexpr std::array<std::size_t, 7> kLengths = {
        0, 1, 55, 56, 63, 64, 65,
    };
    constexpr std::array<std::string_view, 7> kDigests = {
        "e3b0c44298fc1c149afbf4c8996fb924"
        "27ae41e4649b934ca495991b7852b855",
        "1f18d650d205d71d934c3646ff5fac1c"
        "096ba52eba4cf758b865364f4167d3cd",
        "709aad8263c2d9fbe616ce57a05b8d5a"
        "4453f4773e1342b42ccfd7d8fb7102b4",
        "30d4c8102c9227a009e4fc08c046d866"
        "8f3958e67966cfce00c7bc84a99294c6",
        "e4fa263c22359a1255662cc4ff4f7576"
        "005d7078f2316a60280cc367d845148c",
        "64c09fddb170cfc9c7c504fd15bb1a48"
        "9fabe9d0c3c081f791ee22a34bfb160f",
        "f880abc4f16efcdf8f9a55e1676cef74"
        "c08fcf6cb4d18efe5bf315a64381bae2",
    };
    constexpr std::array<std::size_t, 7> kIrregularChunks = {
        7, 1, 19, 2, 31, 3, 11,
    };

    for (std::size_t case_index = 0;
         case_index < kLengths.size(); ++case_index) {
        const std::size_t length = kLengths[case_index];
        const std::string input = patterned_bytes(length);
        const std::string_view expected = kDigests[case_index];
        expect_equal(
            integrity::sha256_string(input), expected,
            "one-shot boundary digest");

        integrity::Sha256Accumulator whole_bytes;
        whole_bytes.update(std::as_bytes(std::span(input)));
        expect_equal(
            whole_bytes.finish(), expected,
            "whole byte-span incremental digest");

        integrity::Sha256Accumulator one_byte;
        for (std::size_t offset = 0;
             offset < input.size(); ++offset) {
            one_byte.update(std::string_view(input).substr(offset, 1));
        }
        expect_equal(
            one_byte.finish(), expected,
            "one-byte incremental digest");

        integrity::Sha256Accumulator irregular;
        std::size_t offset = 0;
        std::size_t chunk_index = 0;
        while (offset < input.size()) {
            const std::size_t count = std::min(
                kIrregularChunks[
                    chunk_index % kIrregularChunks.size()],
                input.size() - offset);
            const std::string_view chunk(input.data() + offset, count);
            if (chunk_index % 2 == 0) {
                irregular.update(chunk);
            } else {
                irregular.update(
                    std::as_bytes(std::span(chunk)));
            }
            offset += count;
            ++chunk_index;
        }
        expect_equal(
            irregular.finish(), expected,
            "irregular incremental digest");
    }
}

void test_incremental_sha256_embedded_nul() {
    static constexpr std::array<char, 3> kInput = {'a', '\0', 'b'};
    constexpr std::string_view input(kInput.data(), kInput.size());

    integrity::Sha256Accumulator accumulator;
    accumulator.update(input.substr(0, 2));
    accumulator.update(std::as_bytes(
        std::span(input.substr(2))));
    expect_equal(
        accumulator.finish(),
        "59b271ae1bbcb1d31d41929817f4b16f"
        "b439eb4f31520b5ad1d5ce98920a7138",
        "embedded-NUL incremental digest");
    expect_equal(
        integrity::sha256_string(input),
        "59b271ae1bbcb1d31d41929817f4b16f"
        "b439eb4f31520b5ad1d5ce98920a7138",
        "embedded-NUL one-shot digest");
}

void test_incremental_sha256_lifecycle_rejection() {
    integrity::Sha256Accumulator accumulator;
    accumulator.update(std::string_view{});
    accumulator.update(std::span<const std::byte>{});
    accumulator.update(std::string_view("abc"));
    const std::string digest = accumulator.finish();
    expect_equal(
        digest,
        integrity::sha256_string("abc"),
        "finished accumulator digest");

    expect_logic_error(
        [&] { accumulator.update(std::string_view{}); },
        "string update after finish was accepted");
    const std::array<std::byte, 1> byte = {std::byte{0x01}};
    expect_logic_error(
        [&] { accumulator.update(std::span(byte)); },
        "byte update after finish was accepted");
    expect_logic_error(
        [&] { static_cast<void>(accumulator.finish()); },
        "second finish was accepted");
}

void test_deterministic_file_snapshot() {
    TemporaryDirectory temporary;
    const auto file = temporary.path() / "artifact.bin";
    constexpr std::string_view contents = "artifact-v1\n";
    write_file(file, contents);

    const auto first = integrity::snapshot_regular_file(file);
    const auto second = integrity::snapshot_regular_file(file);
    expect(first == second, "unchanged snapshots differ");
    expect(
        std::filesystem::path(first.path).is_absolute(),
        "snapshot path is not absolute");
    expect(
        std::filesystem::path(first.path) ==
            std::filesystem::path(first.path).lexically_normal(),
        "snapshot path is not lexically normalized");
    expect(
        std::filesystem::path(first.physical_path).is_absolute(),
        "snapshot physical path is not absolute");
    expect(
        std::filesystem::path(first.physical_path) ==
            std::filesystem::path(first.physical_path)
                .lexically_normal(),
        "snapshot physical path is not lexically normalized");
    expect(
        first.byte_size == contents.size(),
        "snapshot byte size is wrong");
    expect_equal(
        first.sha256, integrity::sha256_string(contents),
        "snapshot digest differs from byte digest");
    expect(
        first.modification_nanoseconds >= 0 &&
            first.modification_nanoseconds < 1'000'000'000,
        "snapshot nanoseconds are out of range");
    expect(
        first.change_nanoseconds >= 0 &&
            first.change_nanoseconds < 1'000'000'000,
        "snapshot change-time nanoseconds are out of range");
    expect(
        first.modification_seconds >= 0 &&
            first.change_seconds >= 0,
        "snapshot timestamps are not sane");
    expect(
        first.link_count == 1,
        "new regular file link count is not one");

    const auto hard_link = temporary.path() / "artifact-hardlink.bin";
    std::filesystem::create_hard_link(file, hard_link);
    const auto linked = integrity::snapshot_regular_file(file);
    const auto through_link =
        integrity::snapshot_regular_file(hard_link);
    expect(
        linked.link_count == 2 &&
            through_link.link_count == 2,
        "hard-link count was not recorded");
    expect(
        linked.device == through_link.device &&
            linked.inode == through_link.inode,
        "hard links did not report the same file identity");
}

void test_content_mutation_changes_digest() {
    TemporaryDirectory temporary;
    const auto file = temporary.path() / "artifact.bin";
    write_file(file, "artifact-v1\n");
    const auto before = integrity::snapshot_regular_file(file);

    write_file(file, "artifact-v2\n");
    const auto after = integrity::snapshot_regular_file(file);
    expect(
        before != after,
        "content mutation did not change snapshot evidence");
    expect(
        before.byte_size == after.byte_size,
        "same-size mutation changed size");
    expect(
        before.sha256 != after.sha256,
        "content mutation did not change digest");
}

void test_rejects_symlink_directory_and_missing_path() {
    TemporaryDirectory temporary;
    const auto file = temporary.path() / "artifact.bin";
    const auto link = temporary.path() / "artifact-link.bin";
    const auto missing = temporary.path() / "missing.bin";
    write_file(file, "artifact\n");
    if (::symlink(file.c_str(), link.c_str()) != 0) {
        throw std::system_error(
            errno, std::generic_category(),
            "cannot create test symlink");
    }

    expect_rejected(
        [&] { integrity::snapshot_regular_file(link); },
        "symlink path was accepted");
    expect_rejected(
        [&] {
            integrity::snapshot_regular_file(temporary.path());
        },
        "directory path was accepted");
    expect_rejected(
        [&] { integrity::snapshot_regular_file(missing); },
        "missing path was accepted");
}

void test_ancestor_symlink_records_lexical_and_physical_paths() {
    TemporaryDirectory temporary;
    const auto physical_parent =
        temporary.path() / "physical-parent";
    const auto alias_parent =
        temporary.path() / "alias-parent";
    std::filesystem::create_directory(physical_parent);
    if (::symlink(
            physical_parent.c_str(), alias_parent.c_str()) != 0) {
        throw std::system_error(
            errno, std::generic_category(),
            "cannot create ancestor test symlink");
    }

    const auto physical_file =
        physical_parent / "artifact.bin";
    const auto requested_file =
        alias_parent / "artifact.bin";
    write_file(physical_file, "artifact\n");

    const auto via_alias =
        integrity::snapshot_regular_file(requested_file);
    const auto direct =
        integrity::snapshot_regular_file(physical_file);
    const auto expected_requested =
        std::filesystem::absolute(requested_file)
            .lexically_normal();
    const auto expected_physical =
        (std::filesystem::canonical(alias_parent) /
         requested_file.filename())
            .lexically_normal();

    expect(
        std::filesystem::path(via_alias.path) ==
            expected_requested,
        "ancestor symlink was not preserved in requested path");
    expect(
        std::filesystem::path(via_alias.physical_path) ==
            expected_physical,
        "ancestor symlink was not resolved in physical path");
    expect(
        via_alias.path != via_alias.physical_path,
        "lexical and physical paths unexpectedly match");
    expect(
        via_alias.physical_path == direct.physical_path,
        "alias and direct paths do not share physical identity");
    expect(
        via_alias.device == direct.device &&
            via_alias.inode == direct.inode &&
            via_alias.sha256 == direct.sha256,
        "alias and direct snapshots do not describe one file");
}

void test_relative_and_absolute_paths_normalize_identically() {
    TemporaryDirectory temporary;
    const auto absolute = temporary.path() / "artifact.bin";
    write_file(absolute, "artifact\n");
    const auto relative = absolute.lexically_relative(
        std::filesystem::current_path());
    expect(
        !relative.empty(),
        "test path could not be made lexically relative");

    const auto from_absolute =
        integrity::snapshot_regular_file(absolute);
    const auto from_relative =
        integrity::snapshot_regular_file(relative);
    expect(
        from_absolute == from_relative,
        "relative and absolute snapshots differ");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "standard SHA-256 vectors",
        test_standard_sha256_vectors);
    runner.run(
        "incremental SHA-256 chunk boundaries",
        test_incremental_sha256_chunk_boundaries);
    runner.run(
        "incremental SHA-256 embedded NUL",
        test_incremental_sha256_embedded_nul);
    runner.run(
        "incremental SHA-256 lifecycle rejection",
        test_incremental_sha256_lifecycle_rejection);
    runner.run(
        "deterministic regular-file snapshot",
        test_deterministic_file_snapshot);
    runner.run(
        "content mutation changes digest",
        test_content_mutation_changes_digest);
    runner.run(
        "symlink, directory, and missing rejection",
        test_rejects_symlink_directory_and_missing_path);
    runner.run(
        "ancestor symlink lexical and physical identity",
        test_ancestor_symlink_records_lexical_and_physical_paths);
    runner.run(
        "relative and absolute path normalization",
        test_relative_and_absolute_paths_normalize_identically);
    return runner.finish();
}
