#include "old_school/fq4_dev_generator.hpp"

#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <new>
#include <string_view>
#include <thread>
#include <type_traits>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef OLD_SCHOOL_FQ4_PRODUCER_COMMIT
namespace {

namespace bundle = old_school::fq4_dev_bundle;
namespace generator = old_school::fq4_dev_generator;

constexpr bool canonical_producer_commit(
    std::string_view value) {
    if (value.size() != 40 && value.size() != 64) {
        return false;
    }
    for (const char character : value) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

static_assert(
    canonical_producer_commit(
        std::string_view(
            OLD_SCHOOL_FQ4_PRODUCER_COMMIT)),
    "OLD_SCHOOL_FQ4_PRODUCER_COMMIT must be canonical "
    "lowercase 40- or 64-digit Git hexadecimal");
static_assert(
    std::is_trivially_copyable_v<
        generator::GenerationProgress>);

int worker(
    const char* executable_path,
    generator::GenerationProgress& progress) {
    try {
        const generator::GenerationReport report =
            generator::generate_and_publish(
                executable_path,
                std::string_view(
                    OLD_SCHOOL_FQ4_PRODUCER_COMMIT),
                &progress);
        if (!report.published) {
            std::cout
                << generator::
                       format_support_rejection_output(
                           report.fit, report.check,
                           report.scope);
            std::cout.flush();
            return std::cout.good() ? 1 : 2;
        }
        std::cout
            << generator::format_support_report(
                   report.fit, report.check)
            << generator::format_failure_scope_report(
                   report.scope)
            << "result=PUBLISHED"
            << " experiment=FQ4-DEV1"
            << " path=" << bundle::kArtifactPath
            << " bytes=" << report.artifact_bytes
            << " sha256=" << report.artifact_sha256
            << " source_games_per_construction="
            << report.source_games_per_construction
            << " complete_constructions="
            << report.complete_constructions
            << " source_game_executions="
            << report.source_game_executions
            << " scored_rows=" << report.scored_rows
            << " repeat_exact="
            << (report.repeated_construction_bit_identical
                    ? 1
                    : 0)
            << " candidate_rollouts="
            << report.candidate_rollout_evaluations
            << '\n';
        std::cout.flush();
        return std::cout.good() ? 0 : 2;
    } catch (const generator::GenerationFailure& failure) {
        std::cerr
            << generator::format_failure_scope_report(
                   failure.scope())
            << "result=NOT_PUBLISHED"
               " reason=construction_exception\n";
        std::cerr.flush();
        return 2;
    } catch (const std::exception&) {
        const generator::FailureScopeReport scope =
            generator::inspect_failure_scope(
                executable_path, progress);
        std::cerr
            << generator::format_failure_scope_report(scope)
            << "result=NOT_PUBLISHED"
               " reason=construction_exception\n";
        std::cerr.flush();
        return 2;
    }
}

int supervise(
    const char* executable_path) {
    int map_flags = MAP_SHARED;
#ifdef MAP_ANONYMOUS
    map_flags |= MAP_ANONYMOUS;
#else
    map_flags |= MAP_ANON;
#endif
    void* const memory =
        ::mmap(
            nullptr,
            sizeof(generator::GenerationProgress),
            PROT_READ | PROT_WRITE,
            map_flags, -1, 0);
    if (memory == MAP_FAILED) {
        std::cerr
            << "result=NOT_PUBLISHED"
               " reason=progress_map_failed\n";
        return 2;
    }
    auto* const shared_progress =
        ::new (memory) generator::GenerationProgress{};

    const pid_t child = ::fork();
    if (child < 0) {
        const generator::FailureScopeReport scope =
            generator::inspect_failure_scope(
                executable_path, *shared_progress);
        std::cerr
            << generator::format_failure_scope_report(scope)
            << "result=NOT_PUBLISHED"
               " reason=watchdog_fork_failed\n";
        static_cast<void>(
            ::munmap(
                memory,
                sizeof(generator::GenerationProgress)));
        return 2;
    }
    if (child == 0) {
        const int status =
            worker(
                executable_path,
                *shared_progress);
        ::_exit(status);
    }

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(
            generator::kWatchdogSeconds);
    while (true) {
        int status = 0;
        const pid_t waited =
            ::waitpid(child, &status, WNOHANG);
        if (waited == child) {
            const generator::GenerationProgress final_progress =
                *shared_progress;
            static_cast<void>(
                ::munmap(
                    memory,
                    sizeof(generator::GenerationProgress)));
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
            const generator::FailureScopeReport scope =
                generator::inspect_failure_scope(
                    executable_path,
                    final_progress);
            std::cerr
                << generator::format_failure_scope_report(scope)
                << "result=NOT_PUBLISHED"
                   " reason=worker_signal\n";
            return 2;
        }
        if (waited < 0) {
            static_cast<void>(
                ::kill(child, SIGKILL));
            static_cast<void>(
                ::waitpid(child, nullptr, 0));
            const generator::GenerationProgress final_progress =
                *shared_progress;
            static_cast<void>(
                ::munmap(
                    memory,
                    sizeof(generator::GenerationProgress)));
            const generator::FailureScopeReport scope =
                generator::inspect_failure_scope(
                    executable_path,
                    final_progress);
            std::cerr
                << generator::format_failure_scope_report(scope)
                << "result=NOT_PUBLISHED"
                   " reason=watchdog_wait_failed\n";
            return 2;
        }
        if (std::chrono::steady_clock::now() >=
            deadline) {
            static_cast<void>(
                ::kill(child, SIGKILL));
            static_cast<void>(
                ::waitpid(child, nullptr, 0));
            const generator::GenerationProgress final_progress =
                *shared_progress;
            static_cast<void>(
                ::munmap(
                    memory,
                    sizeof(generator::GenerationProgress)));
            const generator::FailureScopeReport scope =
                generator::inspect_failure_scope(
                    executable_path,
                    final_progress);
            std::cerr
                << generator::format_failure_scope_report(scope)
                << "result=NOT_PUBLISHED"
                   " reason=watchdog_timeout"
                   " watchdog_seconds="
                << generator::kWatchdogSeconds
                << '\n';
            return 2;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(20));
    }
}

} // namespace
#endif

int main(int argc, char** argv) {
    if (argc != 1) {
        std::cerr
            << "Usage: old-school-fq4-priority-dev-generate\n";
        return 2;
    }

#ifndef OLD_SCHOOL_FQ4_PRODUCER_COMMIT
    (void)argv;
    std::cerr
        << "FQ4 development generator was built without "
           "OLD_SCHOOL_FQ4_PRODUCER_COMMIT\n";
    return 2;
#else
    return supervise(argv[0]);
#endif
}
