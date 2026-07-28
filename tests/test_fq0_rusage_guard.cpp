#include "old_school/fq0_rusage_guard.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

namespace guard = old_school::fq0_rusage_guard;

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

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        std::string pattern =
            (std::filesystem::temp_directory_path() /
             "old-school-fq0-rusage-XXXXXX")
                .string();
        std::vector<char> bytes(
            pattern.begin(), pattern.end());
        bytes.push_back('\0');
        char* const created = ::mkdtemp(bytes.data());
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

guard::ResourceSample sample(
    std::uint64_t current, std::uint64_t peak,
    std::uint64_t resident,
    std::uint64_t start = 123,
    std::uint8_t uuid_byte = 7,
    std::uint64_t exit_time = 0) {
    guard::ResourceSample result;
    result.identity.uuid[0] = uuid_byte;
    result.identity.start_abstime = start;
    result.exit_abstime = exit_time;
    result.physical_footprint = current;
    result.lifetime_max_physical_footprint = peak;
    result.resident_size = resident;
    return result;
}

guard::Config unlimited_config() {
    guard::Config config;
    config.physical_limit_bytes =
        std::numeric_limits<std::uint64_t>::max();
    config.resident_limit_bytes =
        std::numeric_limits<std::uint64_t>::max();
    config.publication_delta_limit_bytes =
        std::numeric_limits<std::uint64_t>::max();
    config.sample_period = std::chrono::milliseconds(10);
    return config;
}

bool file_contains(
    const std::filesystem::path& path,
    std::string_view needle);

void expect_absent_or_terminal_and_logged(
    pid_t pid, const std::filesystem::path& events_path,
    std::string_view description) {
    const guard::SampleRead reading =
        guard::read_process_sample(pid);
    if (!reading.sample.has_value()) {
        expect(
            reading.error_number == ESRCH,
            std::string(description) +
                " inspection failed with errno " +
                std::to_string(reading.error_number));
        return;
    }
    expect(
        reading.sample->exit_abstime != 0,
        std::string(description) +
            " remains execution-capable");
    expect(
        file_contains(
            events_path,
            "\tterminal_process_group_member\tpid=" +
                std::to_string(pid) + " "),
        std::string(description) +
            " terminal identity was not logged");
}

void complete_sample(
    guard::GateState& state,
    const guard::ResourceSample& value,
    std::uint64_t begin, std::uint64_t complete) {
    state.begin_sample(begin);
    state.complete_sample(value, complete);
}

void complete_valid_lifecycle(
    guard::GateState& state,
    std::uint64_t first_epoch,
    guard::ResourceSample final_sample) {
    state.observe_waitid_exit(first_epoch);
    final_sample.exit_abstime = 999;
    state.begin_sample(first_epoch + 1);
    state.complete_sample(
        final_sample, first_epoch + 2);
    state.observe_reaped(first_epoch + 3);
    state.observe_group_quiescent(first_epoch + 4);
}

std::string read_file(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot read test output file");
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void write_exact_to(int descriptor, std::string_view text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const ssize_t count = ::write(
            descriptor, text.data() + offset,
            text.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            ::_exit(120);
        }
    }
}

void write_exact(std::string_view text) {
    write_exact_to(STDOUT_FILENO, text);
}

