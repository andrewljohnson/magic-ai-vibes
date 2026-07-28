#include "old_school/fq0_rusage_guard.hpp"

#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <optional>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utility>

#if defined(__APPLE__)
#include <libproc.h>
#endif

namespace old_school::fq0_rusage_guard {
namespace {

namespace integrity = old_school::artifact_integrity;

volatile sig_atomic_t interrupted_signal = 0;

void record_signal(int signal_number) {
    interrupted_signal = signal_number;
}

class SignalScope {
  public:
    SignalScope() {
        interrupted_signal = 0;
        struct sigaction action {};
        action.sa_handler = record_signal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;

        std::size_t installed = 0;
        for (; installed < signals_.size(); ++installed) {
            if (::sigaction(
                    signals_[installed], &action,
                    &old_actions_[installed]) != 0) {
                const int error = errno;
                while (installed != 0) {
                    --installed;
                    ::sigaction(
                        signals_[installed],
                        &old_actions_[installed], nullptr);
                }
                throw std::system_error(
                    error, std::generic_category(),
                    "cannot install supervisor signal handler");
            }
        }
        installed_ = true;
    }

    SignalScope(const SignalScope&) = delete;
    SignalScope& operator=(const SignalScope&) = delete;

    ~SignalScope() {
        if (!installed_) {
            return;
        }
        for (std::size_t index = 0;
             index < signals_.size(); ++index) {
            ::sigaction(
                signals_[index], &old_actions_[index], nullptr);
        }
        interrupted_signal = 0;
    }

  private:
    const std::array<int, 3> signals_ = {
        SIGINT,
        SIGTERM,
        SIGHUP,
    };
    std::array<struct sigaction, 3> old_actions_{};
    bool installed_ = false;
};

class FileDescriptor {
  public:
    FileDescriptor() = default;

    explicit FileDescriptor(int descriptor)
        : descriptor_(descriptor) {}

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {}

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            reset();
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }

    ~FileDescriptor() {
        reset();
    }

    int get() const {
        return descriptor_;
    }

    int release() {
        return std::exchange(descriptor_, -1);
    }

    void reset(int descriptor = -1) {
        if (descriptor_ >= 0) {
            while (::close(descriptor_) != 0 && errno == EINTR) {
            }
        }
        descriptor_ = descriptor;
    }

  private:
    int descriptor_ = -1;
};

class ChildOwner {
  public:
    explicit ChildOwner(pid_t pid)
        : pid_(pid) {}

    ChildOwner(const ChildOwner&) = delete;
    ChildOwner& operator=(const ChildOwner&) = delete;

    ~ChildOwner() {
        if (pid_ <= 0 || reaped_) {
            return;
        }
        ::kill(-pid_, SIGKILL);
        ::kill(pid_, SIGKILL);
        int status = 0;
        while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
        }
    }

    void mark_reaped() {
        reaped_ = true;
    }

  private:
    pid_t pid_;
    bool reaped_ = false;
};

[[noreturn]] void throw_system_call(
    std::string_view operation, int error = errno) {
    throw std::system_error(
        error, std::generic_category(), std::string(operation));
}

void write_all(int descriptor, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::write(
            descriptor, bytes.data() + offset,
            bytes.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        throw_system_call("cannot write supervisor log");
    }
}

void sync_file(int descriptor) {
    while (::fsync(descriptor) != 0) {
        if (errno == EINTR) {
            continue;
        }
        throw_system_call("cannot synchronize supervisor log");
    }
}

std::string sanitize_tsv(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character == '\t' || character == '\n' ||
            character == '\r') {
            character = ' ';
        }
    }
    return result;
}

std::uint64_t monotonic_nanoseconds() {
    struct timespec now {};
    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        throw_system_call("cannot read monotonic clock");
    }
    constexpr std::uint64_t kNanosecondsPerSecond =
        1'000'000'000ULL;
    return static_cast<std::uint64_t>(now.tv_sec) *
               kNanosecondsPerSecond +
           static_cast<std::uint64_t>(now.tv_nsec);
}

std::string uuid_hex(
    const std::array<std::uint8_t, 16>& uuid) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint8_t byte : uuid) {
        output << std::setw(2)
               << static_cast<unsigned int>(byte);
    }
    return output.str();
}

std::string bytes_hex(std::string_view bytes) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const unsigned char byte : bytes) {
        output << std::setw(2)
               << static_cast<unsigned int>(byte);
    }
    return output.str();
}

