#include "old_school/oc1_action_regression.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ar1 = old_school::oc1_action_regression;

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

ar1::BalancedReport passing_balanced() {
    ar1::BalancedReport report;
    report.passed = true;
    return report;
}

ar1::FocusedReport passing_focused() {
    ar1::FocusedReport report;
    report.passed = true;
    return report;
}

ar1::DvrReport passing_dvr() {
    ar1::DvrReport report;
    report.passed = true;
    return report;
}

ar1::IntegrityReport passing_integrity() {
    ar1::IntegrityReport report;
    report.passed = true;
    return report;
}

ar1::RunReport small_report() {
    ar1::RunReport report;
    report.scientific.parent_model_fingerprint = "parent";
    report.scientific.candidate_model_fingerprint =
        "candidate";
    report.scientific.actor_cache_contents_hash = "actor";
    report.scientific.dvr_bundle_contents_hash = "dvr";
    report.scientific.actor_cache_metadata.schema = "schema";
    report.scientific.actor_cache_metadata.algorithm =
        "algorithm";
    report.scientific.actor_cache_metadata.semantic_revision =
        "semantics";
    report.scientific.actor_cache_metadata.environment_revision =
        "environment";
    report.scientific.actor_cache_metadata.corpus_id = "corpus";
    report.scientific.actor_cache_metadata
        .reference_model_fingerprint = "actor-model";
    report.scientific.actor_cache_metadata
        .information_set_fingerprint = "information";

    ar1::BalancedDecisionEvidence a;
    a.stable_id = "a";
    a.information_action_fingerprint = "ia-a";
    a.c16_deployment.stable_id = "a";
    a.c16_deployment.actions = {{
        .descriptor = "pass",
        .raw_samples = {0.25, 0.5},
        .raw_score = 0.375,
    }};
    ar1::BalancedDecisionEvidence b;
    b.stable_id = "b";
    b.root_deck = old_school::DeckId::Red;
    b.information_action_fingerprint = "ia-b";
    report.scientific.balanced.decisions = {a, b};
    report.scientific.balanced.passed = true;
    report.scientific.focused.passed = true;
    report.scientific.dvr.passed = true;
    report.integrity.hidden.pooled = {
        .attempted = 2,
        .changed = 2,
        .unchanged = 0,
    };
    report.integrity.passed = true;
    report.gate = ar1::evaluate_gate(
        report.scientific.balanced,
        report.scientific.focused,
        report.scientific.dvr,
        report.integrity);
    return report;
}

void test_gate_is_conjunctive_and_exit_codes_are_sealed() {
    const auto pass = ar1::evaluate_gate(
        passing_balanced(), passing_focused(),
        passing_dvr(), passing_integrity());
    expect(pass.passed, "all-passing gate did not pass");
    expect(
        ar1::exit_code(pass) == 0,
        "passing gate did not exit zero");

    auto balanced = passing_balanced();
    balanced.passed = false;
    const auto rejected = ar1::evaluate_gate(
        balanced, passing_focused(), passing_dvr(),
        passing_integrity());
    expect(
        !rejected.passed &&
            !rejected.infrastructure_failure &&
            ar1::exit_code(rejected) == 1,
        "scientific rejection did not exit one");

    auto integrity = passing_integrity();
    integrity.passed = false;
    const auto infrastructure = ar1::evaluate_gate(
        passing_balanced(), passing_focused(),
        passing_dvr(), integrity);
    expect(
        infrastructure.infrastructure_failure &&
            ar1::exit_code(infrastructure) == 2,
        "integrity failure did not exit two");
}

void test_each_scientific_gate_is_independently_required() {
    for (std::size_t failed = 0; failed < 3; ++failed) {
        auto balanced = passing_balanced();
        auto focused = passing_focused();
        auto dvr = passing_dvr();
        if (failed == 0) {
            balanced.passed = false;
        } else if (failed == 1) {
            focused.passed = false;
        } else {
            dvr.passed = false;
        }
        const auto gate = ar1::evaluate_gate(
            balanced, focused, dvr, passing_integrity());
        expect(
            !gate.passed &&
                !gate.infrastructure_failure &&
                gate.failures.size() == 1,
            "one failed scientific conjunct was not isolated");
    }
}