void write_marker_to(int descriptor) {
    const std::string marker(guard::kPublicationMarker);
    const std::size_t split = marker.size() / 2;
    write_exact_to(
        descriptor,
        std::string_view(marker).substr(0, split));
    ::usleep(10'000);
    write_exact_to(
        descriptor,
        std::string_view(marker).substr(split));
    write_exact_to(descriptor, "\n");
}

int supervised_child(
    const std::filesystem::path& executable,
    std::string_view mode, int exit_status) {
    if (mode == "valid") {
        ::usleep(80'000);
        write_marker_to(STDOUT_FILENO);
        ::usleep(100'000);
        return exit_status;
    }
    if (mode == "missing") {
        ::usleep(180'000);
        return exit_status;
    }
    if (mode == "duplicate") {
        ::usleep(80'000);
        write_marker_to(STDOUT_FILENO);
        write_marker_to(STDERR_FILENO);
        ::usleep(100'000);
        return exit_status;
    }
    if (mode == "signal") {
        ::usleep(80'000);
        write_marker_to(STDOUT_FILENO);
        ::usleep(100'000);
        ::raise(SIGTERM);
        return 126;
    }
    if (mode == "allocator") {
        ::usleep(80'000);
        constexpr std::size_t kBytes =
            128U * 1024U * 1024U;
        void* allocation = ::mmap(
            nullptr, kBytes, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANON, -1, 0);
        if (allocation == MAP_FAILED) {
            return 127;
        }
        auto* bytes =
            static_cast<volatile char*>(allocation);
        for (std::size_t offset = 0;
             offset < kBytes; offset += 4096) {
            bytes[offset] = 1;
        }
        write_exact("ALLOCATED\n");
        ::sleep(5);
        ::munmap(allocation, kBytes);
        return exit_status;
    }
    if (mode == "mutate") {
        ::usleep(80'000);
        if (::unlink(executable.c_str()) != 0) {
            return 128;
        }
        const int descriptor = ::open(
            executable.c_str(),
            O_WRONLY | O_CREAT | O_EXCL,
            S_IRWXU);
        if (descriptor < 0) {
            return 129;
        }
        write_exact_to(descriptor, "mutated executable\n");
        ::close(descriptor);
        write_marker_to(STDOUT_FILENO);
        ::usleep(100'000);
        return exit_status;
    }
    if (mode == "descendant") {
        const pid_t descendant = ::fork();
        if (descendant < 0) {
            return 121;
        }
        if (descendant == 0) {
            while (true) {
                ::pause();
            }
        }
        write_exact(
            "DESCENDANT\t" +
            std::to_string(descendant) + "\n");
        while (true) {
            ::pause();
        }
    }
    return 122;
}

void test_marker_scanner() {
    guard::MarkerScanner scanner("aba");
    expect(scanner.feed("xxa").empty(),
           "partial marker must not count");
    const std::vector<std::uint64_t> offsets =
        scanner.feed("baba");
    expect(
        offsets == std::vector<std::uint64_t>({2, 4}),
        "scanner must report overlapping cross-chunk offsets");

    guard::MarkerScanner output("marker");
    guard::MarkerScanner error("marker");
    expect(output.feed("mar").empty(),
           "stdout partial must remain partial");
    expect(error.feed("ker").empty(),
           "stderr must not complete stdout's marker");
}

void test_identity_and_monotone_samples() {
    guard::GateState state(unlimited_config());
    complete_sample(state, sample(100, 100, 80), 1, 2);
    complete_sample(state, sample(200, 200, 160), 3, 4);
    complete_sample(state, sample(90, 200, 70), 5, 6);
    expect(state.failure() == guard::Failure::none,
           "stable samples should pass and current may fall");

    guard::GateState uuid_changed(unlimited_config());
    complete_sample(
        uuid_changed, sample(10, 10, 10), 1, 2);
    complete_sample(
        uuid_changed,
        sample(10, 10, 10, 123, 8), 3, 4);
    expect(
        uuid_changed.failure() ==
            guard::Failure::identity,
        "UUID mutation must fail");

    guard::GateState start_changed(unlimited_config());
    complete_sample(
        start_changed, sample(10, 10, 10), 1, 2);
    complete_sample(
        start_changed,
        sample(10, 10, 10, 124), 3, 4);
    expect(
        start_changed.failure() ==
            guard::Failure::identity,
        "process-start mutation must fail");

    guard::GateState zero_identity(unlimited_config());
    guard::ResourceSample zero = sample(10, 10, 10);
    zero.identity.uuid.fill(0);
    complete_sample(zero_identity, zero, 1, 2);
    expect(
        zero_identity.failure() ==
            guard::Failure::identity,
        "all-zero UUID must fail");
}

void test_invalid_sample_relations() {
    guard::GateState current_above_peak(
        unlimited_config());
    complete_sample(
        current_above_peak, sample(101, 100, 50),
        1, 2);
    expect(
        current_above_peak.failure() ==
                guard::Failure::none &&
            current_above_peak.effective_peak() == 101 &&
            current_above_peak.inversion_count() == 1 &&
            current_above_peak.maximum_inversion_gap() == 1 &&
            current_above_peak.last_inversion_gap() == 1,
        "current-over-lifetime inversion must be diagnostic");

    guard::GateState peak_regressed(unlimited_config());
    complete_sample(
        peak_regressed, sample(100, 200, 50), 1, 2);
    complete_sample(
        peak_regressed, sample(100, 199, 50), 3, 4);
    expect(
        peak_regressed.failure() == guard::Failure::none &&
            peak_regressed.effective_peak() == 200 &&
            peak_regressed.lifetime_regression_count() == 1 &&
            peak_regressed
                    .maximum_lifetime_regression_gap() == 1 &&
            peak_regressed
                    .last_lifetime_regression_gap() == 1,
        "raw lifetime regression must be diagnostic "
        "without lowering the envelope");

    guard::GateState a5(unlimited_config());
    complete_sample(
        a5,
        sample(
            5'591'003'392ULL, 5'590'987'008ULL,
            5'844'631'552ULL),
        1, 2);
    expect(
        a5.failure() == guard::Failure::none &&
            a5.effective_peak() == 5'591'003'392ULL &&
            a5.inversion_count() == 1 &&
            a5.maximum_inversion_gap() == 16'384,
        "literal A5 one-page inversion must be accepted");

    guard::GateState a5b(unlimited_config());
    complete_sample(
        a5b,
        sample(
            2'272'483'264ULL, 2'272'450'496ULL,
            2'424'750'080ULL),
        1, 2);
    expect(
        a5b.failure() == guard::Failure::none &&
            a5b.effective_peak() == 2'272'483'264ULL &&
            a5b.inversion_count() == 1 &&
            a5b.maximum_inversion_gap() == 32'768,
        "literal A5b two-page inversion must be accepted");

    guard::GateState a5c(unlimited_config());
    const std::array<std::uint8_t, 16> a5c_uuid = {
        0xe2, 0xfa, 0xfd, 0xc8,
        0x2b, 0xbc, 0x3d, 0x9b,
        0xb9, 0x96, 0xb7, 0x8c,
        0x17, 0x7c, 0x86, 0xf8,
    };
    const auto a5c_sample =
        [&a5c_uuid](
            std::uint64_t current,
            std::uint64_t raw_lifetime,
            std::uint64_t resident,
            std::uint64_t exit_time = 0) {
            guard::ResourceSample result = sample(
                current, raw_lifetime, resident,
                296'017'542'498ULL, 0, exit_time);
            result.identity.uuid = a5c_uuid;
            return result;
        };
    complete_sample(
        a5c,
        a5c_sample(
            359'745'600, 359'745'600, 400'932'864),
        101, 102);
    complete_sample(
        a5c,
        a5c_sample(
            556'550'592, 556'534'208, 597'639'168),
        103, 104);
    a5c.observe_waitid_exit(105);
    complete_sample(
        a5c,
        a5c_sample(
            0, 558'320'064, 599'408'640,
            296'329'186'725ULL),
        106, 107);
    expect(
        a5c.failure() == guard::Failure::none &&
            a5c.sample_count() == 3 &&
            a5c.final_sample_after_waitid() &&
            a5c.effective_peak() == 558'320'064 &&
            a5c.inversion_count() == 1 &&
            a5c.maximum_inversion_gap() == 16'384 &&
            a5c.lifetime_regression_count() == 0,
        "literal A5c rows 51-53 must preserve identity, "
        "accept the inversion, and retain the final envelope");

    guard::GateState sampler_failed(unlimited_config());
    sampler_failed.begin_sample(1);
    sampler_failed.complete_sample_failure(2);
    expect(
        sampler_failed.failure() ==
            guard::Failure::sampler,
        "sample reader failure must fail closed");
}

void test_process_group_member_classification() {
    const guard::ProcessGroupClassification live =
        guard::classify_process_group_members({
            {
                101,
                {sample(10, 10, 10), 0},
            },
        });
    expect(
        !live.execution_quiescent &&
            live.live_pid == 101 &&
            live.error_number == 0,
        "a live process-group member must reject "
        "execution quiescence");

    const guard::ResourceSample exited =
        sample(0, 10, 10, 123, 7, 999);
    const guard::ProcessGroupClassification terminal =
        guard::classify_process_group_members({
            {
                102,
                {exited, 0},
            },
            {
                103,
                {std::nullopt, ESRCH},
            },
        });
    expect(
        terminal.execution_quiescent &&
            terminal.live_pid == -1 &&
            terminal.error_number == 0 &&
            terminal.terminal_members.size() == 1 &&
            terminal.terminal_members.front().pid == 102 &&
            terminal.terminal_members.front().sample ==
                exited,
        "terminal zombies and ESRCH races must be "
        "execution-quiescent with terminal identity retained");

    const guard::ProcessGroupClassification failed =
        guard::classify_process_group_members({
            {
                104,
                {std::nullopt, EIO},
            },
        });
    expect(
        !failed.execution_quiescent &&
            failed.error_number == EIO,
        "unexpected process-group inspection errors must "
        "fail closed");
}

void test_absolute_limit_boundaries() {
    guard::Config config = unlimited_config();
    config.physical_limit_bytes = 100;
    config.resident_limit_bytes = 100;

    guard::GateState below(config);
    complete_sample(below, sample(99, 99, 99), 1, 2);
    expect(below.failure() == guard::Failure::none,
           "one byte below limits must pass");

    guard::GateState physical(config);
    complete_sample(
        physical, sample(100, 99, 99), 1, 2);
    expect(
        physical.failure() ==
                guard::Failure::physical_limit &&
            physical.effective_peak() == 100 &&
            physical.inversion_count() == 1,
        "current physical cap must trigger at equality "
        "even during an inversion");

    guard::GateState lifetime(config);
    complete_sample(
        lifetime, sample(99, 100, 99), 1, 2);
    expect(
        lifetime.failure() ==
            guard::Failure::physical_limit,
        "lifetime cap must trigger at equality");

    guard::GateState resident(config);
    complete_sample(
        resident, sample(99, 99, 100), 1, 2);
    expect(
        resident.failure() ==
            guard::Failure::resident_limit,
        "resident cap must trigger at equality");
}

void test_publication_delta_boundary() {
    guard::Config config = unlimited_config();
    config.publication_delta_limit_bytes =
        guard::kGibibyte;

    constexpr std::uint64_t kThreeGibibytes =
        3ULL * guard::kGibibyte;
    guard::GateState catch_up(config);
    complete_sample(
        catch_up,
        sample(
            kThreeGibibytes, guard::kGibibyte, 500),
        1, 2);
    catch_up.observe_marker({
        3, guard::MarkerStream::standard_output, 17});
    complete_sample(
        catch_up,
        sample(
            kThreeGibibytes, kThreeGibibytes, 500),
        4, 5);
    expect(
        catch_up.failure() == guard::Failure::none &&
            catch_up.publication_baseline_effective() ==
                kThreeGibibytes &&
            catch_up.publication_current_delta() == 0 &&
            catch_up.publication_effective_delta() == 0 &&
            catch_up.publication_delta_bytes() == 0,
        "raw lifetime catch-up to a pre-marker envelope "
        "must not count as publication growth");

    constexpr std::uint64_t kHistoricPeak =
        2ULL * guard::kGibibyte;
    const guard::ResourceSample historic_baseline =
        sample(1'000, kHistoricPeak, 500);
    guard::GateState current_exact(config);
    complete_sample(
        current_exact, historic_baseline, 1, 2);
    current_exact.observe_marker({
        3, guard::MarkerStream::standard_output, 17});
    complete_sample(
        current_exact,
        sample(
            1'000 + guard::kGibibyte,
            kHistoricPeak, 500),
        4, 5);
    complete_valid_lifecycle(
        current_exact, 6,
        sample(
            1'000 + guard::kGibibyte,
            kHistoricPeak, 500));
    current_exact.finish();
    expect(
        current_exact.failure() == guard::Failure::none &&
            current_exact.publication_current_delta() ==
                guard::kGibibyte &&
            current_exact.publication_effective_delta() == 0 &&
            current_exact.publication_delta_bytes() ==
                guard::kGibibyte,
        "current axis must pass at exactly one GiB "
        "under a historic effective high");

    guard::GateState current_above(config);
    complete_sample(
        current_above, historic_baseline, 1, 2);
    current_above.observe_marker({
        3, guard::MarkerStream::standard_output, 0});
    complete_sample(
        current_above,
        sample(
            1'000 + guard::kGibibyte + 1,
            kHistoricPeak, 500),
        4, 5);
    expect(
        current_above.failure() ==
                guard::Failure::publication_delta &&
            current_above.publication_current_delta() ==
                guard::kGibibyte + 1 &&
            current_above.publication_effective_delta() == 0,
        "current axis must fail at one GiB plus one byte");

    const guard::ResourceSample effective_baseline =
        sample(1'000, 2'000, 500);
    guard::GateState effective_exact(config);
    complete_sample(
        effective_exact, effective_baseline, 1, 2);
    effective_exact.observe_marker({
        3, guard::MarkerStream::standard_output, 0});
    complete_sample(
        effective_exact,
        sample(
            1'000, 2'000 + guard::kGibibyte, 500),
        4, 5);
    expect(
        effective_exact.failure() == guard::Failure::none &&
            effective_exact.publication_current_delta() == 0 &&
            effective_exact.publication_effective_delta() ==
                guard::kGibibyte &&
            effective_exact.publication_delta_bytes() ==
                guard::kGibibyte,
        "effective axis must pass at exactly one GiB "
        "when raw lifetime finds a new high");

    guard::GateState effective_above(config);
    complete_sample(
        effective_above, effective_baseline, 1, 2);
    effective_above.observe_marker({
        3, guard::MarkerStream::standard_output, 0});
    complete_sample(
        effective_above,
        sample(
            1'000,
            2'000 + guard::kGibibyte + 1, 500),
        4, 5);
    expect(
        effective_above.failure() ==
                guard::Failure::publication_delta &&
            effective_above.publication_current_delta() == 0 &&
            effective_above.publication_effective_delta() ==
                guard::kGibibyte + 1,
        "effective axis must fail at one GiB plus one byte");
}

void test_marker_and_lifecycle_contracts() {
    const guard::ResourceSample value =
        sample(10, 10, 10);

    guard::GateState missing(unlimited_config());
    complete_sample(missing, value, 1, 2);
    complete_valid_lifecycle(missing, 3, value);
    missing.finish();
    expect(missing.failure() == guard::Failure::marker,
           "missing marker must fail");

    guard::GateState duplicate(unlimited_config());
    complete_sample(duplicate, value, 1, 2);
    duplicate.observe_marker({
        3, guard::MarkerStream::standard_output, 0});
    duplicate.observe_marker({
        4, guard::MarkerStream::standard_error, 1});
    expect(
        duplicate.failure() == guard::Failure::marker,
        "marker duplicated across streams must fail");

    guard::GateState no_post(unlimited_config());
    complete_sample(no_post, value, 1, 2);
    no_post.observe_marker({
        3, guard::MarkerStream::standard_output, 0});
    no_post.observe_waitid_exit(4);
    no_post.begin_sample(5);
    guard::ResourceSample final_value = value;
    final_value.exit_abstime = 2;
    // Marker was seen before this final sample, so this is a genuine
    // post-marker zombie sample and is allowed.
    no_post.complete_sample(final_value, 6);
    no_post.observe_reaped(7);
    no_post.observe_group_quiescent(8);
    no_post.finish();
    expect(no_post.failure() == guard::Failure::none,
           "a post-marker zombie sample must count");

    guard::GateState marker_after_final(
        unlimited_config());
    complete_sample(marker_after_final, value, 1, 2);
    marker_after_final.observe_waitid_exit(3);
    marker_after_final.begin_sample(4);
    marker_after_final.complete_sample(final_value, 5);
    marker_after_final.observe_marker({
        6, guard::MarkerStream::standard_output, 0});
    marker_after_final.observe_reaped(7);
    marker_after_final.observe_group_quiescent(8);
    marker_after_final.finish();
    expect(
        marker_after_final.failure() ==
            guard::Failure::marker &&
            marker_after_final
                    .post_marker_sample_count() == 0,
        "marker first observed after the final sample "
        "must fail with no post-marker sample");

    guard::GateState missing_final(unlimited_config());
    complete_sample(missing_final, value, 1, 2);
    missing_final.observe_marker({
        3, guard::MarkerStream::standard_output, 0});
    missing_final.observe_waitid_exit(4);
    missing_final.finish();
    expect(
        missing_final.failure() ==
            guard::Failure::process_control,
        "finish must require final sample, reap, and quiescence");
}

void test_infrastructure_failure_precedence() {
    guard::GateState state(unlimited_config());
    complete_sample(state, sample(10, 10, 10), 1, 2);
    state.observe_marker({
        3, guard::MarkerStream::standard_output, 0});
    state.observe_marker({
        4, guard::MarkerStream::standard_error, 0});
    expect(state.failure() == guard::Failure::marker,
           "fixture must begin with marker failure");
    state.observe_failure(guard::Failure::target_integrity);
    expect(
        state.failure() ==
            guard::Failure::target_integrity,
        "later integrity failure must force status 92");
    expect(
        guard::exit_code(state.failure()) ==
            guard::kMonitorFailureExit,
        "integrity override must map to monitor failure");
}

#if defined(__APPLE__)
void write_byte(int descriptor, char byte) {
    while (::write(descriptor, &byte, 1) != 1) {
        if (errno != EINTR) {
            ::_exit(123);
        }
    }
}

void read_byte(int descriptor) {
    char byte = '\0';
    while (::read(descriptor, &byte, 1) != 1) {
        if (errno != EINTR) {
            throw std::runtime_error(
                "controlled child protocol failed");
        }
    }
}

void test_real_rusage_and_zombie_sample() {
    std::array<int, 2> child_to_parent{};
    std::array<int, 2> parent_to_child{};
    if (::pipe(child_to_parent.data()) != 0 ||
        ::pipe(parent_to_child.data()) != 0) {
        throw std::system_error(
            errno, std::generic_category(),
            "cannot create controlled-child pipes");
    }
    const pid_t child = ::fork();
    if (child < 0) {
        throw std::system_error(
            errno, std::generic_category(),
            "cannot fork controlled child");
    }
    if (child == 0) {
        ::close(child_to_parent[0]);
        ::close(parent_to_child[1]);
        write_byte(child_to_parent[1], '0');
        char command = '\0';
        ::read(parent_to_child[0], &command, 1);

        constexpr std::size_t kFirstBytes =
            32U * 1024U * 1024U;
        void* first = ::mmap(
            nullptr, kFirstBytes,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANON, -1, 0);
        if (first == MAP_FAILED) {
            ::_exit(124);
        }
        auto* first_bytes =
            static_cast<volatile char*>(first);
        for (std::size_t offset = 0;
             offset < kFirstBytes; offset += 4096) {
            first_bytes[offset] = 1;
        }
        write_byte(child_to_parent[1], '1');
        ::read(parent_to_child[0], &command, 1);
        ::munmap(first, kFirstBytes);
        write_byte(child_to_parent[1], 'r');
        ::read(parent_to_child[0], &command, 1);

        constexpr std::size_t kSecondBytes =
            64U * 1024U * 1024U;
        void* second = ::mmap(
            nullptr, kSecondBytes,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANON, -1, 0);
        if (second == MAP_FAILED) {
            ::_exit(125);
        }
        auto* second_bytes =
            static_cast<volatile char*>(second);
        for (std::size_t offset = 0;
             offset < kSecondBytes; offset += 4096) {
            second_bytes[offset] = 1;
        }
        write_byte(child_to_parent[1], '2');
        ::read(parent_to_child[0], &command, 1);
        ::munmap(second, kSecondBytes);
        ::_exit(0);
    }

    ::close(child_to_parent[1]);
    ::close(parent_to_child[0]);
    read_byte(child_to_parent[0]);
    const guard::SampleRead initial =
        guard::read_process_sample(child);
    expect(initial.sample.has_value(),
           "initial V4 sample must succeed");
    write_byte(parent_to_child[1], 'a');
    read_byte(child_to_parent[0]);
    const guard::SampleRead first =
        guard::read_process_sample(child);
    expect(first.sample.has_value(),
           "first touched-allocation sample must succeed");
    expect(
        first.sample->identity ==
            initial.sample->identity,
        "V4 identity must remain stable");
    expect(
        first.sample->physical_footprint >
            initial.sample->physical_footprint,
        "touching memory must grow current footprint");
    expect(
        first.sample->lifetime_max_physical_footprint >=
            initial.sample
                ->lifetime_max_physical_footprint,
        "raw lifetime field must be nondecreasing "
        "across the first allocation");

    write_byte(parent_to_child[1], 'b');
    read_byte(child_to_parent[0]);
    const guard::SampleRead released =
        guard::read_process_sample(child);
    expect(released.sample.has_value(),
           "released-allocation sample must succeed");
    expect(
        released.sample->identity ==
            initial.sample->identity,
        "identity must survive release");
    expect(
        released.sample->physical_footprint <
            first.sample->physical_footprint,
        "released mapping must lower current footprint");
    expect(
        released.sample->lifetime_max_physical_footprint >=
            first.sample->lifetime_max_physical_footprint,
        "release must preserve a nondecreasing raw "
        "lifetime observation");

    write_byte(parent_to_child[1], 'c');
    read_byte(child_to_parent[0]);
    const guard::SampleRead second =
        guard::read_process_sample(child);
    expect(second.sample.has_value(),
           "reallocation sample must succeed");
    expect(
        second.sample->identity ==
            initial.sample->identity,
        "identity must survive release/reallocation");
    expect(
        second.sample->lifetime_max_physical_footprint >=
            released.sample
                ->lifetime_max_physical_footprint,
        "raw lifetime observation must be nondecreasing");
    write_byte(parent_to_child[1], 'd');

    siginfo_t information {};
    while (::waitid(
               P_PID, static_cast<id_t>(child),
               &information, WEXITED | WNOWAIT) != 0) {
        if (errno != EINTR) {
            throw std::system_error(
                errno, std::generic_category(),
                "waitid WNOWAIT failed");
        }
    }
    expect(information.si_pid == child,
           "waitid must observe the owned child");
    const guard::SampleRead final =
        guard::read_process_sample(child);
    expect(final.sample.has_value(),
           "unreaped zombie V4 sample must succeed");
    expect(final.sample->exit_abstime != 0,
           "zombie V4 sample must expose exit time");
    expect(
        final.sample->identity ==
            initial.sample->identity,
        "zombie identity must match the live child");
    expect(
        final.sample->lifetime_max_physical_footprint >=
            second.sample
                ->lifetime_max_physical_footprint,
        "zombie raw lifetime observation must remain "
        "nondecreasing");
    int status = 0;
    expect(::waitpid(child, &status, 0) == child,
           "controlled child must reap exactly once");
    expect(WIFEXITED(status) && WEXITSTATUS(status) == 0,
           "controlled child must exit zero");
    expect(
        !guard::read_process_sample(child).sample.has_value(),
        "reaped PID must fail closed");
    expect(
        !guard::read_process_sample(
             std::numeric_limits<pid_t>::max())
             .sample.has_value(),
        "deliberately wrong PID must fail closed");
    ::close(child_to_parent[0]);
    ::close(parent_to_child[1]);
}
#endif

bool file_contains(
    const std::filesystem::path& path,
    std::string_view needle) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    const std::string contents{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    return contents.find(needle) != std::string::npos;
}

guard::Request child_request(
    const std::filesystem::path& log_directory,
    const std::filesystem::path& executable,
    std::string_view mode, int exit_status) {
    return {
        log_directory,
        {
            executable.string(),
            "--supervised-child",
            std::string(mode),
            std::to_string(exit_status),
        },
    };
}

guard::SampleReader fixed_sample_reader(
    const std::filesystem::path& log_directory,
    guard::ResourceSample live_sample);

void append_marker(
    const std::filesystem::path& path) {
    int flags = O_WRONLY | O_APPEND;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    const int descriptor = ::open(path.c_str(), flags);
    if (descriptor < 0) {
        throw std::system_error(
            errno, std::generic_category(),
            "cannot open teardown output");
    }
    try {
        const std::string marker =
            std::string(guard::kPublicationMarker) + "\n";
        std::size_t offset = 0;
        while (offset < marker.size()) {
            const ssize_t count = ::write(
                descriptor, marker.data() + offset,
                marker.size() - offset);
            if (count > 0) {
                offset += static_cast<std::size_t>(count);
            } else if (count < 0 && errno == EINTR) {
                continue;
            } else {
                throw std::system_error(
                    count < 0 ? errno : EIO,
                    std::generic_category(),
                    "cannot append teardown output");
            }
        }
    } catch (...) {
        ::close(descriptor);
        throw;
    }
    if (::close(descriptor) != 0) {
        throw std::system_error(
            errno, std::generic_category(),
            "cannot close teardown output");
    }
}

void test_supervisor_valid_lifecycle(
    const std::filesystem::path& executable) {
    TemporaryDirectory temporary;
    const guard::Request request{
        temporary.path() / "logs",
        {
            executable.string(),
            "--supervised-child",
            "valid",
            "0",
        },
    };
    const guard::Report report =
        guard::supervise_for_test(
            request, unlimited_config(),
            fixed_sample_reader(
                request.log_directory,
                sample(10, 10, 10)));
    if (report.status != 0) {
        throw std::runtime_error(
            "valid supervised child must pass: status " +
            std::to_string(report.status) + ", " +
            report.reason);
    }
    expect(report.exec_succeeded &&
               report.process_group_confirmed,
           "exec and owned process group must be confirmed");
    expect(report.final_sample_after_waitid &&
               report.reaped &&
               report.process_group_quiescent,
           "final sample must precede reap and group quiescence");
    expect(report.marker_count == 1 &&
               report.post_marker_sample_count != 0,
           "valid child must have one marker and a later sample");
    expect(report.target_unchanged &&
               !report.target_sha256_before.empty() &&
               report.target_sha256_before ==
                   report.target_sha256_after,
           "target hashes must be independently retained and equal");
    expect(
        report.marker_observations.size() == 1 &&
            report.marker_observations[0].stream ==
                guard::MarkerStream::standard_output,
        "marker stream and offset must be retained");
    const std::string summary =
        read_file(request.log_directory / "summary.tsv");
    expect(
        summary.find("target_sha256_before\t") !=
                std::string::npos &&
            summary.find("marker_0_stream\tstdout") !=
                std::string::npos &&
            summary.find(
                "physical_limit_bytes\t" +
                std::to_string(
                    std::numeric_limits<
                        std::uint64_t>::max())) !=
                std::string::npos &&
            summary.find("argv_count\t4") !=
                std::string::npos &&
            summary.find("target_physical_path_hex\t") !=
                std::string::npos,
        "summary must contain hashes, config, argv, "
        "physical path, and marker provenance");
    const std::string samples =
        read_file(request.log_directory / "samples.tsv");
    expect(
        samples.starts_with(
            "sequence\tmonotonic_ns\tbegin_epoch"
            "\tcomplete_epoch\t"),
        "sample log must retain begin/complete epochs");
    const std::string events =
        read_file(request.log_directory / "events.tsv");
    expect(
        events.find("\tconfiguration\t") !=
                std::string::npos &&
            events.find("\targument\tindex=0 ") !=
                std::string::npos,
        "events must retain exact config and encoded argv");
}

void test_final_output_scan_after_quiescence(
    const std::filesystem::path& executable) {
    TemporaryDirectory temporary;
    const std::filesystem::path duplicate_logs =
        temporary.path() / "teardown-duplicate";
    const guard::Request duplicate_request =
        child_request(
            duplicate_logs, executable, "valid", 0);
    const guard::Report duplicate =
        guard::supervise_for_test(
            duplicate_request, unlimited_config(),
            fixed_sample_reader(
                duplicate_logs, sample(10, 10, 10)),
            [&]() {
                append_marker(
                    duplicate_logs / "target.stdout");
            });
    expect(
        duplicate.status == guard::kMarkerFailureExit &&
            duplicate.failure == guard::Failure::marker &&
            duplicate.marker_count == 2 &&
            duplicate.reaped &&
            duplicate.process_group_quiescent,
        "a duplicate marker appended during teardown must "
        "be counted after process-group quiescence");
    expect(
        file_contains(
            duplicate_logs / "events.tsv",
            "\tpublication_marker\tstream=stdout ") &&
            duplicate.marker_observations.size() == 2,
        "teardown marker must retain ordinary marker "
        "provenance");

    const std::filesystem::path late_logs =
        temporary.path() / "teardown-late";
    const guard::Request late_request =
        child_request(
            late_logs, executable, "missing", 0);
    const guard::Report late =
        guard::supervise_for_test(
            late_request, unlimited_config(),
            fixed_sample_reader(
                late_logs, sample(10, 10, 10)),
            [&]() {
                append_marker(
                    late_logs / "target.stderr");
            });
    expect(
        late.status == guard::kMarkerFailureExit &&
            late.failure == guard::Failure::marker &&
            late.marker_count == 1 &&
            late.post_marker_sample_count == 0 &&
            late.marker_observations.size() == 1 &&
            late.marker_observations.front().stream ==
                guard::MarkerStream::standard_error &&
            late.reaped &&
            late.process_group_quiescent,
        "a first marker appended during teardown must fail "
        "because no complete sample began afterward");
}

void test_supervisor_exit_and_marker_paths(
    const std::filesystem::path& executable) {
    TemporaryDirectory temporary;
    const guard::Config config = unlimited_config();
    const auto run =
        [&](std::string_view log_name,
            std::string_view mode, int exit_status) {
            const guard::Request request =
                child_request(
                    temporary.path() /
                        std::string(log_name),
                    executable, mode, exit_status);
            return guard::supervise_for_test(
                request, config,
                fixed_sample_reader(
                    request.log_directory,
                    sample(10, 10, 10)));
        };

    const guard::Report exit_one =
        run("exit-one", "valid", 1);
    if (exit_one.status != 1 ||
        exit_one.failure != guard::Failure::none ||
        exit_one.target_exit_code != 1) {
        throw std::runtime_error(
            "ordinary exit 1 must propagate after all gates: "
            "status=" + std::to_string(exit_one.status) +
            " failure=" +
            std::string(
                guard::failure_name(exit_one.failure)) +
            " reason=" + exit_one.reason);
    }

    const guard::Report missing =
        run("missing", "missing", 0);
    expect(
        missing.status == guard::kMarkerFailureExit &&
            missing.failure == guard::Failure::marker &&
            missing.marker_count == 0 &&
            missing.final_sample_after_waitid,
        "missing marker must fail full supervisor as 93");

    const guard::Report duplicate =
        run("duplicate", "duplicate", 0);
    if (duplicate.status !=
            guard::kMarkerFailureExit ||
        duplicate.failure != guard::Failure::marker ||
        duplicate.marker_count != 2 ||
        duplicate.marker_observations.size() != 2 ||
        duplicate.marker_observations[0].stream !=
            guard::MarkerStream::standard_output ||
        duplicate.marker_observations[1].stream !=
            guard::MarkerStream::standard_error) {
        throw std::runtime_error(
            "cross-stream duplicate marker must fail as 93: "
            "status=" + std::to_string(duplicate.status) +
            " failure=" +
            std::string(
                guard::failure_name(duplicate.failure)) +
            " markers=" +
            std::to_string(duplicate.marker_count) +
            " reason=" + duplicate.reason);
    }

    const guard::Report exit_two =
        run("exit-two", "missing", 2);
    expect(
        exit_two.status == guard::kMonitorFailureExit &&
            exit_two.failure ==
                guard::Failure::target_exit &&
            exit_two.target_exit_code == 2,
        "exit 2 must override missing-marker status as 92");

    const guard::Report signaled =
        run("signal", "signal", 0);
    expect(
        signaled.status == guard::kMonitorFailureExit &&
            signaled.failure ==
                guard::Failure::target_exit &&
            signaled.target_signal == SIGTERM &&
            signaled.marker_count == 1,
        "natural target signal must be infrastructure 92");

    const std::filesystem::path non_executable =
        temporary.path() / "not-executable";
    {
        std::ofstream output(
            non_executable, std::ios::binary);
        output << "not an executable\n";
    }
    if (::chmod(non_executable.c_str(), S_IRUSR | S_IWUSR) !=
        0) {
        throw std::system_error(
            errno, std::generic_category(),
            "cannot chmod exec-failure fixture");
    }
    const guard::Request exec_request{
        temporary.path() / "exec-failure",
        {non_executable.string()},
    };
    const guard::Report exec_failure =
        guard::supervise_for_test(
            exec_request, config,
            fixed_sample_reader(
                exec_request.log_directory,
                sample(10, 10, 10)));
    expect(
        exec_failure.status ==
                guard::kMonitorFailureExit &&
            exec_failure.failure ==
                guard::Failure::process_control &&
            !exec_failure.exec_succeeded &&
            exec_failure.target_exit_code == 127 &&
            exec_failure.reaped &&
            exec_failure.process_group_quiescent &&
            exec_failure.target_unchanged,
        "exec failure must be status 92 with cleanup");
}

void test_target_mutation(
    const std::filesystem::path& executable) {
    TemporaryDirectory temporary;
    const std::filesystem::path mutable_target =
        temporary.path() / "mutable-target";
    std::filesystem::copy_file(
        executable, mutable_target,
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::permissions(
        mutable_target,
        std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace);

    const guard::Request request = child_request(
        temporary.path() / "logs", mutable_target,
        "mutate", 0);
    const guard::Report report =
        guard::supervise_for_test(
            request, unlimited_config(),
            fixed_sample_reader(
                request.log_directory,
                sample(10, 10, 10)));
    expect(
        report.status == guard::kMonitorFailureExit &&
            report.failure ==
                guard::Failure::target_integrity &&
            report.target_exit_code == 0 &&
            !report.target_unchanged &&
            !report.target_sha256_before.empty() &&
            !report.target_sha256_after.empty() &&
            report.target_sha256_before !=
                report.target_sha256_after,
        "target replacement must override a valid run as 92");
}

guard::SampleReader fixed_sample_reader(
    const std::filesystem::path& log_directory,
    guard::ResourceSample live_sample) {
    return [log_directory, live_sample](
               pid_t) mutable -> guard::SampleRead {
        guard::ResourceSample result = live_sample;
        if (file_contains(
                log_directory / "events.tsv",
                "\texit_observed_unreaped\t")) {
            result.exit_abstime = 999;
        }
        return {result, 0};
    };
}

void expect_threshold_cleanup(
    const guard::Report& report, int status,
    guard::Failure failure,
    std::string_view description) {
    if (report.status != status ||
        report.failure != failure ||
        report.target_signal != SIGKILL ||
        !report.final_sample_after_waitid ||
        !report.reaped ||
        !report.process_group_quiescent) {
        throw std::runtime_error(
            std::string(description) +
            " did not kill and cleanly reap its process group: "
            "status=" + std::to_string(report.status) +
            " failure=" +
            std::string(
                guard::failure_name(report.failure)) +
            " signal=" +
            (report.target_signal.has_value()
                 ? std::to_string(*report.target_signal)
                 : std::string("none")) +
            " final=" +
            std::to_string(
                report.final_sample_after_waitid) +
            " reaped=" + std::to_string(report.reaped) +
            " quiescent=" +
            std::to_string(
                report.process_group_quiescent) +
            " reason=" + report.reason);
    }
}

void test_injected_absolute_limit_paths(
    const std::filesystem::path& executable) {
    TemporaryDirectory temporary;

    const auto run_physical =
        [&](std::string_view name,
            std::uint64_t value) {
            guard::Config config = unlimited_config();
            config.physical_limit_bytes = 100;
            const std::filesystem::path logs =
                temporary.path() / std::string(name);
            return guard::supervise_for_test(
                child_request(
                    logs, executable, "valid", 0),
                config,
                fixed_sample_reader(
                    logs, sample(value, value, 10)));
        };
    const guard::Report physical_below =
        run_physical("physical-below", 99);
    expect(
        physical_below.status == 0,
        "full path one byte below physical cap must pass");
    const guard::Report physical_exact =
        run_physical("physical-exact", 100);
    expect_threshold_cleanup(
        physical_exact, guard::kPhysicalLimitExit,
        guard::Failure::physical_limit,
        "physical equality");
    const guard::Report physical_over =
        run_physical("physical-over", 101);
    expect_threshold_cleanup(
        physical_over, guard::kPhysicalLimitExit,
        guard::Failure::physical_limit,
        "physical over-limit");

    constexpr std::uint64_t kResidentBoundary =
        guard::kGibibyte;
    const auto run_resident =
        [&](std::string_view name,
            std::uint64_t value) {
            guard::Config config = unlimited_config();
            config.resident_limit_bytes =
                kResidentBoundary;
            const std::filesystem::path logs =
                temporary.path() / std::string(name);
            return guard::supervise_for_test(
                child_request(
                    logs, executable, "valid", 0),
                config,
                fixed_sample_reader(
                    logs, sample(10, 10, value)));
        };
    const guard::Report resident_below =
        run_resident(
            "resident-below", kResidentBoundary - 1);
    expect(
        resident_below.status == 0,
        "full path one byte below resident cap must pass");
    const guard::Report resident_exact =
        run_resident(
            "resident-exact", kResidentBoundary);
    expect_threshold_cleanup(
        resident_exact, guard::kResidentLimitExit,
        guard::Failure::resident_limit,
        "resident equality");
    const guard::Report resident_over =
        run_resident(
            "resident-over", kResidentBoundary + 1);
    expect_threshold_cleanup(
        resident_over, guard::kResidentLimitExit,
        guard::Failure::resident_limit,
        "resident over-limit");
}

void test_injected_publication_delta_paths(
    const std::filesystem::path& executable) {
    TemporaryDirectory temporary;
    const auto run =
        [&](std::string_view name,
            std::uint64_t delta) {
            const std::filesystem::path logs =
                temporary.path() / std::string(name);
            guard::Config config = unlimited_config();
            config.publication_delta_limit_bytes =
                guard::kGibibyte;
            const guard::SampleReader reader =
                [logs, delta](pid_t) {
                    const bool after_marker =
                        file_contains(
                            logs / "target.stdout",
                            guard::kPublicationMarker);
                    const bool final =
                        file_contains(
                            logs / "events.tsv",
                            "\texit_observed_unreaped\t");
                    guard::ResourceSample value =
                        sample(
                            1'000,
                            2'000 +
                                (after_marker
                                     ? delta
                                     : 0),
                            500);
                    value.exit_abstime = final ? 999 : 0;
                    return guard::SampleRead{value, 0};
                };
            return guard::supervise_for_test(
                child_request(
                    logs, executable, "valid", 0),
                config, reader);
        };

    const guard::Report exact =
        run("delta-exact", guard::kGibibyte);
    expect(
        exact.status == 0 &&
            exact.publication_delta_bytes ==
                guard::kGibibyte,
        "full path exact one-GiB delta must pass");

    const guard::Report over =
        run("delta-over", guard::kGibibyte + 1);
    expect_threshold_cleanup(
        over, guard::kPublicationDeltaExit,
        guard::Failure::publication_delta,
        "publication delta over-limit");
    expect(
        over.publication_delta_bytes ==
            guard::kGibibyte + 1,
        "over-limit delta must retain exact magnitude");
}

void test_injected_sample_invariant_cleanup(
    const std::filesystem::path& executable) {
    TemporaryDirectory temporary;

    const std::filesystem::path diagnostic_logs =
        temporary.path() / "diagnostic-envelope";
    std::size_t diagnostic_calls = 0;
    const guard::SampleReader diagnostic_reader =
        [diagnostic_logs, &diagnostic_calls](
            pid_t) mutable -> guard::SampleRead {
            if (file_contains(
                    diagnostic_logs / "events.tsv",
                    "\texit_observed_unreaped\t")) {
                return {
                    sample(0, 12, 10, 123, 7, 999), 0};
            }
            ++diagnostic_calls;
            return {
                diagnostic_calls == 1
                    ? sample(11, 10, 10)
                    : sample(10, 9, 10),
                0};
        };
    const guard::Report diagnostic =
        guard::supervise_for_test(
            child_request(
                diagnostic_logs, executable, "valid", 0),
            unlimited_config(), diagnostic_reader);
    expect(
        diagnostic.status == 0 &&
            diagnostic.failure == guard::Failure::none &&
            diagnostic.effective_peak == 12 &&
            diagnostic.inversion_count >= 2 &&
            diagnostic.maximum_inversion_gap == 1 &&
            diagnostic.lifetime_regression_count == 1 &&
            diagnostic.maximum_lifetime_regression_gap == 1 &&
            diagnostic.publication_baseline_effective == 11 &&
            diagnostic.publication_current_delta == 0 &&
            diagnostic.publication_effective_delta == 1,
        "full supervisor must accept and report raw "
        "inversions/regressions without lowering its envelope");
    expect(
        file_contains(
            diagnostic_logs / "events.tsv",
            "\tphysical_footprint_inversion\t") &&
            file_contains(
                diagnostic_logs / "events.tsv",
                "\traw_lifetime_regression\t") &&
            file_contains(
                diagnostic_logs / "samples.tsv",
                "\tlifetime_regression_gap\n") &&
            file_contains(
                diagnostic_logs / "summary.tsv",
                "physical_peak_semantics\tscalar_envelope\n") &&
            file_contains(
                diagnostic_logs / "summary.tsv",
                "lifetime_regression_count\t1\n"),
        "diagnostic samples, events, and summary must retain "
        "the scalar-envelope evidence");

    struct Case {
        std::string_view name;
        guard::Failure expected;
        std::vector<guard::ResourceSample> live_samples;
    };
    const std::array<Case, 2> cases = {{
        {
            "uuid-change",
            guard::Failure::identity,
            {
                sample(10, 10, 10),
                sample(10, 10, 10, 123, 8),
            },
        },
        {
            "start-change",
            guard::Failure::identity,
            {
                sample(10, 10, 10),
                sample(10, 10, 10, 124, 7),
            },
        },
    }};

    for (const Case& test_case : cases) {
        const std::filesystem::path logs =
            temporary.path() / std::string(test_case.name);
        std::size_t live_index = 0;
        const guard::SampleReader reader =
            [logs, &live_index, &test_case](
                pid_t) mutable -> guard::SampleRead {
                if (file_contains(
                        logs / "events.tsv",
                        "\texit_observed_unreaped\t")) {
                    guard::ResourceSample final =
                        sample(10, 20, 10);
                    final.exit_abstime = 999;
                    return {final, 0};
                }
                const std::size_t index = std::min(
                    live_index,
                    test_case.live_samples.size() - 1);
                ++live_index;
                return {
                    test_case.live_samples[index], 0};
            };
        const guard::Report report =
            guard::supervise_for_test(
                child_request(
                    logs, executable, "missing", 0),
                unlimited_config(), reader);
        expect_threshold_cleanup(
            report, guard::kMonitorFailureExit,
            test_case.expected, test_case.name);
    }
}

void test_real_low_limit_allocator(
    const std::filesystem::path& executable) {
    const guard::SampleRead self =
        guard::read_process_sample(::getpid());
    expect(self.sample.has_value(),
           "allocator test needs a real supervisor sample");
    constexpr std::uint64_t kHeadroom =
        32ULL * 1024ULL * 1024ULL;
    guard::Config config = unlimited_config();
    config.physical_limit_bytes =
        self.sample->physical_footprint + kHeadroom;
    TemporaryDirectory temporary;
    const std::filesystem::path logs =
        temporary.path() / "allocator";
    bool first = true;
    const guard::SampleReader delayed_real_reader =
        [&first](pid_t pid) {
            if (first) {
                first = false;
                ::usleep(300'000);
            }
            return guard::read_process_sample(pid);
        };
    const guard::Report report =
        guard::supervise_for_test(
            child_request(
                logs, executable, "allocator", 0),
            config, delayed_real_reader);
    expect_threshold_cleanup(
        report, guard::kPhysicalLimitExit,
        guard::Failure::physical_limit,
        "real low-limit allocator");
    expect(
        report.maximum_current_physical_footprint >=
                config.physical_limit_bytes &&
            file_contains(
                logs / "target.stdout", "ALLOCATED\n"),
        "allocator must touch memory past the real V4 cap "
        "before being killed");
}

void test_monitor_interruption_cleanup(
    const std::filesystem::path& executable) {
    TemporaryDirectory temporary;
    const std::filesystem::path logs =
        temporary.path() / "interrupted";
    std::size_t calls = 0;
    const guard::SampleReader reader =
        [logs, &calls](pid_t) {
            guard::ResourceSample value =
                sample(10, 10, 10);
            if (file_contains(
                    logs / "events.tsv",
                    "\texit_observed_unreaped\t")) {
                value.exit_abstime = 999;
                return guard::SampleRead{value, 0};
            }
            ++calls;
            if (calls == 2) {
                ::kill(::getpid(), SIGTERM);
            }
            return guard::SampleRead{value, 0};
        };
    const guard::Report report =
        guard::supervise_for_test(
            child_request(
                logs, executable, "missing", 0),
            unlimited_config(), reader);
    expect_threshold_cleanup(
        report, guard::kMonitorFailureExit,
        guard::Failure::interrupted,
        "monitor interruption");
}

void test_threshold_kills_descendant_group(
    const std::filesystem::path& executable) {
    TemporaryDirectory temporary;
    const std::filesystem::path logs =
        temporary.path() / "threshold-descendant";
    guard::Config config = unlimited_config();
    config.physical_limit_bytes = 100;
    bool first = true;
    const guard::SampleReader reader =
        [logs, &first](pid_t) {
            if (first) {
                first = false;
                ::usleep(60'000);
            }
            guard::ResourceSample value =
                sample(100, 100, 10);
            if (file_contains(
                    logs / "events.tsv",
                    "\texit_observed_unreaped\t")) {
                value.exit_abstime = 999;
            }
            return guard::SampleRead{value, 0};
        };
    const guard::Report report =
        guard::supervise_for_test(
            child_request(
                logs, executable, "descendant", 0),
            config, reader);
    expect_threshold_cleanup(
        report, guard::kPhysicalLimitExit,
        guard::Failure::physical_limit,
        "threshold descendant group");
    const std::string output =
        read_file(logs / "target.stdout");
    const std::size_t tab = output.find('\t');
    const std::size_t newline = output.find('\n');
    expect(
        output.starts_with("DESCENDANT\t") &&
            tab != std::string::npos &&
            newline != std::string::npos,
        "threshold fixture must report its descendant");
    const pid_t descendant = static_cast<pid_t>(
        std::stol(output.substr(
            tab + 1, newline - tab - 1)));
    expect_absent_or_terminal_and_logged(
        descendant, logs / "events.tsv",
        "threshold-killed descendant");
}

void test_injected_failure_kills_group(
    const std::filesystem::path& executable) {
    TemporaryDirectory temporary;
    const guard::Request request{
        temporary.path() / "logs",
        {
            executable.string(),
            "--supervised-child",
            "descendant",
            "0",
        },
    };
    std::size_t calls = 0;
    const guard::SampleReader reader =
        [&](pid_t) -> guard::SampleRead {
            ++calls;
            if (calls == 1) {
                ::usleep(60'000);
                return {sample(10, 10, 10), 0};
            }
            if (calls == 2) {
                return {std::nullopt, EIO};
            }
            return {sample(10, 10, 10, 123, 7, 999), 0};
        };
    const guard::Report report =
        guard::supervise_for_test(
            request, unlimited_config(), reader);
    if (!report.process_group_quiescent) {
        throw std::runtime_error(
            "post-start EIO process-group cleanup log:\n" +
            read_file(
                request.log_directory / "events.tsv"));
    }
    expect_threshold_cleanup(
        report, guard::kMonitorFailureExit,
        guard::Failure::sampler,
        "post-start EIO");

    const std::string output =
        read_file(request.log_directory / "target.stdout");
    const std::size_t tab = output.find('\t');
    const std::size_t newline = output.find('\n');
    expect(
        output.starts_with("DESCENDANT\t") &&
            tab != std::string::npos &&
            newline != std::string::npos,
        "descendant PID must be reported");
    const pid_t descendant = static_cast<pid_t>(
        std::stol(output.substr(
            tab + 1, newline - tab - 1)));
    expect_absent_or_terminal_and_logged(
        descendant,
        request.log_directory / "events.tsv",
        "monitor-failure-killed descendant");
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 4 &&
        std::string_view(argv[1]) ==
            "--supervised-child") {
        return supervised_child(
            argv[0], argv[2], std::stoi(argv[3]));
    }

    const std::filesystem::path executable =
        std::filesystem::absolute(argv[0])
            .lexically_normal();
    TestRunner runner;
    runner.run("streaming marker scanner", test_marker_scanner);
    runner.run(
        "identity and monotone samples",
        test_identity_and_monotone_samples);
    runner.run(
        "invalid sample relations",
        test_invalid_sample_relations);
    runner.run(
        "process-group member classification",
        test_process_group_member_classification);
    runner.run(
        "absolute limit boundaries",
        test_absolute_limit_boundaries);
    runner.run(
        "publication delta boundary",
        test_publication_delta_boundary);
    runner.run(
        "marker and lifecycle contracts",
        test_marker_and_lifecycle_contracts);
    runner.run(
        "infrastructure failure precedence",
        test_infrastructure_failure_precedence);
#if defined(__APPLE__)
    runner.run(
        "real V4 allocation and zombie sample",
        test_real_rusage_and_zombie_sample);
    runner.run(
        "valid supervisor lifecycle",
        [&]() {
            test_supervisor_valid_lifecycle(executable);
        });
    runner.run(
        "final output scan after quiescence",
        [&]() {
            test_final_output_scan_after_quiescence(
                executable);
        });
    runner.run(
        "supervisor exit and marker paths",
        [&]() {
            test_supervisor_exit_and_marker_paths(
                executable);
        });
    runner.run(
        "target mutation",
        [&]() {
            test_target_mutation(executable);
        });
    runner.run(
        "injected absolute limit paths",
        [&]() {
            test_injected_absolute_limit_paths(
                executable);
        });
    runner.run(
        "injected publication delta paths",
        [&]() {
            test_injected_publication_delta_paths(
                executable);
        });
    runner.run(
        "injected sample invariant cleanup",
        [&]() {
            test_injected_sample_invariant_cleanup(
                executable);
        });
    runner.run(
        "real low-limit allocator",
        [&]() {
            test_real_low_limit_allocator(executable);
        });
    runner.run(
        "monitor interruption cleanup",
        [&]() {
            test_monitor_interruption_cleanup(
                executable);
        });
    runner.run(
        "threshold kills descendant group",
        [&]() {
            test_threshold_kills_descendant_group(
                executable);
        });
    runner.run(
        "injected failure kills process group",
        [&]() {
            test_injected_failure_kills_group(executable);
        });
#endif
    return runner.finish();
}