void create_log_directory(
    const std::filesystem::path& path) {
    if (path.empty() || !path.is_absolute()) {
        throw std::invalid_argument(
            "log directory must be an absolute path");
    }
    if (::mkdir(path.c_str(), S_IRWXU) != 0) {
        throw_system_call(
            "cannot create exclusive supervisor log directory '" +
            path.string() + "'");
    }

    struct stat status {};
    if (::lstat(path.c_str(), &status) != 0) {
        throw_system_call(
            "cannot inspect supervisor log directory");
    }
    if (!S_ISDIR(status.st_mode) ||
        status.st_uid != ::geteuid() ||
        (status.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        throw std::runtime_error(
            "supervisor log directory is not private");
    }
}

FileDescriptor open_exclusive_log(
    const std::filesystem::path& path) {
    int flags = O_RDWR | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    while (true) {
        const int descriptor =
            ::open(path.c_str(), flags, S_IRUSR | S_IWUSR);
        if (descriptor >= 0) {
            return FileDescriptor(descriptor);
        }
        if (errno == EINTR) {
            continue;
        }
        throw_system_call(
            "cannot create exclusive supervisor log '" +
            path.string() + "'");
    }
}

void set_close_on_exec(int descriptor) {
    const int old_flags = ::fcntl(descriptor, F_GETFD);
    if (old_flags < 0) {
        throw_system_call("cannot inspect descriptor flags");
    }
    if (::fcntl(
            descriptor, F_SETFD, old_flags | FD_CLOEXEC) != 0) {
        throw_system_call("cannot set close-on-exec");
    }
}

std::array<FileDescriptor, 2> make_exec_pipe() {
    std::array<int, 2> raw = {-1, -1};
    if (::pipe(raw.data()) != 0) {
        throw_system_call("cannot create exec-status pipe");
    }
    std::array<FileDescriptor, 2> result = {
        FileDescriptor(raw[0]),
        FileDescriptor(raw[1]),
    };
    set_close_on_exec(result[0].get());
    set_close_on_exec(result[1].get());
    return result;
}

enum class ChildLaunchStage : int {
    process_group = 1,
    standard_output = 2,
    standard_error = 3,
    execute = 4,
};

struct ChildLaunchFailure {
    int stage;
    int error_number;
};

void child_report_failure(
    int descriptor, ChildLaunchStage stage,
    int error_number) {
    const ChildLaunchFailure failure = {
        static_cast<int>(stage),
        error_number,
    };
    const char* bytes =
        reinterpret_cast<const char*>(&failure);
    std::size_t offset = 0;
    while (offset < sizeof(failure)) {
        const ssize_t count = ::write(
            descriptor, bytes + offset,
            sizeof(failure) - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

std::optional<ChildLaunchFailure> read_exec_failure(
    int descriptor) {
    ChildLaunchFailure failure{};
    char* bytes = reinterpret_cast<char*>(&failure);
    std::size_t offset = 0;
    while (offset < sizeof(failure)) {
        const ssize_t count = ::read(
            descriptor, bytes + offset,
            sizeof(failure) - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count == 0) {
            if (offset == 0) {
                return std::nullopt;
            }
            throw std::runtime_error(
                "exec-status pipe closed with a partial record");
        }
        if (errno == EINTR) {
            continue;
        }
        throw_system_call("cannot read exec-status pipe");
    }

    char extra = '\0';
    while (true) {
        const ssize_t count = ::read(descriptor, &extra, 1);
        if (count == 0) {
            break;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            throw_system_call("cannot finish exec-status read");
        }
        throw std::runtime_error(
            "exec-status pipe contained multiple records");
    }
    return failure;
}

std::string child_launch_stage_name(int stage) {
    switch (static_cast<ChildLaunchStage>(stage)) {
    case ChildLaunchStage::process_group:
        return "setpgid";
    case ChildLaunchStage::standard_output:
        return "stdout redirect";
    case ChildLaunchStage::standard_error:
        return "stderr redirect";
    case ChildLaunchStage::execute:
        return "exec";
    }
    return "unknown child launch stage";
}

void sleep_for(std::chrono::milliseconds duration) {
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            duration);
    struct timespec requested {
        static_cast<time_t>(
            nanoseconds.count() / 1'000'000'000LL),
        static_cast<long>(
            nanoseconds.count() % 1'000'000'000LL),
    };
    while (::nanosleep(&requested, &requested) != 0) {
        if (errno == EINTR) {
            if (interrupted_signal != 0) {
                return;
            }
            continue;
        }
        throw_system_call("supervisor polling sleep failed");
    }
}

bool waitid_observed_exit(
    pid_t pid, bool block, int& error_number) {
    siginfo_t information {};
    const int options =
        WEXITED | WNOWAIT | (block ? 0 : WNOHANG);
    while (::waitid(
               P_PID, static_cast<id_t>(pid),
               &information, options) != 0) {
        if (errno == EINTR) {
            if (interrupted_signal != 0 && !block) {
                error_number = EINTR;
                return false;
            }
            continue;
        }
        error_number = errno;
        return false;
    }
    error_number = 0;
    return information.si_pid == pid;
}

struct KillOutcome {
    bool success = false;
    int group_error = 0;
    int leader_error = 0;
};

KillOutcome kill_owned_process_group(pid_t pid) {
    if (::kill(-pid, SIGKILL) == 0) {
        return {true, 0, 0};
    }
    const int group_error = errno;
    if (group_error != ESRCH) {
        return {false, group_error, 0};
    }
    if (::kill(pid, SIGKILL) == 0) {
        return {true, group_error, 0};
    }
    const int leader_error = errno;
    return {
        leader_error == ESRCH,
        group_error,
        leader_error,
    };
}

std::string make_reason(
    std::string_view prefix, int error_number);

enum class ProcessGroupExistence {
    absent,
    present,
    error,
};

struct ProcessGroupExistenceRead {
    ProcessGroupExistence existence =
        ProcessGroupExistence::error;
    int error_number = 0;
};

ProcessGroupExistenceRead process_group_existence(
    pid_t process_group) {
    if (::kill(-process_group, 0) == 0) {
        return {ProcessGroupExistence::present, 0};
    }
    const int error_number = errno;
    if (error_number == ESRCH) {
        return {ProcessGroupExistence::absent, 0};
    }
    if (error_number == EPERM) {
        return {ProcessGroupExistence::present, 0};
    }
    return {ProcessGroupExistence::error, error_number};
}

struct ProcessGroupInspection {
    bool execution_quiescent = false;
    pid_t live_pid = -1;
    int error_number = 0;
    std::vector<TerminalProcessGroupMember>
        terminal_members;
};

#if defined(__APPLE__)
struct ProcessGroupMemberList {
    std::optional<std::vector<pid_t>> members;
    int error_number = 0;
};

ProcessGroupMemberList list_process_group_members(
    pid_t process_group) {
    constexpr std::size_t kCapacityCushion = 16;
    constexpr int kListAttempts = 4;
    std::size_t minimum_capacity = kCapacityCushion;

    for (int attempt = 0;
         attempt < kListAttempts; ++attempt) {
        errno = 0;
        const int estimated_count =
            ::proc_listpgrppids(
                process_group, nullptr, 0);
        const int estimate_error = errno;
        if (estimated_count < 0 ||
            (estimated_count == 0 &&
             estimate_error != 0)) {
            return {
                std::nullopt,
                estimate_error != 0
                    ? estimate_error
                    : EIO,
            };
        }

        const std::size_t estimate =
            static_cast<std::size_t>(estimated_count);
        if (estimate >
            std::numeric_limits<std::size_t>::max() -
                kCapacityCushion) {
            return {std::nullopt, EOVERFLOW};
        }
        const std::size_t capacity = std::max(
            minimum_capacity,
            estimate + kCapacityCushion);
        constexpr std::size_t kMaximumPidCount =
            static_cast<std::size_t>(
                std::numeric_limits<int>::max()) /
            sizeof(pid_t);
        if (capacity > kMaximumPidCount) {
            return {std::nullopt, EOVERFLOW};
        }

        std::vector<pid_t> members(capacity);
        errno = 0;
        const int listed_count =
            ::proc_listpgrppids(
                process_group, members.data(),
                static_cast<int>(
                    capacity * sizeof(pid_t)));
        const int list_error = errno;
        if (listed_count < 0 ||
            (listed_count == 0 && list_error != 0)) {
            return {
                std::nullopt,
                list_error != 0 ? list_error : EIO,
            };
        }
        const std::size_t count =
            static_cast<std::size_t>(listed_count);
        if (count >= capacity) {
            if (capacity >
                kMaximumPidCount / 2) {
                return {std::nullopt, EOVERFLOW};
            }
            minimum_capacity = capacity * 2;
            continue;
        }

        members.resize(count);
        std::erase_if(
            members,
            [](pid_t pid) { return pid <= 0; });
        std::sort(members.begin(), members.end());
        members.erase(
            std::unique(members.begin(), members.end()),
            members.end());
        return {std::move(members), 0};
    }
    return {std::nullopt, EAGAIN};
}

void remember_terminal_member(
    std::vector<TerminalProcessGroupMember>& members,
    pid_t pid, const ResourceSample& sample) {
    const auto existing = std::find_if(
        members.begin(), members.end(),
        [&](const TerminalProcessGroupMember& member) {
            return member.pid == pid &&
                   member.sample.identity ==
                       sample.identity;
        });
    if (existing == members.end()) {
        members.push_back({pid, sample});
    }
}

ProcessGroupInspection inspect_process_group(
    pid_t process_group) {
    constexpr int kStablePasses = 4;
    std::optional<std::vector<pid_t>>
        prior_terminal_members;
    ProcessGroupInspection result;

    for (int pass = 0; pass < kStablePasses; ++pass) {
        const ProcessGroupMemberList listed =
            list_process_group_members(process_group);
        if (!listed.members.has_value()) {
            result.error_number = listed.error_number;
            return result;
        }

        std::vector<ProcessGroupMemberRead> readings;
        readings.reserve(listed.members->size());
        for (const pid_t pid : *listed.members) {
            readings.push_back(
                {pid, read_process_sample(pid)});
        }
        const ProcessGroupClassification classification =
            classify_process_group_members(readings);
        for (const TerminalProcessGroupMember& member :
             classification.terminal_members) {
            remember_terminal_member(
                result.terminal_members, member.pid,
                member.sample);
        }
        if (classification.error_number != 0) {
            result.error_number =
                classification.error_number;
            return result;
        }
        if (!classification.execution_quiescent) {
            result.live_pid = classification.live_pid;
            return result;
        }

        if (prior_terminal_members.has_value() &&
            std::includes(
                prior_terminal_members->begin(),
                prior_terminal_members->end(),
                listed.members->begin(),
                listed.members->end())) {
            result.execution_quiescent = true;
            return result;
        }
        prior_terminal_members = *listed.members;
    }

    result.error_number = EAGAIN;
    return result;
}
#endif

struct ProcessGroupQuiescence {
    bool execution_quiescent = false;
    std::string failure_detail;
    std::vector<TerminalProcessGroupMember>
        terminal_members;
};

void merge_terminal_members(
    std::vector<TerminalProcessGroupMember>& destination,
    const std::vector<TerminalProcessGroupMember>& source) {
    for (const TerminalProcessGroupMember& member : source) {
        const auto existing = std::find_if(
            destination.begin(), destination.end(),
            [&](const TerminalProcessGroupMember& known) {
                return known.pid == member.pid &&
                       known.sample.identity ==
                           member.sample.identity;
            });
        if (existing == destination.end()) {
            destination.push_back(member);
        }
    }
}

ProcessGroupQuiescence make_process_group_quiescent(
    pid_t process_group) {
    ProcessGroupQuiescence result;
    constexpr int kChecks = 200;

    for (int check = 0; check < kChecks; ++check) {
        const ProcessGroupExistenceRead existence =
            process_group_existence(process_group);
        if (existence.existence ==
            ProcessGroupExistence::absent) {
            result.execution_quiescent = true;
            return result;
        }
        if (existence.existence ==
            ProcessGroupExistence::error) {
            result.failure_detail =
                make_reason(
                    "process-group existence check failed",
                    existence.error_number);
            return result;
        }

#if defined(__APPLE__)
        const ProcessGroupInspection inspection =
            inspect_process_group(process_group);
        merge_terminal_members(
            result.terminal_members,
            inspection.terminal_members);
        if (inspection.execution_quiescent) {
            result.execution_quiescent = true;
            return result;
        }
        if (inspection.error_number != 0) {
            const ProcessGroupExistenceRead after_error =
                process_group_existence(process_group);
            if (after_error.existence ==
                ProcessGroupExistence::absent) {
                result.execution_quiescent = true;
                return result;
            }
            result.failure_detail =
                make_reason(
                    "process-group member inspection failed",
                    inspection.error_number);
            return result;
        }
        result.failure_detail =
            "execution-capable process-group member pid=" +
            std::to_string(inspection.live_pid);
#else
        result.failure_detail =
            "process group contains an unclassified member";
#endif

        if (::kill(-process_group, SIGKILL) != 0) {
            const int kill_error = errno;
            if (kill_error == ESRCH) {
                continue;
            }
            result.failure_detail =
                make_reason(
                    "live process-group SIGKILL failed",
                    kill_error);
            return result;
        }

        struct timespec delay {0, 10'000'000L};
        while (::nanosleep(&delay, &delay) != 0 &&
               errno == EINTR) {
        }
    }

    const ProcessGroupExistenceRead existence =
        process_group_existence(process_group);
    if (existence.existence ==
        ProcessGroupExistence::absent) {
        result.execution_quiescent = true;
        result.failure_detail.clear();
    } else if (existence.existence ==
               ProcessGroupExistence::error) {
        result.failure_detail =
            make_reason(
                "final process-group existence check failed",
                existence.error_number);
#if defined(__APPLE__)
    } else {
        const ProcessGroupInspection inspection =
            inspect_process_group(process_group);
        merge_terminal_members(
            result.terminal_members,
            inspection.terminal_members);
        if (inspection.execution_quiescent) {
            result.execution_quiescent = true;
            result.failure_detail.clear();
        } else if (inspection.error_number != 0) {
            result.failure_detail =
                make_reason(
                    "final process-group member inspection "
                    "failed",
                    inspection.error_number);
        } else {
            result.failure_detail =
                "execution-capable process-group member "
                "remained after bounded SIGKILL retries "
                "pid=" +
                std::to_string(inspection.live_pid);
        }
#endif
    }
    return result;
}

void read_appended(
    int descriptor, std::uint64_t& offset,
    const auto& consume) {
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t count = ::pread(
            descriptor, buffer.data(), buffer.size(),
            static_cast<off_t>(offset));
        if (count > 0) {
            const std::size_t byte_count =
                static_cast<std::size_t>(count);
            consume(std::string_view(
                buffer.data(), byte_count));
            offset += static_cast<std::uint64_t>(byte_count);
            continue;
        }
        if (count == 0) {
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        throw_system_call("cannot scan target output");
    }
}

std::uint64_t positive_difference(
    std::uint64_t later, std::uint64_t earlier) {
    return later > earlier ? later - earlier : 0;
}

void append_event(
    int descriptor, std::string_view event,
    std::string_view detail) {
    std::ostringstream line;
    line << monotonic_nanoseconds() << '\t'
         << sanitize_tsv(event) << '\t'
         << sanitize_tsv(detail) << '\n';
    write_all(descriptor, line.str());
}

void append_sample(
    int descriptor, std::size_t sequence,
    const ResourceSample& sample, bool final_sample,
    bool began_after_marker,
    std::uint64_t begin_epoch,
    std::uint64_t complete_epoch,
    std::uint64_t effective_peak,
    std::uint64_t inversion_gap,
    std::uint64_t lifetime_regression_gap) {
    std::ostringstream line;
    line << sequence << '\t'
         << monotonic_nanoseconds() << '\t'
         << begin_epoch << '\t'
         << complete_epoch << '\t'
         << (final_sample ? 1 : 0) << '\t'
         << (began_after_marker ? 1 : 0) << '\t'
         << uuid_hex(sample.identity.uuid) << '\t'
         << sample.identity.start_abstime << '\t'
         << sample.exit_abstime << '\t'
         << sample.physical_footprint << '\t'
         << sample.lifetime_max_physical_footprint << '\t'
         << sample.resident_size << '\t'
         << effective_peak << '\t'
         << inversion_gap << '\t'
         << lifetime_regression_gap << '\n';
    write_all(descriptor, line.str());
}

std::string make_reason(
    std::string_view prefix, int error_number) {
    return std::string(prefix) + ": " +
           std::generic_category().message(error_number);
}

} // namespace

SampleRead read_process_sample(pid_t pid) {
    if (pid <= 0) {
        return {std::nullopt, EINVAL};
    }
#if defined(__APPLE__)
    struct rusage_info_v4 raw {};
    int result = 0;
    do {
        result = ::proc_pid_rusage(
            pid, RUSAGE_INFO_V4,
            reinterpret_cast<rusage_info_t*>(&raw));
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return {std::nullopt, errno};
    }

    ResourceSample sample;
    std::copy(
        std::begin(raw.ri_uuid), std::end(raw.ri_uuid),
        sample.identity.uuid.begin());
    sample.identity.start_abstime =
        raw.ri_proc_start_abstime;
    sample.exit_abstime = raw.ri_proc_exit_abstime;
    sample.physical_footprint = raw.ri_phys_footprint;
    sample.lifetime_max_physical_footprint =
        raw.ri_lifetime_max_phys_footprint;
    sample.resident_size = raw.ri_resident_size;
    return {sample, 0};
#else
    return {std::nullopt, ENOTSUP};
#endif
}

ProcessGroupClassification classify_process_group_members(
    const std::vector<ProcessGroupMemberRead>& members) {
    ProcessGroupClassification result;
    for (const ProcessGroupMemberRead& member : members) {
        if (member.pid <= 0) {
            result.error_number = EINVAL;
            return result;
        }
        if (!member.reading.sample.has_value()) {
            if (member.reading.error_number == ESRCH) {
                continue;
            }
            result.error_number =
                member.reading.error_number != 0
                    ? member.reading.error_number
                    : EIO;
            return result;
        }

        const ResourceSample& sample =
            *member.reading.sample;
        if (sample.exit_abstime == 0) {
            result.live_pid = member.pid;
            return result;
        }
        const bool nonzero_uuid = std::any_of(
            sample.identity.uuid.begin(),
            sample.identity.uuid.end(),
            [](std::uint8_t byte) {
                return byte != 0;
            });
        if (sample.identity.start_abstime == 0 ||
            !nonzero_uuid) {
            result.error_number = EPROTO;
            return result;
        }
        result.terminal_members.push_back(
            {member.pid, sample});
    }
    result.execution_quiescent = true;
    return result;
}

int exit_code(Failure failure) {
    switch (failure) {
    case Failure::none:
        return 0;
    case Failure::physical_limit:
        return kPhysicalLimitExit;
    case Failure::resident_limit:
        return kResidentLimitExit;
    case Failure::publication_delta:
        return kPublicationDeltaExit;
    case Failure::marker:
        return kMarkerFailureExit;
    case Failure::sampler:
    case Failure::identity:
    case Failure::sample_order:
    case Failure::lifetime_peak_regression:
    case Failure::process_control:
    case Failure::target_integrity:
    case Failure::target_exit:
    case Failure::interrupted:
        return kMonitorFailureExit;
    }
    return kMonitorFailureExit;
}

std::string_view failure_name(Failure failure) {
    switch (failure) {
    case Failure::none:
        return "none";
    case Failure::sampler:
        return "sampler";
    case Failure::identity:
        return "identity";
    case Failure::sample_order:
        return "sample_order";
    case Failure::lifetime_peak_regression:
        return "lifetime_peak_regression";
    case Failure::physical_limit:
        return "physical_limit";
    case Failure::resident_limit:
        return "resident_limit";
    case Failure::publication_delta:
        return "publication_delta";
    case Failure::marker:
        return "marker";
    case Failure::process_control:
        return "process_control";
    case Failure::target_integrity:
        return "target_integrity";
    case Failure::target_exit:
        return "target_exit";
    case Failure::interrupted:
        return "interrupted";
    }
    return "unknown";
}

MarkerScanner::MarkerScanner(std::string marker)
    : marker_(std::move(marker)),
      prefix_(marker_.size(), 0) {
    if (marker_.empty()) {
        throw std::invalid_argument(
            "marker scanner requires a nonempty marker");
    }
    for (std::size_t index = 1, matched = 0;
         index < marker_.size(); ++index) {
        while (matched != 0 &&
               marker_[matched] != marker_[index]) {
            matched = prefix_[matched - 1];
        }
        if (marker_[matched] == marker_[index]) {
            ++matched;
        }
        prefix_[index] = matched;
    }
}

std::vector<std::uint64_t> MarkerScanner::feed(
    std::string_view bytes) {
    std::vector<std::uint64_t> offsets;
    for (std::size_t index = 0;
         index < bytes.size(); ++index) {
        const char byte = bytes[index];
        while (matched_ != 0 &&
               marker_[matched_] != byte) {
            matched_ = prefix_[matched_ - 1];
        }
        if (marker_[matched_] == byte) {
            ++matched_;
        }
        if (matched_ == marker_.size()) {
            ++count_;
            offsets.push_back(
                bytes_seen_ +
                static_cast<std::uint64_t>(index) + 1 -
                static_cast<std::uint64_t>(
                    marker_.size()));
            matched_ = prefix_.back();
        }
    }
    bytes_seen_ += static_cast<std::uint64_t>(
        bytes.size());
    return offsets;
}

std::size_t MarkerScanner::count() const {
    return count_;
}

std::string_view marker_stream_name(
    MarkerStream stream) {
    switch (stream) {
    case MarkerStream::standard_output:
        return "stdout";
    case MarkerStream::standard_error:
        return "stderr";
    }
    return "unknown";
}

GateState::GateState(Config config)
    : config_(config) {
    if (config_.physical_limit_bytes == 0 ||
        config_.resident_limit_bytes == 0 ||
        config_.publication_delta_limit_bytes == 0 ||
        config_.sample_period.count() <= 0) {
        throw std::invalid_argument(
            "rusage guard limits and sample period must be positive");
    }
}

void GateState::fail(
    Failure failure, std::string reason) {
    if (failure_ != Failure::none) {
        const bool new_is_infrastructure =
            exit_code(failure) == kMonitorFailureExit;
        const bool old_is_infrastructure =
            exit_code(failure_) == kMonitorFailureExit;
        if (new_is_infrastructure &&
            !old_is_infrastructure) {
            reason =
                std::move(reason) +
                " (after " +
                std::string(failure_name(failure_)) +
                ": " + reason_ + ")";
            failure_ = failure;
            reason_ = std::move(reason);
        }
        return;
    }
    failure_ = failure;
    reason_ = std::move(reason);
}

bool GateState::accept_epoch(std::uint64_t epoch) {
    if (epoch <= last_observation_epoch_) {
        fail(
            Failure::process_control,
            "supervisor observation epoch is not monotonic");
        return false;
    }
    last_observation_epoch_ = epoch;
    return true;
}

void GateState::begin_sample(std::uint64_t epoch) {
    if (!accept_epoch(epoch)) {
        return;
    }
    if (active_sample_begin_epoch_.has_value()) {
        fail(
            Failure::process_control,
            "a resource sample began while another was active");
        return;
    }
    active_sample_begin_epoch_ = epoch;
}

void GateState::complete_sample_failure(
    std::uint64_t epoch) {
    if (!accept_epoch(epoch)) {
        return;
    }
    if (!active_sample_begin_epoch_.has_value()) {
        fail(
            Failure::process_control,
            "a failed resource sample completed without beginning");
        return;
    }
    active_sample_begin_epoch_.reset();
    fail(
        Failure::sampler,
        "proc_pid_rusage RUSAGE_INFO_V4 failed");
}

void GateState::enforce_limits(
    const ResourceSample& sample) {
    if (effective_peak_ >=
        config_.physical_limit_bytes) {
        fail(
            Failure::physical_limit,
            "physical footprint reached its fixed cap");
        return;
    }
    if (sample.resident_size >=
        config_.resident_limit_bytes) {
        fail(
            Failure::resident_limit,
            "resident size reached its fixed cap");
    }
}

void GateState::enforce_publication_delta(
    const ResourceSample& sample) {
    if (!publication_baseline_.has_value() ||
        !publication_baseline_effective_.has_value()) {
        fail(
            Failure::marker,
            "post-marker sample has no known pre-marker baseline");
        return;
    }
    maximum_post_marker_current_ = std::max(
        maximum_post_marker_current_,
        sample.physical_footprint);
    maximum_post_marker_effective_ = std::max(
        maximum_post_marker_effective_,
        effective_peak_);
    publication_current_delta_ =
        positive_difference(
            maximum_post_marker_current_,
            publication_baseline_->physical_footprint);
    publication_effective_delta_ =
        positive_difference(
            maximum_post_marker_effective_,
            *publication_baseline_effective_);
    publication_delta_bytes_ =
        std::max(
            publication_current_delta_,
            publication_effective_delta_);
    if (publication_delta_bytes_ >
        config_.publication_delta_limit_bytes) {
        fail(
            Failure::publication_delta,
            "post-marker physical footprint growth exceeded its cap");
    }
}

void GateState::complete_sample(
    const ResourceSample& sample,
    std::uint64_t epoch) {
    ++sample_count_;
    last_inversion_gap_ = 0;
    last_lifetime_regression_gap_ = 0;
    if (!accept_epoch(epoch)) {
        return;
    }
    if (!active_sample_begin_epoch_.has_value()) {
        fail(
            Failure::process_control,
            "a resource sample completed without beginning");
        return;
    }
    const std::uint64_t begin_epoch =
        *active_sample_begin_epoch_;
    active_sample_begin_epoch_.reset();
    const bool final_after_waitid =
        waitid_epoch_.has_value() &&
        begin_epoch > *waitid_epoch_;
    if (sample.identity.start_abstime == 0 ||
        std::all_of(
            sample.identity.uuid.begin(),
            sample.identity.uuid.end(),
            [](std::uint8_t byte) {
                return byte == 0;
            })) {
        fail(
            Failure::identity,
            "sampled process identity is zero");
        return;
    }
    if (!identity_.has_value()) {
        identity_ = sample.identity;
    } else if (*identity_ != sample.identity) {
        fail(
            Failure::identity,
            "sampled process UUID or start time changed");
        return;
    }
    if (final_after_waitid &&
        sample.exit_abstime == 0) {
        fail(
            Failure::process_control,
            "post-waitid zombie sample lacks a process-exit time");
        return;
    }

    last_inversion_gap_ =
        positive_difference(
            sample.physical_footprint,
            sample.lifetime_max_physical_footprint);
    if (last_inversion_gap_ != 0) {
        ++inversion_count_;
        maximum_inversion_gap_ = std::max(
            maximum_inversion_gap_,
            last_inversion_gap_);
    }
    if (last_sample_.has_value()) {
        last_lifetime_regression_gap_ =
            positive_difference(
                last_sample_
                    ->lifetime_max_physical_footprint,
                sample
                    .lifetime_max_physical_footprint);
    }
    if (last_lifetime_regression_gap_ != 0) {
        ++lifetime_regression_count_;
        maximum_lifetime_regression_gap_ = std::max(
            maximum_lifetime_regression_gap_,
            last_lifetime_regression_gap_);
    }
    effective_peak_ = std::max({
        effective_peak_,
        sample.physical_footprint,
        sample.lifetime_max_physical_footprint,
    });
    last_sample_ = sample;
    if (final_after_waitid) {
        final_sample_epoch_ = epoch;
    }
    if (failure_ != Failure::none) {
        return;
    }
    enforce_limits(sample);
    if (failure_ != Failure::none) {
        return;
    }
    if (marker_epoch_.has_value() &&
        begin_epoch > *marker_epoch_) {
        ++post_marker_sample_count_;
        enforce_publication_delta(sample);
    }
}

void GateState::observe_marker(
    const MarkerObservation& observation) {
    ++marker_count_;
    marker_observations_.push_back(observation);
    if (!accept_epoch(observation.epoch)) {
        return;
    }
    if (marker_count_ != 1) {
        fail(
            Failure::marker,
            "publication marker occurred more than once");
        return;
    }
    if (!last_sample_.has_value()) {
        fail(
            Failure::marker,
            "publication marker has no known pre-marker sample");
        return;
    }
    marker_epoch_ = observation.epoch;
    publication_baseline_ = last_sample_;
    publication_baseline_effective_ =
        effective_peak_;
    maximum_post_marker_current_ =
        last_sample_->physical_footprint;
    maximum_post_marker_effective_ =
        effective_peak_;
    publication_current_delta_ = 0;
    publication_effective_delta_ = 0;
    publication_delta_bytes_ = 0;
}

void GateState::observe_waitid_exit(std::uint64_t epoch) {
    if (!accept_epoch(epoch)) {
        return;
    }
    if (waitid_epoch_.has_value()) {
        fail(
            Failure::process_control,
            "waitid exit observation occurred more than once");
        return;
    }
    waitid_epoch_ = epoch;
}

void GateState::observe_reaped(std::uint64_t epoch) {
    if (!accept_epoch(epoch)) {
        return;
    }
    if (!final_sample_epoch_.has_value() ||
        epoch <= *final_sample_epoch_) {
        fail(
            Failure::process_control,
            "target was reaped before its final zombie sample");
        return;
    }
    reap_epoch_ = epoch;
}

void GateState::observe_group_quiescent(
    std::uint64_t epoch) {
    if (!accept_epoch(epoch)) {
        return;
    }
    if (!reap_epoch_.has_value() ||
        epoch <= *reap_epoch_) {
        fail(
            Failure::process_control,
            "process group was checked before target reap");
        return;
    }
    group_quiescent_epoch_ = epoch;
}

void GateState::observe_failure(Failure failure) {
    if (failure == Failure::none) {
        return;
    }
    fail(
        failure,
        std::string(failure_name(failure)) +
            " supervisor failure");
}

void GateState::finish() {
    if (!waitid_epoch_.has_value() ||
        !final_sample_epoch_.has_value() ||
        !reap_epoch_.has_value() ||
        !group_quiescent_epoch_.has_value()) {
        fail(
            Failure::process_control,
            "supervisor lifecycle did not complete "
            "waitid/final-sample/reap/group-quiescence");
    }
    if (marker_count_ != 1) {
        fail(
            Failure::marker,
            marker_count_ == 0
                ? "publication marker is missing"
                : "publication marker is not unique");
        return;
    }
    if (post_marker_sample_count_ == 0) {
        fail(
            Failure::marker,
            "no complete sample began after marker observation");
    }
}

Failure GateState::failure() const {
    return failure_;
}

const std::string& GateState::reason() const {
    return reason_;
}

const std::optional<ResourceSample>&
GateState::last_sample() const {
    return last_sample_;
}

std::size_t GateState::sample_count() const {
    return sample_count_;
}

std::size_t GateState::marker_count() const {
    return marker_count_;
}

std::size_t GateState::post_marker_sample_count() const {
    return post_marker_sample_count_;
}

std::uint64_t GateState::publication_delta_bytes() const {
    return publication_delta_bytes_;
}

std::uint64_t GateState::effective_peak() const {
    return effective_peak_;
}

std::size_t GateState::inversion_count() const {
    return inversion_count_;
}

std::uint64_t GateState::maximum_inversion_gap() const {
    return maximum_inversion_gap_;
}

std::size_t GateState::lifetime_regression_count() const {
    return lifetime_regression_count_;
}

std::uint64_t
GateState::maximum_lifetime_regression_gap() const {
    return maximum_lifetime_regression_gap_;
}

std::uint64_t GateState::last_inversion_gap() const {
    return last_inversion_gap_;
}

std::uint64_t
GateState::last_lifetime_regression_gap() const {
    return last_lifetime_regression_gap_;
}

bool GateState::marker_seen() const {
    return marker_count_ != 0;
}

bool GateState::final_sample_after_waitid() const {
    return final_sample_epoch_.has_value();
}

bool GateState::reaped() const {
    return reap_epoch_.has_value();
}

bool GateState::group_quiescent() const {
    return group_quiescent_epoch_.has_value();
}

const std::vector<MarkerObservation>&
GateState::marker_observations() const {
    return marker_observations_;
}

const std::optional<ResourceSample>&
GateState::publication_baseline() const {
    return publication_baseline_;
}

const std::optional<std::uint64_t>&
GateState::publication_baseline_effective() const {
    return publication_baseline_effective_;
}

std::uint64_t
GateState::maximum_post_marker_current() const {
    return maximum_post_marker_current_;
}

std::uint64_t
GateState::maximum_post_marker_effective() const {
    return maximum_post_marker_effective_;
}

std::uint64_t
GateState::publication_current_delta() const {
    return publication_current_delta_;
}

std::uint64_t
GateState::publication_effective_delta() const {
    return publication_effective_delta_;
}

static Report supervise_with_reader(
    const Request& request, const Config& config,
    const SampleReader& sample_reader,
    const std::function<void()>&
        before_group_quiescence) {
    Report report;
    std::optional<FileDescriptor> events;
    std::optional<FileDescriptor> samples;
    std::optional<FileDescriptor> summary;

    try {
        GateState gate(config);
        if (request.command.empty()) {
            throw std::invalid_argument(
                "supervisor command must not be empty");
        }
        const std::filesystem::path target =
            request.command.front();
        if (!target.is_absolute()) {
            throw std::invalid_argument(
                "supervisor target must be an absolute path");
        }

        const integrity::RegularFileSnapshot before =
            integrity::snapshot_regular_file(target);
        report.target_sha256_before = before.sha256;
        create_log_directory(request.log_directory);
        const FileDescriptor target_output =
            open_exclusive_log(
                request.log_directory / "target.stdout");
        const FileDescriptor target_error =
            open_exclusive_log(
                request.log_directory / "target.stderr");
        events.emplace(open_exclusive_log(
            request.log_directory / "events.tsv"));
        samples.emplace(open_exclusive_log(
            request.log_directory / "samples.tsv"));
        summary.emplace(open_exclusive_log(
            request.log_directory / "summary.tsv"));
        write_all(
            events->get(),
            "monotonic_ns\tevent\tdetail\n");
        write_all(
            samples->get(),
            "sequence\tmonotonic_ns\tbegin_epoch"
            "\tcomplete_epoch\tfinal_after_waitid"
            "\tbegan_after_marker\tuuid\tstart_abstime"
            "\texit_abstime\tphys_footprint"
            "\tlifetime_max_phys_footprint\tresident_size"
            "\teffective_peak\tcurrent_over_lifetime_gap"
            "\tlifetime_regression_gap\n");
        append_event(
            events->get(), "preflight",
            "supervisor_pid=" +
                std::to_string(::getpid()) +
                " target_physical_path_hex=" +
                bytes_hex(before.physical_path) +
                " target_sha256=" + before.sha256);
        append_event(
            events->get(), "configuration",
            "physical_limit_bytes=" +
                std::to_string(
                    config.physical_limit_bytes) +
                " resident_limit_bytes=" +
                std::to_string(
                    config.resident_limit_bytes) +
                " publication_delta_limit_bytes=" +
                std::to_string(
                    config.publication_delta_limit_bytes) +
                " physical_peak_semantics=scalar_envelope" +
                " sample_period_milliseconds=" +
                std::to_string(
                    config.sample_period.count()));
        for (std::size_t index = 0;
             index < request.command.size(); ++index) {
            append_event(
                events->get(), "argument",
                "index=" + std::to_string(index) +
                    " length=" +
                    std::to_string(
                        request.command[index].size()) +
                    " hex=" +
                    bytes_hex(request.command[index]));
        }

        const SampleRead preflight =
            read_process_sample(::getpid());
        if (!preflight.sample.has_value()) {
            throw std::runtime_error(make_reason(
                "proc_pid_rusage preflight failed",
                preflight.error_number));
        }

        SignalScope signals;
        std::vector<char*> child_arguments;
        child_arguments.reserve(
            request.command.size() + 1);
        for (const std::string& argument : request.command) {
            child_arguments.push_back(
                const_cast<char*>(argument.c_str()));
        }
        child_arguments.push_back(nullptr);

        auto exec_pipe = make_exec_pipe();
        const pid_t child = ::fork();
        if (child < 0) {
            throw_system_call("cannot fork supervised target");
        }
        if (child == 0) {
            exec_pipe[0].reset();
            if (::setpgid(0, 0) != 0) {
                const int error_number = errno;
                child_report_failure(
                    exec_pipe[1].get(),
                    ChildLaunchStage::process_group,
                    error_number);
                ::_exit(127);
            }
            if (::dup2(
                    target_output.get(),
                    STDOUT_FILENO) < 0) {
                const int error_number = errno;
                child_report_failure(
                    exec_pipe[1].get(),
                    ChildLaunchStage::standard_output,
                    error_number);
                ::_exit(127);
            }
            if (::dup2(
                    target_error.get(),
                    STDERR_FILENO) < 0) {
                const int error_number = errno;
                child_report_failure(
                    exec_pipe[1].get(),
                    ChildLaunchStage::standard_error,
                    error_number);
                ::_exit(127);
            }
            ::execv(
                child_arguments.front(), child_arguments.data());
            const int error_number = errno;
            child_report_failure(
                exec_pipe[1].get(),
                ChildLaunchStage::execute, error_number);
            ::_exit(127);
        }

        report.child_pid = child;
        ChildOwner child_owner(child);
        exec_pipe[1].reset();
        const std::optional<ChildLaunchFailure>
            launch_failure =
                read_exec_failure(exec_pipe[0].get());
        exec_pipe[0].reset();
        if (launch_failure.has_value()) {
            std::ostringstream reason;
            reason << child_launch_stage_name(
                          launch_failure->stage)
                   << " failed: "
                   << std::generic_category().message(
                          launch_failure->error_number);
            gate.observe_failure(Failure::process_control);
            append_event(
                events->get(), "exec_failure", reason.str());
        } else {
            const pid_t process_group = ::getpgid(child);
            if (process_group != child) {
                gate.observe_failure(
                    Failure::process_control);
                append_event(
                    events->get(), "process_group_failure",
                    process_group < 0
                        ? make_reason(
                              "getpgid failed", errno)
                        : "child is not its process-group leader");
            } else {
                report.process_group_confirmed = true;
                report.exec_succeeded = true;
                append_event(
                    events->get(), "exec_succeeded",
                    "pid=" + std::to_string(child));
            }
        }

        MarkerScanner output_marker{
            std::string(kPublicationMarker)};
        MarkerScanner error_marker{
            std::string(kPublicationMarker)};
        std::uint64_t output_offset = 0;
        std::uint64_t error_offset = 0;
        std::size_t sample_sequence = 0;
        std::uint64_t observation_epoch = 0;
        bool kill_requested = false;
        bool exit_observed = false;

        const auto next_epoch = [&]() {
            return ++observation_epoch;
        };

        const auto scan_stream =
            [&](int descriptor, std::uint64_t& offset,
                MarkerScanner& scanner,
                MarkerStream stream) {
                read_appended(
                    descriptor, offset,
                    [&](std::string_view bytes) {
                        const std::vector<std::uint64_t>
                            new_markers = scanner.feed(bytes);
                        for (const std::uint64_t marker_offset :
                             new_markers) {
                            gate.observe_marker({
                                next_epoch(),
                                stream,
                                marker_offset,
                            });
                            append_event(
                                events->get(),
                                "publication_marker",
                                "stream=" +
                                    std::string(
                                        marker_stream_name(
                                            stream)) +
                                    " offset=" +
                                    std::to_string(
                                        marker_offset) +
                                    " count=" +
                                    std::to_string(
                                        gate.marker_count()));
                            sync_file(events->get());
                        }
                    });
            };

        const auto scan_output = [&]() {
            scan_stream(
                target_output.get(), output_offset,
                output_marker,
                MarkerStream::standard_output);
            scan_stream(
                target_error.get(), error_offset,
                error_marker,
                MarkerStream::standard_error);
        };

        const auto sample_once =
            [&](bool final_after_waitid) {
                const std::uint64_t begin_epoch =
                    next_epoch();
                const bool marker_seen_before_begin =
                    gate.marker_seen();
                gate.begin_sample(begin_epoch);
                const SampleRead reading =
                    sample_reader(child);
                scan_output();
                const std::uint64_t complete_epoch =
                    next_epoch();
                if (!reading.sample.has_value()) {
                    gate.complete_sample_failure(
                        complete_epoch);
                    append_event(
                        events->get(), "sample_failure",
                        make_reason(
                            "proc_pid_rusage failed",
                            reading.error_number));
                    return;
                }
                ++sample_sequence;
                report
                    .maximum_current_physical_footprint =
                    std::max(
                        report
                            .maximum_current_physical_footprint,
                        reading.sample
                            ->physical_footprint);
                report
                    .maximum_lifetime_physical_footprint =
                    std::max(
                        report
                            .maximum_lifetime_physical_footprint,
                        reading.sample
                            ->lifetime_max_physical_footprint);
                report.maximum_resident_size =
                    std::max(
                        report.maximum_resident_size,
                        reading.sample->resident_size);
                const std::size_t inversion_count_before =
                    gate.inversion_count();
                const std::size_t
                    lifetime_regression_count_before =
                        gate.lifetime_regression_count();
                const std::optional<std::uint64_t>
                    previous_raw_lifetime =
                        gate.last_sample().has_value()
                            ? std::optional<std::uint64_t>(
                                  gate.last_sample()
                                      ->lifetime_max_physical_footprint)
                            : std::nullopt;
                gate.complete_sample(
                    *reading.sample, complete_epoch);
                append_sample(
                    samples->get(), sample_sequence,
                    *reading.sample, final_after_waitid,
                    marker_seen_before_begin,
                    begin_epoch, complete_epoch,
                    gate.effective_peak(),
                    gate.last_inversion_gap(),
                    gate.last_lifetime_regression_gap());
                bool diagnostic_event = false;
                if (gate.inversion_count() >
                    inversion_count_before) {
                    diagnostic_event = true;
                    append_event(
                        events->get(),
                        "physical_footprint_inversion",
                        "sequence=" +
                            std::to_string(sample_sequence) +
                            " current=" +
                            std::to_string(
                                reading.sample
                                    ->physical_footprint) +
                            " raw_lifetime=" +
                            std::to_string(
                                reading.sample
                                    ->lifetime_max_physical_footprint) +
                            " effective_peak=" +
                            std::to_string(
                                gate.effective_peak()) +
                            " gap=" +
                            std::to_string(
                                gate.last_inversion_gap()));
                }
                if (gate.lifetime_regression_count() >
                    lifetime_regression_count_before) {
                    diagnostic_event = true;
                    append_event(
                        events->get(),
                        "raw_lifetime_regression",
                        "sequence=" +
                            std::to_string(sample_sequence) +
                            " previous_raw_lifetime=" +
                            std::to_string(
                                previous_raw_lifetime.value_or(
                                    0)) +
                            " raw_lifetime=" +
                            std::to_string(
                                reading.sample
                                    ->lifetime_max_physical_footprint) +
                            " effective_peak=" +
                            std::to_string(
                                gate.effective_peak()) +
                            " gap=" +
                            std::to_string(
                                gate
                                    .last_lifetime_regression_gap()));
                }
                sync_file(samples->get());
                if (diagnostic_event) {
                    sync_file(events->get());
                }
            };

        while (!exit_observed) {
            if (interrupted_signal != 0 &&
                gate.failure() == Failure::none) {
                gate.observe_failure(Failure::interrupted);
                append_event(
                    events->get(), "interrupted",
                    "signal=" +
                        std::to_string(interrupted_signal));
            }

            if (gate.failure() == Failure::none) {
                scan_output();
            }
            if (gate.failure() == Failure::none) {
                sample_once(false);
            }

            if (gate.failure() != Failure::none &&
                !kill_requested) {
                kill_requested = true;
                sync_file(target_output.get());
                sync_file(target_error.get());
                sync_file(events->get());
                sync_file(samples->get());
                append_event(
                    events->get(), "kill_process_group",
                    std::string(failure_name(
                        gate.failure())));
                int pre_kill_wait_error = 0;
                exit_observed = waitid_observed_exit(
                    child, false, pre_kill_wait_error);
                if (exit_observed) {
                    append_event(
                        events->get(),
                        "kill_skipped_waitable",
                        "leader already exited");
                } else if (pre_kill_wait_error != 0 &&
                           !(pre_kill_wait_error == EINTR &&
                             interrupted_signal != 0)) {
                    gate.observe_failure(
                        Failure::process_control);
                    append_event(
                        events->get(),
                        "pre_kill_waitid_failure",
                        make_reason(
                            "waitid failed",
                            pre_kill_wait_error));
                } else {
                    const KillOutcome outcome =
                        kill_owned_process_group(child);
                    append_event(
                        events->get(),
                        "kill_process_group_result",
                        "success=" +
                            std::to_string(outcome.success) +
                            " group_errno=" +
                            std::to_string(
                                outcome.group_error) +
                            " leader_errno=" +
                            std::to_string(
                                outcome.leader_error));
                    if (!outcome.success) {
                        int post_kill_wait_error = 0;
                        exit_observed =
                            waitid_observed_exit(
                                child, false,
                                post_kill_wait_error);
                        if (exit_observed) {
                            append_event(
                                events->get(),
                                "kill_raced_with_exit",
                                "leader became waitable");
                        } else {
                            gate.observe_failure(
                                Failure::process_control);
                            if (post_kill_wait_error != 0) {
                                append_event(
                                    events->get(),
                                    "post_kill_waitid_failure",
                                    make_reason(
                                        "waitid failed",
                                        post_kill_wait_error));
                            }
                        }
                    }
                }
            }

            int wait_error = 0;
            if (!exit_observed) {
                exit_observed = waitid_observed_exit(
                    child, kill_requested, wait_error);
            }
            if (!exit_observed && wait_error != 0 &&
                !(wait_error == EINTR &&
                  interrupted_signal != 0)) {
                gate.observe_failure(
                    Failure::process_control);
                append_event(
                    events->get(), "waitid_failure",
                    make_reason("waitid failed", wait_error));
                if (!kill_requested) {
                    kill_requested = true;
                    const KillOutcome outcome =
                        kill_owned_process_group(child);
                    append_event(
                        events->get(),
                        "waitid_failure_kill_result",
                        "success=" +
                            std::to_string(outcome.success) +
                            " group_errno=" +
                            std::to_string(
                                outcome.group_error) +
                            " leader_errno=" +
                            std::to_string(
                                outcome.leader_error));
                }
            }
            if (!exit_observed) {
                sleep_for(
                    kill_requested
                        ? std::chrono::milliseconds(10)
                        : config.sample_period);
            }
        }

        append_event(
            events->get(), "exit_observed_unreaped",
            "pid=" + std::to_string(child));
        gate.observe_waitid_exit(next_epoch());
        if (gate.failure() == Failure::none) {
            scan_output();
        }
        sample_once(true);

        int wait_status = 0;
        struct rusage usage {};
        pid_t waited = -1;
        do {
            waited = ::wait4(
                child, &wait_status, 0, &usage);
        } while (waited < 0 && errno == EINTR);
        if (waited != child) {
            gate.observe_failure(Failure::process_control);
            append_event(
                events->get(), "wait4_failure",
                make_reason("wait4 failed", errno));
        } else {
            child_owner.mark_reaped();
            gate.observe_reaped(next_epoch());
            report.reaped = true;
            report.wait4_max_resident_size =
                static_cast<std::uint64_t>(
                    std::max<decltype(usage.ru_maxrss)>(
                        usage.ru_maxrss, 0));
            report.maximum_resident_size =
                std::max(
                    report.maximum_resident_size,
                    report.wait4_max_resident_size);
            if (report.wait4_max_resident_size >=
                    config.resident_limit_bytes &&
                gate.failure() == Failure::none) {
                gate.observe_failure(
                    Failure::resident_limit);
            }

            if (WIFEXITED(wait_status)) {
                report.target_exit_code =
                    WEXITSTATUS(wait_status);
                if (*report.target_exit_code != 0 &&
                    *report.target_exit_code != 1) {
                    gate.observe_failure(
                        Failure::target_exit);
                }
            } else if (WIFSIGNALED(wait_status)) {
                report.target_signal =
                    WTERMSIG(wait_status);
                if (!kill_requested) {
                    gate.observe_failure(
                        Failure::target_exit);
                }
            } else {
                gate.observe_failure(Failure::target_exit);
            }
        }

        scan_output();
        if (report.reaped) {
            if (before_group_quiescence) {
                before_group_quiescence();
            }
            const ProcessGroupQuiescence quiescence =
                make_process_group_quiescent(child);
            for (const TerminalProcessGroupMember& member :
                 quiescence.terminal_members) {
                append_event(
                    events->get(),
                    "terminal_process_group_member",
                    "pid=" + std::to_string(member.pid) +
                        " uuid=" +
                        uuid_hex(
                            member.sample.identity.uuid) +
                        " start_abstime=" +
                        std::to_string(
                            member.sample.identity
                                .start_abstime) +
                        " exit_abstime=" +
                        std::to_string(
                            member.sample.exit_abstime));
            }
            if (quiescence.execution_quiescent) {
                gate.observe_group_quiescent(next_epoch());
                report.process_group_quiescent = true;
            } else {
                gate.observe_failure(
                    Failure::process_control);
                append_event(
                    events->get(),
                    "process_group_not_quiescent",
                    "pgid=" + std::to_string(child) +
                        " detail=" +
                        quiescence.failure_detail);
            }
        }
        scan_output();
        gate.finish();

        try {
            const integrity::RegularFileSnapshot after =
                integrity::snapshot_regular_file(target);
            report.target_sha256_after = after.sha256;
            append_event(
                events->get(), "postflight",
                "target_sha256=" + after.sha256);
            report.target_unchanged = before == after;
            if (!report.target_unchanged) {
                gate.observe_failure(
                    Failure::target_integrity);
                append_event(
                    events->get(),
                    "target_integrity_failure",
                    "target snapshot changed");
            }
        } catch (const std::exception& error) {
            gate.observe_failure(Failure::target_integrity);
            append_event(
                events->get(), "target_integrity_failure",
                error.what());
        }

        report.failure = gate.failure();
        report.reason = gate.reason();
        report.sample_count = gate.sample_count();
        report.marker_count = gate.marker_count();
        report.post_marker_sample_count =
            gate.post_marker_sample_count();
        report.publication_delta_bytes =
            gate.publication_delta_bytes();
        report.effective_peak =
            gate.effective_peak();
        report.inversion_count =
            gate.inversion_count();
        report.maximum_inversion_gap =
            gate.maximum_inversion_gap();
        report.lifetime_regression_count =
            gate.lifetime_regression_count();
        report.maximum_lifetime_regression_gap =
            gate.maximum_lifetime_regression_gap();
        report.final_sample_after_waitid =
            gate.final_sample_after_waitid();
        report.reaped = gate.reaped();
        report.process_group_quiescent =
            gate.group_quiescent();
        report.marker_observations =
            gate.marker_observations();
        if (gate.publication_baseline().has_value()) {
            report.publication_baseline_current =
                gate.publication_baseline()
                    ->physical_footprint;
            report.publication_baseline_effective =
                gate.publication_baseline_effective();
        }
        report.maximum_post_marker_current =
            gate.maximum_post_marker_current();
        report.maximum_post_marker_effective =
            gate.maximum_post_marker_effective();
        report.publication_current_delta =
            gate.publication_current_delta();
        report.publication_effective_delta =
            gate.publication_effective_delta();
        if (gate.failure() == Failure::none &&
            report.target_exit_code.has_value()) {
            report.status = *report.target_exit_code;
            report.reason =
                "all supervisor gates passed";
        } else {
            report.status = exit_code(gate.failure());
        }

        std::ostringstream summary_text;
        summary_text
            << "status\t" << report.status << '\n'
            << "failure\t"
            << failure_name(report.failure) << '\n'
            << "reason\t"
            << sanitize_tsv(report.reason) << '\n'
            << "supervisor_pid\t" << ::getpid() << '\n'
            << "physical_limit_bytes\t"
            << config.physical_limit_bytes << '\n'
            << "resident_limit_bytes\t"
            << config.resident_limit_bytes << '\n'
            << "publication_delta_limit_bytes\t"
            << config.publication_delta_limit_bytes << '\n'
            << "physical_peak_semantics\tscalar_envelope\n"
            << "sample_period_milliseconds\t"
            << config.sample_period.count() << '\n'
            << "target_path_length\t"
            << before.path.size() << '\n'
            << "target_path_hex\t"
            << bytes_hex(before.path) << '\n'
            << "target_physical_path_length\t"
            << before.physical_path.size() << '\n'
            << "target_physical_path_hex\t"
            << bytes_hex(before.physical_path) << '\n'
            << "argv_count\t"
            << request.command.size() << '\n'
            << "child_pid\t" << report.child_pid << '\n'
            << "exec_succeeded\t"
            << (report.exec_succeeded ? 1 : 0) << '\n'
            << "process_group_confirmed\t"
            << (report.process_group_confirmed ? 1 : 0)
            << '\n'
            << "final_sample_after_waitid\t"
            << (report.final_sample_after_waitid ? 1 : 0)
            << '\n'
            << "target_unchanged\t"
            << (report.target_unchanged ? 1 : 0) << '\n'
            << "reaped\t"
            << (report.reaped ? 1 : 0) << '\n'
            << "process_group_quiescent\t"
            << (report.process_group_quiescent ? 1 : 0)
            << '\n'
            << "target_sha256_before\t"
            << report.target_sha256_before << '\n'
            << "target_sha256_after\t"
            << report.target_sha256_after << '\n'
            << "samples\t" << report.sample_count << '\n'
            << "markers\t" << report.marker_count << '\n'
            << "post_marker_samples\t"
            << report.post_marker_sample_count << '\n'
            << "publication_delta_bytes\t"
            << report.publication_delta_bytes << '\n'
            << "publication_current_delta\t"
            << report.publication_current_delta << '\n'
            << "publication_effective_delta\t"
            << report.publication_effective_delta << '\n'
            << "effective_peak\t"
            << report.effective_peak << '\n'
            << "inversion_count\t"
            << report.inversion_count << '\n'
            << "maximum_inversion_gap\t"
            << report.maximum_inversion_gap << '\n'
            << "lifetime_regression_count\t"
            << report.lifetime_regression_count << '\n'
            << "maximum_lifetime_regression_gap\t"
            << report.maximum_lifetime_regression_gap
            << '\n'
            << "maximum_current_physical_footprint\t"
            << report.maximum_current_physical_footprint
            << '\n'
            << "maximum_lifetime_physical_footprint\t"
            << report.maximum_lifetime_physical_footprint
            << '\n'
            << "maximum_resident_size\t"
            << report.maximum_resident_size << '\n'
            << "wait4_max_resident_size\t"
            << report.wait4_max_resident_size << '\n';
        for (std::size_t index = 0;
             index < request.command.size(); ++index) {
            summary_text
                << "argv_" << index << "_length\t"
                << request.command[index].size() << '\n'
                << "argv_" << index << "_hex\t"
                << bytes_hex(request.command[index])
                << '\n';
        }
        if (report.publication_baseline_current.has_value()) {
            summary_text
                << "publication_baseline_current\t"
                << *report.publication_baseline_current
                << '\n'
                << "publication_baseline_effective\t"
                << *report.publication_baseline_effective
                << '\n'
                << "maximum_post_marker_current\t"
                << report.maximum_post_marker_current
                << '\n'
                << "maximum_post_marker_effective\t"
                << report.maximum_post_marker_effective
                << '\n';
        }
        for (std::size_t index = 0;
             index < report.marker_observations.size();
             ++index) {
            const MarkerObservation& observation =
                report.marker_observations[index];
            summary_text
                << "marker_" << index << "_stream\t"
                << marker_stream_name(observation.stream)
                << '\n'
                << "marker_" << index << "_offset\t"
                << observation.byte_offset << '\n'
                << "marker_" << index << "_epoch\t"
                << observation.epoch << '\n';
        }
        if (report.target_exit_code.has_value()) {
            summary_text << "target_exit\t"
                         << *report.target_exit_code
                         << '\n';
        }
        if (report.target_signal.has_value()) {
            summary_text << "target_signal\t"
                         << *report.target_signal << '\n';
        }
        write_all(summary->get(), summary_text.str());
        append_event(
            events->get(), "supervisor_finished",
            "status=" + std::to_string(report.status));
        sync_file(target_output.get());
        sync_file(target_error.get());
        sync_file(events->get());
        sync_file(samples->get());
        sync_file(summary->get());
        return report;
    } catch (const std::exception& error) {
        report.status = kMonitorFailureExit;
        report.failure = Failure::process_control;
        report.reason = error.what();
        if (events.has_value()) {
            try {
                append_event(
                    events->get(),
                    "supervisor_exception", error.what());
                sync_file(events->get());
            } catch (const std::exception&) {
            }
        }
        return report;
    }
}

Report supervise(
    const Request& request, const Config& config) {
    return supervise_with_reader(
        request, config, read_process_sample, {});
}

Report supervise_for_test(
    const Request& request, const Config& config,
    SampleReader sample_reader) {
    return supervise_for_test(
        request, config, std::move(sample_reader), {});
}

Report supervise_for_test(
    const Request& request, const Config& config,
    SampleReader sample_reader,
    std::function<void()> before_group_quiescence) {
    if (!sample_reader) {
        Report report;
        report.reason =
            "test sample reader must not be empty";
        return report;
    }
    return supervise_with_reader(
        request, config, sample_reader,
        before_group_quiescence);
}

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error) {
    constexpr std::string_view usage =
        "Usage: old-school-fq0-quarantine-supervisor "
        "--log-dir PATH -- ABSOLUTE_COMMAND [ARG ...]";
    if (argc < 5 ||
        std::string_view(argv[1]) != "--log-dir" ||
        std::string_view(argv[3]) != "--") {
        error << usage << '\n';
        return kUsageExit;
    }

    Request request;
    request.log_directory = argv[2];
    for (int index = 4; index < argc; ++index) {
        request.command.emplace_back(argv[index]);
    }
    if (request.command.empty() ||
        !std::filesystem::path(
             request.command.front()).is_absolute()) {
        error << usage << '\n';
        return kUsageExit;
    }

    const Report report = supervise(request, Config{});
    output
        << "FQ0 rusage supervisor status: "
        << report.status << '\n'
        << "  failure: "
        << failure_name(report.failure) << '\n'
        << "  reason: " << report.reason << '\n'
        << "  samples: " << report.sample_count
        << " (post-marker "
        << report.post_marker_sample_count << ")\n"
        << "  markers: " << report.marker_count << '\n'
        << "  peak physical bytes: "
        << report.effective_peak
        << '\n'
        << "  physical inversions: "
        << report.inversion_count
        << " (maximum gap "
        << report.maximum_inversion_gap << ")\n"
        << "  raw lifetime regressions: "
        << report.lifetime_regression_count
        << " (maximum gap "
        << report.maximum_lifetime_regression_gap << ")\n"
        << "  peak resident bytes: "
        << report.maximum_resident_size << '\n'
        << "  publication delta bytes: "
        << report.publication_delta_bytes << '\n';
    if (report.status != 0 && report.status != 1) {
        error
            << "FQ0 rusage supervisor rejected the run: "
            << report.reason << '\n';
    }
    return report.status;
}

} // namespace old_school::fq0_rusage_guard
