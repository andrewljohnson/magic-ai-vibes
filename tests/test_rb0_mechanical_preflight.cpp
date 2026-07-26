#include "old_school/rb0_mechanical_preflight.hpp"

#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mechanical =
    old_school::rb0_mechanical_preflight;
namespace rb0 = old_school::replay_weight_audit;

namespace {

class TestRunner {
  public:
    void run(
        std::string_view name,
        const std::function<void()>& test) {
        try {
            test();
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

mechanical::Report failed_weight_fixture() {
    mechanical::Report report;
    report.seed = mechanical::kEngineeringSeed;
    report.generation = mechanical::kGeneration;
    report.balanced_blocks = mechanical::kBalancedBlocks;
    report.parent_fingerprint =
        std::string(rb0::kParentFingerprint);
    report.exact_engineering_seed = true;
    report.quarantined_seed_excluded = true;
    report.exact_generation = true;
    report.exact_block_count = true;
    report.artifact_snapshot_bound = true;
    report.parent_fingerprint_exact = true;
    report.parent_schema_exact = true;
    report.artifact_unchanged_after_load = true;
    report.artifact_unchanged_after_canonical = true;
    report.artifact_unchanged_after_repeat = true;
    report.artifact_unchanged_after_reverse = true;
    report.artifact_unchanged_after_single_worker = true;
    report.artifact_unchanged_final = true;
    report.repeated_capture_bit_identical = true;
    report.reversed_capture_bit_identical = true;
    report.worker_capture_bit_identical = true;
    for (mechanical::CaptureEvidence& capture :
         report.captures) {
        capture.physical_game_count_exact = true;
        capture.actor_game_count_exact = true;
        capture.rows_present = true;
        capture.rootless_actor_games_zero = true;
        capture.schedule_balanced = true;
        capture.hashes_well_formed = true;
        capture.trace_invariants_passed = true;
        capture.ro4_identity_passed = true;
        capture.terminal_tail_identity_passed = true;
        capture.hidden_repartition_passed = true;
        capture.hidden_changed_state_present = true;
        capture.hidden_grouping_identity_passed = true;
        capture.hidden_target_hash_identity_passed = true;
        capture.hidden_weight_identity_passed = true;
        capture.hidden_scoring_hash_identity_passed = true;
        capture.weights.finite_positive = true;
        capture.weights.global_mass_identity = true;
        capture.weights.actor_mass_identity = true;
        capture.weights.turn_mass_identity = true;
        capture.weight_identity_passed = true;
    }
    report.captures[0].weights.global_mass_identity = false;
    report.captures[0].weight_identity_passed = false;
    report.captures[0].weights.maximum_global_mass_error =
        1.25e-9;
    report.captures[0].global_mass_tolerance = 1.0e-9;
    return report;
}

void test_quarantined_seed_is_rejected() {
    bool rejected = false;
    try {
        mechanical::require_engineering_seed(
            rb0::kQuarantinedAuditSeed);
    } catch (const std::invalid_argument& error) {
        rejected =
            std::string_view(error.what()).find(
                "quarantined") != std::string_view::npos &&
            std::string_view(error.what()).find(
                std::to_string(rb0::kQuarantinedAuditSeed)) !=
                std::string_view::npos;
    }
    expect(
        rejected,
        "quarantined RB0-0 seed must fail with a named fence");
    mechanical::require_engineering_seed(
        mechanical::kEngineeringSeed);
}

void test_reserved_seed_is_rejected() {
    bool rejected = false;
    try {
        mechanical::require_engineering_seed(
            rb0::kAuditSeed);
    } catch (const std::invalid_argument& error) {
        rejected =
            std::string_view(error.what()).find(
                "reserved") != std::string_view::npos &&
            std::string_view(error.what()).find(
                std::to_string(rb0::kAuditSeed)) !=
                std::string_view::npos;
    }
    expect(
        rejected,
        "reserved RB0-0 seed must fail with a named fence");
}

void test_alternate_engineering_seed_is_rejected() {
    bool rejected = false;
    try {
        mechanical::require_engineering_seed(
            mechanical::kEngineeringSeed + 1);
    } catch (const std::invalid_argument& error) {
        rejected =
            std::string_view(error.what()).find(
                "exact engineering seed") !=
            std::string_view::npos;
    }
    expect(
        rejected,
        "alternate engineering seed must fail closed");
}

void test_failed_invariant_remains_named() {
    const mechanical::Report report =
        failed_weight_fixture();
    const auto invariants =
        mechanical::named_invariants(report);
    bool found = false;
    for (const mechanical::NamedInvariant& invariant :
         invariants) {
        if (invariant.name ==
                "canonical.weight.global-mass" &&
            !invariant.passed) {
            found = true;
        }
    }
    expect(found, "failed global-mass flag must retain its name");

    std::ostringstream output;
    mechanical::write_report(report, output);
    expect(
        output.str().find(
            "mechanical\tcanonical.weight.global-mass\tFAIL") !=
            std::string::npos,
        "failed invariant must survive report formatting");
    expect(
        output.str().find(
            "weight-mass\tcanonical.global\tmax_error=") !=
            std::string::npos &&
            output.str().find(";tolerance=") !=
                std::string::npos,
        "failed mass identity must retain error and tolerance");
    expect(
        !mechanical::mechanically_clean(report) &&
            mechanical::exit_code(false) == 2,
        "mechanical failure must remain a diagnostic exit two");
}

void test_wrong_schedule_hash_fails_named_balance() {
    rb0::Capture capture;
    capture.physical_games = mechanical::kPhysicalGames;
    capture.actor_games = mechanical::kActorGames;
    capture.schedule_hash = std::string(64, '0');
    const std::size_t expected_cell =
        mechanical::kBalancedBlocks *
        (old_school::kDeckCount - 1);
    for (auto& deck : capture.deck_seat_started_counts) {
        for (auto& seat : deck) {
            for (std::size_t& count : seat) {
                count = expected_cell;
            }
        }
    }
    for (std::size_t first = 0;
         first < old_school::kDeckCount; ++first) {
        for (std::size_t second = 0;
             second < old_school::kDeckCount; ++second) {
            capture.ordered_pair_counts[first][second] =
                first == second
                    ? 0
                    : 2 * mechanical::kBalancedBlocks;
        }
    }

    const mechanical::CaptureEvidence evidence =
        mechanical::inspect_capture(
            capture, mechanical::kEngineeringSeed,
            mechanical::kGeneration,
            mechanical::kBalancedBlocks);
    expect(
        !evidence.schedule_balanced,
        "wrong-but-well-formed schedule hash passed balance");
}

void test_report_contains_no_scientific_fields() {
    std::ostringstream output;
    mechanical::write_report(
        failed_weight_fixture(), output);
    for (const std::string_view forbidden : {
             "signed bias",
             "Brier",
             "log loss",
             "qualification",
             "confidence interval",
             "MDE",
             "scientific gate",
         }) {
        expect(
            output.str().find(forbidden) ==
                std::string::npos,
            std::string("mechanical report leaked field: ") +
                std::string(forbidden));
    }
}

void test_exit_code_has_no_scientific_rejection_state() {
    expect(
        mechanical::exit_code(true) == 0,
        "clean mechanical preflight exit");
    expect(
        mechanical::exit_code(false) == 2,
        "failed mechanical preflight exit");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "quarantined seed is rejected",
        test_quarantined_seed_is_rejected);
    runner.run(
        "reserved seed is rejected",
        test_reserved_seed_is_rejected);
    runner.run(
        "alternate seed is rejected",
        test_alternate_engineering_seed_is_rejected);
    runner.run(
        "failed invariant remains named",
        test_failed_invariant_remains_named);
    runner.run(
        "wrong schedule hash fails named balance",
        test_wrong_schedule_hash_fails_named_balance);
    runner.run(
        "report contains no scientific fields",
        test_report_contains_no_scientific_fields);
    runner.run(
        "mechanical exit has no rejection state",
        test_exit_code_has_no_scientific_rejection_state);
    return runner.finish();
}
