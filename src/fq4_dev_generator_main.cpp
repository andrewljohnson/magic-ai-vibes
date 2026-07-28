#include "old_school/fq4_dev_generator.hpp"

#include <exception>
#include <iostream>
#include <string_view>

#ifdef OLD_SCHOOL_FQ4_PRODUCER_COMMIT
namespace {

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
    try {
        const auto report =
            old_school::fq4_dev_generator::
                generate_and_publish(
                    argv[0],
                    std::string_view(
                        OLD_SCHOOL_FQ4_PRODUCER_COMMIT));
        std::cout
            << "FQ4-DEV0 bundle published"
            << " path="
            << old_school::fq4_dev_bundle::kArtifactPath
            << " bytes=" << report.artifact_bytes
            << " sha256=" << report.artifact_sha256
            << " source_games_per_construction="
            << report.source_games_per_construction
            << " complete_constructions="
            << report.complete_constructions
            << " source_game_executions="
            << report.source_game_executions
            << " scored_rows=" << report.scored_rows
            << " fit_high_confidence="
            << report.fit.high_confidence_roots
            << " check_high_confidence="
            << report.check.high_confidence_roots
            << " repeat_exact="
            << (report.repeated_construction_bit_identical
                    ? 1
                    : 0)
            << " candidate_rollouts="
            << report.candidate_rollout_evaluations
            << '\n';
        return report.published ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr
            << "FQ4 development generation failed: "
            << error.what() << '\n';
        return 2;
    }
#endif
}
