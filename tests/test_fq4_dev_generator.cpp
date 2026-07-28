#include "old_school/fq4_dev_generator.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace generator =
    old_school::fq4_dev_generator;
namespace bundle =
    old_school::fq4_dev_bundle;
namespace collection =
    old_school::fq4_priority_collection;
namespace schedule_data =
    old_school::fq4_dev_schedule;

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
            std::cerr
                << "[FAIL] " << name << ": "
                << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout
            << passed_ << " passed, "
            << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(
            std::string(message));
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
    throw std::runtime_error(
        std::string(message));
}

collection::CanonicalRoot synthetic_root() {
    const auto fit_schedule =
        schedule_data::source_schedule(
            schedule_data::Split::Fit);
    const collection::RootLocator locator{
        .source_block =
            0,
        .source_seed_base =
            schedule_data::kFitSeedBase,
        .schedule_index = 0,
        .game_seed =
            fit_schedule.front().game_seed,
        .owner_seat = 0,
        .trace_ordinal = 17,
    };
    const std::string information =
        bundle::format_sha256(
            bundle::sha256("information"));
    const std::string stable =
        collection::block_bound_stable_root_id(
            locator, information,
            generator::kStableRootSchema);
    const std::vector<std::string> descriptors{
        "kind-00-pass",
        "kind-01-cast",
    };
    old_school::probes::DecisionProbe probe{
        .stable_id = stable,
        .root_deck = old_school::DeckId::Green,
        .opponent_deck =
            old_school::DeckId::Red,
        .root_player = 0,
        .candidates = {
            {
                .descriptor = descriptors[0],
                .action =
                    old_school::PriorityAction::pass(),
            },
            {
                .descriptor = descriptors[1],
                .action =
                    old_school::PriorityAction::
                        cast_creature(
                            old_school::CardId::
                                GrizzlyBears),
            },
        },
    };
    return {
        .probe = std::move(probe),
        .manifest = {
            .locator = locator,
            .owner_deck =
                old_school::DeckId::Green,
            .opponent_deck =
                old_school::DeckId::Red,
            .stable_id = stable,
            .information_action_fingerprint =
                information,
            .canonical_descriptors = descriptors,
            .pass_index = 0,
        },
        .information_action_bytes =
            "synthetic-owner-information",
    };
}

collection::RobustDominance synthetic_dominance() {
    return {
        .pass_index = 0,
        .complete_world_counts = {0, 8},
        .strict_world_counts = {0, 8},
        .robustly_pass_dominated = {false, true},
        .complete_comparisons = 8,
        .transition_count = 24,
        .shape_valid = true,
    };
}

collection::CanonicalRoot synthetic_transition_root() {
    auto fixtures =
        old_school::probes::make_probe_dev_v3();
    const auto fixture = std::find_if(
        fixtures.begin(), fixtures.end(),
        [](const auto& probe) {
            return probe.decision_kind ==
                       old_school::probes::
                           DecisionKind::Priority &&
                   probe.candidates.size() >= 2;
        });
    if (fixture == fixtures.end()) {
        throw std::runtime_error(
            "development fixtures have no Priority root");
    }
    collection::CanonicalRoot result;
    result.probe = *fixture;
    const collection::RootLocator locator{
        .source_block = 0,
        .source_seed_base = 987654321,
        .schedule_index = 0,
        .game_seed = 123456789,
        .owner_seat =
            fixture->root_player,
        .trace_ordinal = 0,
    };
    const std::string information =
        bundle::format_sha256(
            bundle::sha256(
                "synthetic-transition-information"));
    result.manifest = {
        .locator = locator,
        .owner_deck = fixture->root_deck,
        .opponent_deck =
            fixture->opponent_deck,
        .stable_id =
            collection::block_bound_stable_root_id(
                locator, information,
                generator::kStableRootSchema),
        .information_action_fingerprint =
            information,
    };
    result.probe.stable_id =
        result.manifest.stable_id;
    for (std::size_t index = 0;
         index < fixture->candidates.size(); ++index) {
        const auto& candidate =
            fixture->candidates[index];
        result.manifest.canonical_descriptors.push_back(
            candidate.descriptor);
        const auto* action =
            std::get_if<old_school::PriorityAction>(
                &candidate.action);
        if (action == nullptr) {
            throw std::runtime_error(
                "Priority fixture has a non-Priority action");
        }
        if (action->kind ==
            old_school::PriorityActionKind::Pass) {
            result.manifest.pass_index = index;
        }
    }
    return result;
}