void test_descriptive_focused_loss_cannot_reject() {
    auto focused = passing_focused();
    focused.descriptive_parent_reference_losses = 3;
    focused.lost_parent_reference_agreements = 0;
    const auto gate = ar1::evaluate_gate(
        passing_balanced(), focused, passing_dvr(),
        passing_integrity());
    expect(
        gate.passed,
        "descriptive focused losses silently tightened gate");
}

void test_scientific_hash_is_order_canonical_and_bit_exact() {
    ar1::RunReport report = small_report();
    const std::string original =
        ar1::testing::scientific_projection_hash(
            report.scientific);

    std::reverse(
        report.scientific.balanced.decisions.begin(),
        report.scientific.balanced.decisions.end());
    expect(
        ar1::testing::scientific_projection_hash(
            report.scientific) == original,
        "stable-ID root reordering changed canonical hash");

    auto& sample =
        report.scientific.balanced.decisions[1]
            .c16_deployment.actions[0]
            .raw_samples[0];
    sample =
        std::nextafter(
            sample,
            std::numeric_limits<double>::infinity());
    expect(
        ar1::testing::scientific_projection_hash(
            report.scientific) != original,
        "one raw IEEE-754 sample bit did not change hash");

    report = small_report();
    const std::string positive_zero =
        ar1::testing::scientific_projection_hash(
            report.scientific);
    report.scientific.balanced.actor_metrics.control
        .mean_regret = -0.0;
    expect(
        ar1::testing::scientific_projection_hash(
            report.scientific) != positive_zero,
        "positive and negative zero hashed identically");
}

void test_full_hash_binds_integrity_and_avoids_self_reference() {
    ar1::RunReport report = small_report();
    const std::string original =
        ar1::testing::canonical_full_report_hash(report);
    report.integrity.full_report_hash = "ignored-self-value";
    expect(
        ar1::testing::canonical_full_report_hash(report) ==
            original,
        "full hash included its own output field");

    ++report.integrity.hidden.pooled.changed;
    expect(
        ar1::testing::canonical_full_report_hash(report) !=
            original,
        "hidden nonvacuity counters were not bound");
}

void test_repeated_full_construction_hash_binds_typed_report() {
    ar1::RunReport report = small_report();
    const std::string original =
        ar1::testing::canonical_construction_report_hash(
            report);
    ar1::RunReport mutated = report;
    ++mutated.integrity.first_before.parent.device;
    expect(
        ar1::testing::canonical_construction_report_hash(
            mutated) != original,
        "construction hash omitted immutable snapshot fields");

    mutated = report;
    mutated.integrity.first_full_construction_hash =
        "self-hash-value";
    expect(
        ar1::testing::canonical_construction_report_hash(
            mutated) == original,
        "construction hash recursively included its own hash");
    expect(
        ar1::testing::canonical_full_report_hash(mutated) !=
            ar1::testing::canonical_full_report_hash(report),
        "final full hash did not bind repeated-construction hashes");
}

void test_complete_bundle_hash_binds_hidden_and_integrity_sides() {
    const ar1::RunReport report = small_report();
    ar1::ConstructionBundleReport bundle;
    bundle.original = report.scientific;
    bundle.hidden = report.scientific;
    bundle.hidden_audit = report.integrity.hidden;
    bundle.original_scientific_hash =
        ar1::testing::scientific_projection_hash(
            bundle.original);
    bundle.hidden_scientific_hash =
        ar1::testing::scientific_projection_hash(
            bundle.hidden);
    bundle.original_hidden_scientific_bit_identical = true;
    bundle.artifacts_unchanged = true;
    bundle.passed = true;
    const std::string original =
        ar1::testing::canonical_construction_bundle_hash(
            bundle);

    ar1::ConstructionBundleReport repeated = bundle;
    repeated.hidden.balanced.actor_metrics.control.mean_regret =
        std::nextafter(
            repeated.hidden.balanced.actor_metrics.control
                .mean_regret,
            std::numeric_limits<double>::infinity());
    expect(
        ar1::testing::canonical_construction_bundle_hash(
            repeated) != original,
        "complete bundle hash omitted repeated hidden evidence");

    repeated = bundle;
    ++repeated.hidden_audit.pooled.changed;
    expect(
        ar1::testing::canonical_construction_bundle_hash(
            repeated) != original,
        "complete bundle hash omitted repeated hidden audit");

    repeated = bundle;
    ++repeated.after.parent.device;
    expect(
        ar1::testing::canonical_construction_bundle_hash(
            repeated) != original,
        "complete bundle hash omitted repeated artifact integrity");
}

