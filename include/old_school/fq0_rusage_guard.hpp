#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace old_school::fq0_rusage_guard {

constexpr std::uint64_t kGibibyte = 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kPhysicalLimitBytes = 20ULL * kGibibyte;
constexpr std::uint64_t kResidentLimitBytes = 16ULL * kGibibyte;
constexpr std::uint64_t kPublicationDeltaLimitBytes = kGibibyte;
constexpr std::string_view kPublicationMarker =
    "FQ0 publication: report validation complete; streaming evidence";

constexpr int kPhysicalLimitExit = 90;
constexpr int kResidentLimitExit = 91;
constexpr int kMonitorFailureExit = 92;
constexpr int kMarkerFailureExit = 93;
constexpr int kPublicationDeltaExit = 94;
constexpr int kUsageExit = 95;

// The production CLI always uses the constants above. This configuration is
// public only so focused integration tests can exercise the same supervisor
// with harmless low limits and a short polling interval.
struct Config {
    std::uint64_t physical_limit_bytes = kPhysicalLimitBytes;
    std::uint64_t resident_limit_bytes = kResidentLimitBytes;
    std::uint64_t publication_delta_limit_bytes =
        kPublicationDeltaLimitBytes;
    std::chrono::milliseconds sample_period{250};
};

struct ProcessIdentity {
    std::array<std::uint8_t, 16> uuid{};
    std::uint64_t start_abstime = 0;

    bool operator==(const ProcessIdentity&) const = default;
};

struct ResourceSample {
    ProcessIdentity identity;
    std::uint64_t exit_abstime = 0;
    std::uint64_t physical_footprint = 0;
    std::uint64_t lifetime_max_physical_footprint = 0;
    std::uint64_t resident_size = 0;

    bool operator==(const ResourceSample&) const = default;
};

struct SampleRead {
    std::optional<ResourceSample> sample;
    int error_number = 0;
};

using SampleReader = std::function<SampleRead(pid_t)>;

// Uses macOS proc_pid_rusage(RUSAGE_INFO_V4). On unsupported platforms the
// read fails with ENOTSUP. A zombie that has not yet been reaped remains a
// valid target on macOS.
SampleRead read_process_sample(pid_t pid);

// Deterministic process-group classification seam shared by the native
// supervisor and focused lifecycle tests. ESRCH means the listed PID raced
// away; an otherwise successful sample is execution-quiescent only when its
// kernel-reported process-exit time is nonzero.
struct ProcessGroupMemberRead {
    pid_t pid = -1;
    SampleRead reading;
};

struct TerminalProcessGroupMember {
    pid_t pid = -1;
    ResourceSample sample;
};

struct ProcessGroupClassification {
    bool execution_quiescent = false;
    pid_t live_pid = -1;
    int error_number = 0;
    std::vector<TerminalProcessGroupMember>
        terminal_members;
};

ProcessGroupClassification classify_process_group_members(
    const std::vector<ProcessGroupMemberRead>& members);

enum class Failure {
    none,
    sampler,
    identity,
    sample_order,
    lifetime_peak_regression,
    physical_limit,
    resident_limit,
    publication_delta,
    marker,
    process_control,
    target_integrity,
    target_exit,
    interrupted,
};

int exit_code(Failure failure);
std::string_view failure_name(Failure failure);

class MarkerScanner {
  public:
    explicit MarkerScanner(std::string marker);

    // Returns the zero-based byte offset of every newly completed
    // occurrence. Matching is streaming and therefore handles arbitrary
    // chunk boundaries.
    std::vector<std::uint64_t> feed(std::string_view bytes);
    std::size_t count() const;

  private:
    std::string marker_;
    std::vector<std::size_t> prefix_;
    std::size_t matched_ = 0;
    std::size_t count_ = 0;
    std::uint64_t bytes_seen_ = 0;
};

enum class MarkerStream {
    standard_output,
    standard_error,
};

std::string_view marker_stream_name(MarkerStream stream);

struct MarkerObservation {
    std::uint64_t epoch = 0;
    MarkerStream stream = MarkerStream::standard_output;
    std::uint64_t byte_offset = 0;
};

class GateState {
  public:
    explicit GateState(Config config);

    void begin_sample(std::uint64_t epoch);
    void complete_sample_failure(std::uint64_t epoch);
    void complete_sample(
        const ResourceSample& sample,
        std::uint64_t epoch);
    void observe_marker(const MarkerObservation& observation);
    void observe_waitid_exit(std::uint64_t epoch);
    void observe_reaped(std::uint64_t epoch);
    void observe_group_quiescent(std::uint64_t epoch);
    void observe_failure(Failure failure);
    void finish();