void test_schedule_preflight() {
    const auto fit =
        schedule_data::source_schedule(
            schedule_data::Split::Fit);
    const auto check =
        schedule_data::source_schedule(
            schedule_data::Split::Check);
    const auto exact =
        generator::preflight_schedules(
            fit, check);
    expect(exact.exact, "frozen schedules did not pass");
    expect(
        exact.fit_sha256 ==
            schedule_data::
                kExpectedFitScheduleSha256 &&
            exact.check_sha256 ==
                schedule_data::
                    kExpectedCheckScheduleSha256,
        "schedule preflight did not preserve literal hashes");

    auto changed_fit = fit;
    changed_fit.front().game_seed ^= 1U;
    const auto changed =
        generator::preflight_schedules(
            changed_fit, check);
    expect(
        !changed.exact &&
            !changed.failures.empty(),
        "schedule mutation was accepted");

    auto overlapping_check = check;
    overlapping_check.front().game_seed =
        fit.front().game_seed;
    const auto overlap =
        generator::preflight_schedules(
            fit, overlapping_check);
    expect(
        !overlap.exact,
        "cross-split seed overlap was accepted");
}

void test_collection_firewall() {
    const collection::CollectionSpec& spec =
        generator::collection_spec();
    expect(spec.valid(), "development collection spec is invalid");
    expect(
        spec.hidden_seed_namespace ==
                bundle::kHiddenNamespace &&
            spec.dominance_seed_namespace ==
                bundle::kDominanceNamespace,
        "development namespaces drifted");
    expect(
        spec.hidden_seed_namespace !=
                generator::
                    kForbiddenHeldOutHiddenNamespace &&
            spec.dominance_seed_namespace !=
                generator::
                    kForbiddenHeldOutDominanceNamespace &&
            schedule_data::kGenerationNamespace !=
                generator::
                    kForbiddenHeldOutGenerationNamespace,
        "held-out namespace crossed the structural firewall");
    expect(
        generator::kForbiddenSourceSeedBases ==
            std::array<std::uint64_t, 4>{
                790,
                791,
                202607280210ULL,
                202607280211ULL,
            } &&
        schedule_data::kFitSeedBase !=
                generator::
                    kForbiddenSourceSeedBases[0] &&
            schedule_data::kFitSeedBase !=
                generator::
                    kForbiddenSourceSeedBases[1] &&
            schedule_data::kCheckSeedBase !=
                generator::
                    kForbiddenSourceSeedBases[0] &&
            schedule_data::kCheckSeedBase !=
                generator::
                    kForbiddenSourceSeedBases[1] &&
            schedule_data::kFitSeedBase !=
                generator::
                    kForbiddenSourceSeedBases[2] &&
            schedule_data::kFitSeedBase !=
                generator::
                    kForbiddenSourceSeedBases[3] &&
            schedule_data::kCheckSeedBase !=
                generator::
                    kForbiddenSourceSeedBases[2] &&
            schedule_data::kCheckSeedBase !=
                generator::
                    kForbiddenSourceSeedBases[3],
        "held-out source seed crossed the firewall");

    const std::string expected_contract =
        "old-school-fq4-priority-dev-collection-spec-v2\n"
        "owner-information-schema="
        "old-school-fq4-priority-dev-owner-information-action-v2\n"
        "stable-root-schema="
        "old-school-fq4-priority-dev-stable-root-v2\n"
        "block-bound-ids=1\n"
        "replay-manifest-schema="
        "old-school-fq4-priority-dev-retained-manifest-v2\n"
        "hidden-seed-namespace=5066888523877075017\n"
        "hidden-seed-scope="
        "old-school-fq4-priority-dev-hidden-v2\n"
        "dominance-seed-namespace=5066888523877073999\n"
        "dominance-seed-scope="
        "old-school-fq4-priority-dev-dominance-v2\n"
        "maximum-legal-actions=32\n"
        "maximum-roots-per-owner-game=16\n"
        "dominance-worlds=8\n";
    expect(
        generator::collection_spec_contract_bytes() ==
            expected_contract,
        "collection-spec contract bytes drifted");
    expect(
        generator::collection_spec_contract_sha256() ==
                bundle::sha256(expected_contract) &&
            bundle::format_sha256(
                generator::
                    collection_spec_contract_sha256()) ==
                bundle::kCollectionSpecSha256,
        "collection-spec contract hash drifted");
}