void test_snapshot_requirement_fails_closed() {
    const std::filesystem::path path =
        std::filesystem::absolute("synthetic-artifact.bin");
    old_school::artifact_integrity::RegularFileSnapshot snapshot;
    snapshot.path = path.string();
    snapshot.byte_size = 12;
    snapshot.sha256 = "abc";
    const ar1::FileRequirement requirement{
        .path = path.string(),
        .byte_size = 12,
        .sha256 = "abc",
    };
    expect(
        ar1::testing::snapshot_matches_requirement(
            snapshot, requirement),
        "exact synthetic snapshot did not match");

    auto wrong_size = requirement;
    ++wrong_size.byte_size;
    expect(
        !ar1::testing::snapshot_matches_requirement(
            snapshot, wrong_size),
        "wrong byte count was accepted");
    auto wrong_sha = requirement;
    wrong_sha.sha256 = "def";
    expect(
        !ar1::testing::snapshot_matches_requirement(
            snapshot, wrong_sha),
        "wrong SHA was accepted");
    auto wrong_path = requirement;
    wrong_path.path =
        std::filesystem::absolute("other-artifact.bin")
            .string();
    expect(
        !ar1::testing::snapshot_matches_requirement(
            snapshot, wrong_path),
        "wrong path was accepted");
}

void test_focused_family_census_has_seven_bounded_slots() {
    const std::array<std::size_t, ar1::kFocusedFamilyCount>
        exact = {2, 2, 1, 1, 6, 1, 0};
    expect(
        ar1::testing::focused_authored_census_is_exact(
            exact, 13),
        "exact six-authored-family census did not pass");
    auto missing = exact;
    --missing[4];
    expect(
        !ar1::testing::focused_authored_census_is_exact(
            missing, 12),
        "missing field root passed focused census");
    auto leaked_dvr = exact;
    leaked_dvr[6] = 1;
    expect(
        !ar1::testing::focused_authored_census_is_exact(
            leaked_dvr, 14),
        "DVR family leaked into authored focused census");
}

void test_cli_rejects_every_argument_without_running_evidence() {
    char program[] = "old-school-oc1-action-regression";
    char extra[] = "--seed";
    char* argv[] = {program, extra};
    std::ostringstream output;
    std::ostringstream error;
    const int status =
        ar1::run_cli(2, argv, output, error);
    expect(
        status == 2,
        "no-knob CLI accepted an argument");
    expect(
        output.str().empty(),
        "wrong-argument CLI began evidence output");
    expect(
        error.str().find(
            "Usage: old-school-oc1-action-regression") !=
            std::string::npos &&
            error.str().find("accepts no") !=
                std::string::npos,
        "wrong-argument CLI did not explain sealed contract");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "gate is conjunctive and exit codes are sealed",
        test_gate_is_conjunctive_and_exit_codes_are_sealed);
    tests.run(
        "each scientific gate is independently required",
        test_each_scientific_gate_is_independently_required);
    tests.run(
        "descriptive focused loss cannot reject",
        test_descriptive_focused_loss_cannot_reject);
    tests.run(
        "scientific hash is order canonical and bit exact",
        test_scientific_hash_is_order_canonical_and_bit_exact);
    tests.run(
        "full hash binds integrity and avoids self reference",
        test_full_hash_binds_integrity_and_avoids_self_reference);
    tests.run(
        "repeated full construction hash binds typed report",
        test_repeated_full_construction_hash_binds_typed_report);
    tests.run(
        "complete bundle hash binds hidden and integrity sides",
        test_complete_bundle_hash_binds_hidden_and_integrity_sides);
    tests.run(
        "snapshot requirement fails closed",
        test_snapshot_requirement_fails_closed);
    tests.run(
        "focused family census has seven bounded slots",
        test_focused_family_census_has_seven_bounded_slots);
    tests.run(
        "CLI rejects every argument without running evidence",
        test_cli_rejects_every_argument_without_running_evidence);
    return tests.finish();
}