    Failure failure() const;
    const std::string& reason() const;
    const std::optional<ResourceSample>& last_sample() const;
    std::size_t sample_count() const;
    std::size_t marker_count() const;
    std::size_t post_marker_sample_count() const;
    std::uint64_t publication_delta_bytes() const;
    std::uint64_t effective_peak() const;
    std::size_t inversion_count() const;
    std::uint64_t maximum_inversion_gap() const;
    std::size_t lifetime_regression_count() const;
    std::uint64_t maximum_lifetime_regression_gap() const;
    std::uint64_t last_inversion_gap() const;
    std::uint64_t last_lifetime_regression_gap() const;
    bool marker_seen() const;
    bool final_sample_after_waitid() const;
    bool reaped() const;
    bool group_quiescent() const;
    const std::vector<MarkerObservation>&
    marker_observations() const;
    const std::optional<ResourceSample>&
    publication_baseline() const;
    const std::optional<std::uint64_t>&
    publication_baseline_effective() const;
    std::uint64_t maximum_post_marker_current() const;
    std::uint64_t maximum_post_marker_effective() const;
    std::uint64_t publication_current_delta() const;
    std::uint64_t publication_effective_delta() const;

  private:
    void fail(Failure failure, std::string reason);
    void enforce_limits(const ResourceSample& sample);
    void enforce_publication_delta(const ResourceSample& sample);
    bool accept_epoch(std::uint64_t epoch);

    Config config_;
    Failure failure_ = Failure::none;
    std::string reason_;
    std::optional<ProcessIdentity> identity_;
    std::optional<ResourceSample> last_sample_;
    std::optional<ResourceSample> publication_baseline_;
    std::optional<std::uint64_t>
        publication_baseline_effective_;
    std::vector<MarkerObservation> marker_observations_;
    std::size_t sample_count_ = 0;
    std::size_t marker_count_ = 0;
    std::size_t post_marker_sample_count_ = 0;
    std::uint64_t effective_peak_ = 0;
    std::size_t inversion_count_ = 0;
    std::uint64_t maximum_inversion_gap_ = 0;
    std::size_t lifetime_regression_count_ = 0;
    std::uint64_t maximum_lifetime_regression_gap_ = 0;
    std::uint64_t last_inversion_gap_ = 0;
    std::uint64_t last_lifetime_regression_gap_ = 0;
    std::uint64_t maximum_post_marker_current_ = 0;
    std::uint64_t maximum_post_marker_effective_ = 0;
    std::uint64_t publication_current_delta_ = 0;
    std::uint64_t publication_effective_delta_ = 0;
    std::uint64_t publication_delta_bytes_ = 0;
    std::uint64_t last_observation_epoch_ = 0;
    std::optional<std::uint64_t> marker_epoch_;
    std::optional<std::uint64_t> waitid_epoch_;
    std::optional<std::uint64_t> final_sample_epoch_;
    std::optional<std::uint64_t> reap_epoch_;
    std::optional<std::uint64_t> group_quiescent_epoch_;
    std::optional<std::uint64_t> active_sample_begin_epoch_;
};

struct Request {
    std::filesystem::path log_directory;
    std::vector<std::string> command;
};

struct Report {
    int status = kMonitorFailureExit;
    Failure failure = Failure::process_control;
    std::string reason;
    std::optional<int> target_exit_code;
    std::optional<int> target_signal;
    pid_t child_pid = -1;
    bool process_group_confirmed = false;
    bool exec_succeeded = false;
    bool final_sample_after_waitid = false;
    bool reaped = false;
    bool process_group_quiescent = false;
    bool target_unchanged = false;
    std::size_t sample_count = 0;
    std::size_t marker_count = 0;
    std::size_t post_marker_sample_count = 0;
    std::uint64_t publication_delta_bytes = 0;
    std::uint64_t effective_peak = 0;
    std::size_t inversion_count = 0;
    std::uint64_t maximum_inversion_gap = 0;
    std::size_t lifetime_regression_count = 0;
    std::uint64_t maximum_lifetime_regression_gap = 0;
    std::uint64_t maximum_current_physical_footprint = 0;
    std::uint64_t maximum_lifetime_physical_footprint = 0;
    std::uint64_t maximum_resident_size = 0;
    std::uint64_t wait4_max_resident_size = 0;
    std::optional<std::uint64_t> publication_baseline_current;
    std::optional<std::uint64_t> publication_baseline_effective;
    std::uint64_t maximum_post_marker_current = 0;
    std::uint64_t maximum_post_marker_effective = 0;
    std::uint64_t publication_current_delta = 0;
    std::uint64_t publication_effective_delta = 0;
    std::string target_sha256_before;
    std::string target_sha256_after;
    std::vector<MarkerObservation> marker_observations;
};

Report supervise(const Request& request, const Config& config);
Report supervise_for_test(
    const Request& request, const Config& config,
    SampleReader sample_reader);
Report supervise_for_test(
    const Request& request, const Config& config,
    SampleReader sample_reader,
    std::function<void()> before_group_quiescence);

// Production entry point. Its only accepted form is:
//   --log-dir PATH -- ABSOLUTE_COMMAND [ARG ...]
// The production memory limits are fixed and have no CLI override.
int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error);

} // namespace old_school::fq0_rusage_guard