void test_feature_contract_and_sparse_encoding() {
    const std::string contract =
        generator::feature_contract_bytes();
    expect(
        contract.find("feature-count=893\n") !=
            std::string::npos,
        "feature contract omitted its dimension");
    expect(
        generator::feature_contract_sha256() ==
            bundle::sha256(contract),
        "feature contract hash is not canonical");
    expect(
        bundle::format_sha256(
            generator::feature_contract_sha256()) ==
            bundle::kFeatureContractSha256,
        "feature contract hash drifted from its literal golden");

    std::vector<double> dense(
        bundle::kFeatureCount, 0.0);
    dense[1] = -0.0;
    dense[7] = 1.25;
    dense[892] = -3.5;
    const auto sparse =
        generator::sparsify_priority_features(
            dense);
    expect(
        sparse.size() == 3 &&
            sparse[0].index == 1 &&
            sparse[0].value_bits ==
                0x8000000000000000ULL &&
            sparse[1].index == 7 &&
            sparse[2].index == 892,
        "sparse encoding did not omit only positive zero");

    dense.pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                generator::
                    sparsify_priority_features(
                        dense));
        },
        "wrong feature dimension was accepted");
    dense.resize(bundle::kFeatureCount, 0.0);
    dense[3] =
        std::numeric_limits<double>::infinity();
    expect_rejected(
        [&] {
            static_cast<void>(
                generator::
                    sparsify_priority_features(
                        dense));
        },
        "nonfinite feature was accepted");
}

void test_census_adapter() {
    const auto root = synthetic_root();
    const auto dominance =
        synthetic_dominance();
    const bundle::CensusRow row =
        generator::make_census_row(
            root, dominance);
    expect(
        row.schedule_index == 0 &&
            row.owner_seat == 0 &&
            row.trace_ordinal == 17 &&
            row.owner_deck == 0 &&
            row.opponent_deck == 1 &&
            row.pass_index == 0,
        "public census locator drifted");
    expect(
        row.stable_root_id ==
                bundle::parse_sha256(
                    root.manifest.stable_id) &&
            row.stable_root_id ==
                bundle::expected_stable_root_sha256(
                    bundle::Split::Fit,
                    row.schedule_block,
                    row.schedule_index,
                    row.owner_seat,
                    row.trace_ordinal,
                    row.information_action_sha256) &&
            row.physical_game_sha256 ==
                bundle::sha256(
                    collection::block_bound_physical_game_id(
                        root.manifest.locator)) &&
            row.physical_game_sha256 ==
                bundle::expected_physical_game_sha256(
                    bundle::Split::Fit,
                    row.schedule_block,
                    row.schedule_index) &&
            row.information_action_sha256 ==
                bundle::parse_sha256(
                    root.manifest
                        .information_action_fingerprint),
        "census hashes do not bind collector bytes");
    expect(
        row.dominance.size() == 2 &&
            row.dominance[0] ==
                bundle::DominanceCount{
                    .complete = 0,
                    .strict = 0,
                } &&
            row.dominance[1] ==
                bundle::DominanceCount{
                    .complete = 8,
                    .strict = 8,
                },
        "census dominance counts drifted");

    auto bad = dominance;
    bad.complete_world_counts[0] = 8;
    expect_rejected(
        [&] {
            static_cast<void>(
                generator::make_census_row(
                    root, bad));
        },
        "nonzero Pass dominance count was accepted");
    bad = dominance;
    bad.strict_world_counts[1] = 7;
    expect_rejected(
        [&] {
            static_cast<void>(
                generator::make_census_row(
                    root, bad));
        },
        "robust mask/count disagreement was accepted");

    auto wrong_game = root;
    wrong_game.manifest.locator.game_seed ^= 1U;
    wrong_game.manifest.stable_id =
        collection::block_bound_stable_root_id(
            wrong_game.manifest.locator,
            wrong_game.manifest
                .information_action_fingerprint,
            generator::kStableRootSchema);
    wrong_game.probe.stable_id =
        wrong_game.manifest.stable_id;
    expect_rejected(
        [&] {
            static_cast<void>(
                generator::make_census_row(
                    wrong_game, dominance));
        },
        "non-schedule game seed was accepted");

    auto held_out = root;
    held_out.manifest.locator.source_seed_base =
        generator::kForbiddenSourceSeedBases[0];
    held_out.manifest.stable_id =
        collection::block_bound_stable_root_id(
            held_out.manifest.locator,
            held_out.manifest
                .information_action_fingerprint,
            generator::kStableRootSchema);
    held_out.probe.stable_id =
        held_out.manifest.stable_id;
    expect_rejected(
        [&] {
            static_cast<void>(
                generator::make_census_row(
                    held_out, dominance));
        },
        "held-out source coordinate was accepted");

    auto wrong_deck = root;
    wrong_deck.manifest.owner_deck =
        old_school::DeckId::Blue;
    wrong_deck.probe.root_deck =
        old_school::DeckId::Blue;
    expect_rejected(
        [&] {
            static_cast<void>(
                generator::make_census_row(
                    wrong_deck, dominance));
        },
        "schedule-inconsistent owner deck was accepted");
}

void test_support_floor() {
    std::vector<bundle::CensusRow> census;
    std::vector<bundle::SelectedRow> selected;
    std::vector<generator::ParentWitness> witnesses;
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        bundle::CensusRow row{
            .owner_deck =
                static_cast<std::uint8_t>(deck),
            .stable_root_id =
                bundle::sha256(
                    "root-" +
                    std::to_string(deck)),
            .physical_game_sha256 =
                bundle::sha256(
                    "game-" +
                    std::to_string(deck)),
        };
        census.push_back(row);
        selected.push_back({
            .split = bundle::Split::Fit,
            .census = row,
            .roles = static_cast<std::uint8_t>(
                bundle::Role::DominancePositive |
                bundle::Role::BackgroundControl),
        });
        witnesses.push_back({
            .owner_deck =
                static_cast<old_school::DeckId>(
                    deck),
            .stable_root_id =
                row.stable_root_id,
            .physical_game_sha256 =
                row.physical_game_sha256,
            .classification =
                collection::ParentClass::Class1,
        });
    }
    const auto complete =
        generator::summarize_support(
            census, selected, witnesses);
    expect(
        complete.publishable() &&
            complete.failed_gate_mask == 0 &&
            complete.high_confidence_roots == 5 &&
            complete.high_confidence_games == 5 &&
            complete.high_confidence_decks == 5,
        "complete synthetic split missed support floor");

    auto weak = witnesses;
    weak.back().classification =
        collection::ParentClass::Safe;
    const auto underpowered =
        generator::summarize_support(
            census, selected, weak);
    expect(
        underpowered.coverage_met &&
            !underpowered.parent_error_floor_met &&
            underpowered.failed_gate_mask ==
                generator::kParentErrorGateFailed,
        "underpowered parent-error split was publishable");

    auto coverage_only_rows = selected;
    coverage_only_rows.back().roles =
        bundle::Role::DominancePositive;
    const auto coverage_only =
        generator::summarize_support(
            census, coverage_only_rows, witnesses);
    expect(
        !coverage_only.coverage_met &&
            coverage_only.parent_error_floor_met &&
            coverage_only.failed_gate_mask ==
                generator::kCoverageGateFailed,
        "coverage-only failure did not preserve parent support");

    auto both_rows = selected;
    both_rows.back().roles =
        static_cast<std::uint8_t>(
            bundle::Role::BackgroundControl);
    auto both_witnesses = witnesses;
    both_witnesses.pop_back();
    const auto both_failed =
        generator::summarize_support(
            census, both_rows, both_witnesses);
    expect(
        !both_failed.coverage_met &&
            !both_failed.parent_error_floor_met &&
            both_failed.failed_gate_mask ==
                (generator::kCoverageGateFailed |
                 generator::kParentErrorGateFailed),
        "two-gate failure did not preserve both failure bits");

    auto alien = witnesses;
    alien.front().stable_root_id =
        bundle::sha256("not-selected");
    const auto inconsistent =
        generator::summarize_support(
            census, selected, alien);
    expect(
        !inconsistent.publishable(),
        "witness outside selected positives was accepted");

    const std::string report =
        generator::format_support_report(
            coverage_only, underpowered);
    const std::size_t fit_start =
        report.find("support split=fit deck=0");
    const std::size_t fit_pooled =
        report.find("support split=fit scope=pooled");
    const std::size_t check_start =
        report.find("support split=check deck=0");
    const std::size_t check_pooled =
        report.find("support split=check scope=pooled");
    const auto count_field =
        [&report](std::string_view field) {
            std::size_t count = 0;
            std::size_t position = 0;
            while ((position =
                        report.find(field, position)) !=
                   std::string::npos) {
                ++count;
                position += field.size();
            }
            return count;
        };
    expect(
        fit_start == 0 &&
            fit_pooled != std::string::npos &&
            check_start != std::string::npos &&
            check_pooled != std::string::npos &&
            fit_pooled < check_start &&
            check_start < check_pooled &&
            report.find("failed_gate_mask=1") !=
                std::string::npos &&
            report.find(
                "failed_gate_mask=2",
                check_start) !=
                std::string::npos &&
            count_field(
                "high_confidence_roots=") == 2 &&
            count_field(
                "high_confidence_games=") == 2 &&
            static_cast<std::size_t>(
                std::count(
                    report.begin(),
                    report.end(), '\n')) == 12,
        "count-only report did not emit complete FIT then CHECK "
        "reports before a conjunctive decision");
    expect(
        report.find("root-") == std::string::npos &&
            report.find("game-") == std::string::npos &&
            report.find("score") == std::string::npos &&
            report.find("action") == std::string::npos &&
            report.find("outcome") == std::string::npos,
        "count-only report leaked an unlicensed row field");

    auto changed_count = coverage_only;
    ++changed_count.census_by_deck[0];
    expect(
        generator::format_support_report(
            changed_count, underpowered) !=
            report,
        "count-only report ignored a count mutation");
    auto changed_bit = coverage_only;
    changed_bit.failed_gate_mask ^=
        generator::kParentErrorGateFailed;
    expect(
        generator::format_support_report(
            changed_bit, underpowered) !=
            report,
        "count-only report ignored a gate-bit mutation");

    const generator::FailureScopeReport rejection_scope{
        .executable_after_sha256 =
            std::string(64, 'a'),
        .parent_after_sha256 =
            std::string(64, 'b'),
        .executable_snapshot_ok = true,
        .parent_snapshot_ok = true,
        .artifact_status_known = true,
        .artifact_present = false,
        .temporary_status_known = true,
        .temporary_absent = true,
    };
    const std::string rejection =
        generator::format_support_rejection_output(
            coverage_only, underpowered,
            rejection_scope);
    const std::size_t rejection_fit =
        rejection.find(
            "support split=fit deck=0");
    const std::size_t rejection_check =
        rejection.find(
            "support split=check deck=0");
    const std::size_t rejection_postcondition =
        rejection.find(
            "postcondition executable_after_sha256=");
    const std::size_t rejection_verdict =
        rejection.find("result=NOT_PUBLISHED");
    expect(
        rejection_fit == 0 &&
            rejection_check != std::string::npos &&
            rejection_postcondition !=
                std::string::npos &&
            rejection_verdict !=
                std::string::npos &&
            rejection_fit < rejection_check &&
            rejection_check <
                rejection_postcondition &&
            rejection_postcondition <
                rejection_verdict &&
            rejection.ends_with(
                "result=NOT_PUBLISHED"
                " reason=support_gate_failed"
                " failed_gate_mask_fit=1"
                " failed_gate_mask_check=2\n") &&
            rejection.find(
                "result=NOT_PUBLISHED",
                rejection_verdict + 1) ==
                std::string::npos &&
            rejection.find("root-") ==
                std::string::npos &&
            rejection.find("game-") ==
                std::string::npos &&
            rejection.find("action") ==
                std::string::npos &&
            rejection.find("outcome") ==
                std::string::npos &&
            rejection.find("score") ==
                std::string::npos,
        "support rejection output did not preserve report, "
        "postcondition, verdict order and privacy");
}

void test_repeat_identity_helper() {
    const std::string bytes{"a\0b", 3};
    expect(
        generator::
            complete_constructions_byte_identical(
                bytes, bytes),
        "equal binary bundles did not compare exact");
    std::string changed = bytes;
    changed[1] = '\1';
    expect(
        !generator::
             complete_constructions_byte_identical(
                 bytes, changed),
        "one-bit bundle drift was accepted");
}

void test_failure_scope_report() {
    generator::FailureScopeReport scope{
        .executable_after_sha256 =
            std::string(64, 'a'),
        .parent_after_sha256 =
            std::string(64, 'b'),
        .executable_snapshot_ok = true,
        .parent_snapshot_ok = true,
        .artifact_status_known = true,
        .artifact_present = false,
        .temporary_status_known = true,
        .temporary_absent = true,
    };
    scope.progress.source_games_completed = {{
        {{160, 80}},
        {{40, 0}},
    }};
    const std::string report =
        generator::format_failure_scope_report(scope);
    const std::size_t c0_fit =
        report.find(
            "postcondition construction=0 split=fit "
            "source_games_completed=160");
    const std::size_t c0_check =
        report.find(
            "postcondition construction=0 split=check "
            "source_games_completed=80");
    const std::size_t c1_fit =
        report.find(
            "postcondition construction=1 split=fit "
            "source_games_completed=40");
    const std::size_t c1_check =
        report.find(
            "postcondition construction=1 split=check "
            "source_games_completed=0");
    expect(
        report.starts_with(
            "postcondition executable_after_sha256=") &&
            report.find("artifact_present=0") !=
                std::string::npos &&
            report.find("temporary_absent=1") !=
                std::string::npos &&
            report.find(
                "candidate_rollout_evaluations=0") !=
                std::string::npos &&
            c0_fit != std::string::npos &&
            c0_check != std::string::npos &&
            c1_fit != std::string::npos &&
            c1_check != std::string::npos &&
            c0_fit < c0_check &&
            c0_check < c1_fit &&
            c1_fit < c1_check,
        "failure scope omitted or reordered a postcondition");

    const generator::GenerationFailure failure(
        "synthetic failure", scope);
    expect(
        std::string_view(failure.what()) ==
                "synthetic failure" &&
            failure.scope() == scope,
        "generation failure did not retain its scope report");
}

void test_direct_robust_dominance_evaluation() {
    const collection::CanonicalRoot root =
        synthetic_transition_root();
    std::vector<std::string> first_failures;
    const collection::RobustDominance first =
        collection::evaluate_robust_dominance(
            root, generator::collection_spec(),
            first_failures);
    std::vector<std::string> second_failures;
    const collection::RobustDominance second =
        collection::evaluate_robust_dominance(
            root, generator::collection_spec(),
            second_failures);
    expect(
        first_failures.empty() &&
            second_failures.empty(),
        "direct dominance evaluation reported infrastructure failure");
    expect(
        first == second &&
            first_failures == second_failures,
        "direct dominance evaluation is not deterministic");
    expect(
        first.shape_valid &&
            first.transition_count ==
                collection::kDominanceWorlds *
                    root.probe.candidates.size(),
        "direct dominance transition accounting drifted");
    expect(
        first.complete_world_counts.size() ==
                root.probe.candidates.size() &&
            first.strict_world_counts.size() ==
                root.probe.candidates.size() &&
            first.robustly_pass_dominated.size() ==
                root.probe.candidates.size(),
        "direct dominance result has wrong shape");
    for (std::size_t action = 0;
         action < root.probe.candidates.size();
         ++action) {
        expect(
            first.strict_world_counts[action] <=
                    first.complete_world_counts[action] &&
                first.complete_world_counts[action] <=
                    collection::kDominanceWorlds,
            "direct dominance counts violate bounds");
        expect(
            first.robustly_pass_dominated[action] ==
                (action != first.pass_index &&
                 first.strict_world_counts[action] ==
                     collection::kDominanceWorlds),
            "direct dominance robust mask drifted");
    }
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "frozen schedule preflight",
        test_schedule_preflight);
    runner.run(
        "held-out collection firewall",
        test_collection_firewall);
    runner.run(
        "portable feature contract and sparse encoding",
        test_feature_contract_and_sparse_encoding);
    runner.run(
        "census wire adapter",
        test_census_adapter);
    runner.run(
        "split coverage and parent-error floor",
        test_support_floor);
    runner.run(
        "complete-construction byte identity",
        test_repeat_identity_helper);
    runner.run(
        "failure-scope count-only report",
        test_failure_scope_report);
    runner.run(
        "direct robust-dominance evaluation",
        test_direct_robust_dominance_evaluation);
    return runner.finish();
}
