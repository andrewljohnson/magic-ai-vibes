#include "old_school/rb0_mechanical_preflight.hpp"
#include "old_school/audit_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::rb0_mechanical_preflight {
namespace {

using ActorKey = std::pair<std::size_t, std::size_t>;
using ActorTurnKey =
    std::tuple<std::size_t, std::size_t, std::size_t>;

constexpr std::size_t kTrainingGames = 800;
constexpr std::size_t kSelfPlayGenerations = 16;
constexpr std::size_t kMaximumGameTurns = 500;

using audit_common::is_lower_hex_digest;
using audit_common::mass_tolerance;

bool capture_schedule_balanced(
    const rb0::Capture& capture,
    std::size_t balanced_blocks) {
    const std::size_t expected_games =
        balanced_blocks *
        learned_iteration::kBalancedScheduleGames;
    const std::size_t expected_cell =
        balanced_blocks * (kDeckCount - 1);
    bool balanced =
        capture.physical_games == expected_games &&
        capture.actor_games == 2 * expected_games;
    for (const auto& deck :
         capture.deck_seat_started_counts) {
        for (const auto& seat : deck) {
            for (const std::size_t count : seat) {
                balanced =
                    balanced && count == expected_cell;
            }
        }
    }
    for (std::size_t first = 0;
         first < kDeckCount; ++first) {
        for (std::size_t second = 0;
             second < kDeckCount; ++second) {
            const std::size_t expected =
                first == second
                    ? 0
                    : 2 * balanced_blocks;
            balanced =
                balanced &&
                capture.ordered_pair_counts[first][second] ==
                    expected;
        }
    }
    return balanced;
}

double turn_tolerance_at_maximum_error(
    const rb0::Capture& capture) {
    if (capture.records.empty() ||
        capture.weights.actor_games == 0) {
        return mass_tolerance(0.0);
    }
    std::map<ActorKey, std::set<std::size_t>> actor_turns;
    std::map<ActorTurnKey, long double> turn_masses;
    for (const rb0::AuditRecord& record : capture.records) {
        const ActorKey actor = {
            record.physical_game, record.perspective};
        actor_turns[actor].insert(record.root_turn);
        turn_masses[{
            record.physical_game,
            record.perspective,
            record.root_turn,
        }] += record.treatment_weight;
    }

    const double expected_actor =
        static_cast<double>(capture.records.size()) /
        static_cast<double>(capture.weights.actor_games);
    double maximum_error = -1.0;
    double selected_tolerance = mass_tolerance(0.0);
    for (const auto& [actor, turns] : actor_turns) {
        if (turns.empty()) {
            continue;
        }
        const double expected_turn =
            expected_actor /
            static_cast<double>(turns.size());
        const double tolerance =
            mass_tolerance(expected_turn);
        for (const std::size_t turn : turns) {
            const double error = std::abs(
                static_cast<double>(
                    turn_masses.at({
                        actor.first, actor.second, turn})) -
                expected_turn);
            if (error > maximum_error ||
                (error == maximum_error &&
                 tolerance < selected_tolerance)) {
                maximum_error = error;
                selected_tolerance = tolerance;
            }
        }
    }
    return selected_tolerance;
}

void add_capture_invariants(
    std::vector<NamedInvariant>& result,
    std::string_view name,
    const CaptureEvidence& capture) {
    const auto add =
        [&](std::string_view suffix, bool passed) {
            result.push_back({
                .name =
                    std::string(name) + "." +
                    std::string(suffix),
                .passed = passed,
            });
        };
    add("accounting.physical-games",
        capture.physical_game_count_exact);
    add("accounting.actor-games",
        capture.actor_game_count_exact);
    add("accounting.rows-present", capture.rows_present);
    add("accounting.rootless-zero",
        capture.rootless_actor_games_zero);
    add("schedule.balance", capture.schedule_balanced);
    add("hashes.well-formed", capture.hashes_well_formed);
    add("trace.identity", capture.trace_invariants_passed);
    add("ro4.identity", capture.ro4_identity_passed);
    add("terminal-tail.identity",
        capture.terminal_tail_identity_passed);
    add("hidden.repartition",
        capture.hidden_repartition_passed);
    add("hidden.changed-state-present",
        capture.hidden_changed_state_present);
    add("hidden.grouping",
        capture.hidden_grouping_identity_passed);
    add("hidden.target-hash",
        capture.hidden_target_hash_identity_passed);
    add("hidden.weight",
        capture.hidden_weight_identity_passed);
    add("hidden.scoring-hash",
        capture.hidden_scoring_hash_identity_passed);
    add("weight.finite-positive",
        capture.weights.finite_positive);
    add("weight.global-mass",
        capture.weights.global_mass_identity);
    add("weight.actor-mass",
        capture.weights.actor_mass_identity);
    add("weight.turn-mass",
        capture.weights.turn_mass_identity);
    add("weight.identity", capture.weight_identity_passed);
}

void write_boolean(
    std::ostream& output, std::string_view name,
    bool passed) {
    output << "mechanical\t" << name << '\t'
           << (passed ? "PASS" : "FAIL") << '\n';
}

void write_capture(
    std::ostream& output, std::string_view name,
    const CaptureEvidence& capture) {
    output << "counts\t" << name
           << "\tphysical_games=" << capture.physical_games
           << ";actor_games=" << capture.actor_games
           << ";rows=" << capture.rows
           << ";rootless_actor_games="
           << capture.rootless_actor_games
           << ";hidden_changed_states="
           << capture.hidden_repartition_states << '\n'
           << "hash\t" << name << ".schedule\t"
           << capture.schedule_hash << '\n'
           << "hash\t" << name << ".trace\t"
           << capture.trace_hash << '\n'
           << "hash\t" << name << ".outcome\t"
           << capture.outcome_hash << '\n'
           << "hash\t" << name << ".feature\t"
           << capture.feature_hash << '\n'
           << "hash\t" << name << ".grouping\t"
           << capture.grouping_hash << '\n'
           << "hash\t" << name << ".ro4\t"
           << capture.ro4_target_hash << '\n'
           << "hash\t" << name << ".weight\t"
           << capture.weight_hash << '\n'
           << "hash\t" << name << ".scoring\t"
           << capture.scoring_hash << '\n'
           << "weight-mass\t" << name
           << ".global\tmax_error="
           << capture.weights.maximum_global_mass_error
           << ";tolerance="
           << capture.global_mass_tolerance << '\n'
           << "weight-mass\t" << name
           << ".actor\tmax_error="
           << capture.weights.maximum_actor_mass_error
           << ";tolerance="
           << capture.actor_mass_tolerance << '\n'
           << "weight-mass\t" << name
           << ".turn\tmax_error="
           << capture.weights.maximum_turn_mass_error
           << ";tolerance_at_max_error="
           << capture.turn_mass_tolerance_at_maximum_error
           << '\n';
}

} // namespace

void require_engineering_seed(std::uint64_t seed) {
    if (seed == rb0::kAuditSeed) {
        throw std::invalid_argument(
            "RB0-E1 rejects quarantined RB0-0 seed " +
            std::to_string(rb0::kAuditSeed));
    }
    if (seed != kEngineeringSeed) {
        throw std::invalid_argument(
            "RB0-E1 requires exact engineering seed " +
            std::to_string(kEngineeringSeed));
    }
}

CaptureEvidence inspect_capture(
    const rb0::Capture& capture, std::uint64_t seed,
    std::size_t generation, std::size_t balanced_blocks) {
    require_engineering_seed(seed);
    CaptureEvidence result;
    result.physical_games = capture.physical_games;
    result.actor_games = capture.actor_games;
    result.rows = capture.records.size();
    result.rootless_actor_games =
        capture.rootless_actor_games;
    result.hidden_repartition_states =
        capture.hidden_repartition_states;
    result.weights = capture.weights;
    result.global_mass_tolerance =
        mass_tolerance(
            capture.weights.expected_total_weight);
    result.actor_mass_tolerance =
        mass_tolerance(
            capture.weights.expected_actor_weight);
    result.turn_mass_tolerance_at_maximum_error =
        turn_tolerance_at_maximum_error(capture);
    const std::size_t expected_games =
        balanced_blocks *
        learned_iteration::kBalancedScheduleGames;
    result.physical_game_count_exact =
        capture.physical_games == expected_games;
    result.actor_game_count_exact =
        capture.actor_games == 2 * expected_games;
    result.rows_present = !capture.records.empty();
    result.rootless_actor_games_zero =
        capture.rootless_actor_games == 0;
    result.schedule_balanced =
        generation == kGeneration &&
        capture_schedule_balanced(
            capture, balanced_blocks) &&
        capture.schedule_hash ==
            rb0::audit_schedule_hash(
                rb0::audit_schedule(
                    seed, generation,
                    balanced_blocks));
    result.hashes_well_formed =
        is_lower_hex_digest(capture.schedule_hash) &&
        is_lower_hex_digest(capture.trace_hash) &&
        is_lower_hex_digest(capture.outcome_hash) &&
        is_lower_hex_digest(capture.feature_hash) &&
        is_lower_hex_digest(capture.grouping_hash) &&
        is_lower_hex_digest(capture.ro4_target_hash) &&
        is_lower_hex_digest(capture.weight_hash) &&
        is_lower_hex_digest(capture.scoring_hash);
    result.trace_invariants_passed =
        capture.trace_invariants_passed;
    result.ro4_identity_passed =
        capture.ro4_identity_passed;
    result.terminal_tail_identity_passed =
        capture.terminal_tail_identity_passed;
    result.hidden_repartition_passed =
        capture.hidden_repartition_passed;
    result.hidden_changed_state_present =
        capture.hidden_repartition_states > 0;
    result.hidden_grouping_identity_passed =
        capture.hidden_grouping_identity_passed;
    result.hidden_target_hash_identity_passed =
        capture.hidden_target_hash_identity_passed;
    result.hidden_weight_identity_passed =
        capture.hidden_weight_identity_passed;
    result.hidden_scoring_hash_identity_passed =
        capture.hidden_scoring_hash_identity_passed;
    result.weight_identity_passed =
        capture.weight_identity_passed;
    result.schedule_hash = capture.schedule_hash;
    result.trace_hash = capture.trace_hash;
    result.outcome_hash = capture.outcome_hash;
    result.feature_hash = capture.feature_hash;
    result.grouping_hash = capture.grouping_hash;
    result.ro4_target_hash = capture.ro4_target_hash;
    result.weight_hash = capture.weight_hash;
    result.scoring_hash = capture.scoring_hash;
    return result;
}

std::vector<NamedInvariant> named_invariants(
    const Report& report) {
    std::vector<NamedInvariant> result = {
        {"configuration.engineering-seed",
         report.exact_engineering_seed},
        {"configuration.quarantined-seed-excluded",
         report.quarantined_seed_excluded},
        {"configuration.generation",
         report.exact_generation},
        {"configuration.blocks", report.exact_block_count},
        {"artifact.snapshot-bound",
         report.artifact_snapshot_bound},
        {"artifact.parent-fingerprint",
         report.parent_fingerprint_exact},
        {"artifact.parent-schema", report.parent_schema_exact},
        {"artifact.after-load",
         report.artifact_unchanged_after_load},
        {"artifact.after-canonical",
         report.artifact_unchanged_after_canonical},
        {"artifact.after-repeat",
         report.artifact_unchanged_after_repeat},
        {"artifact.after-reverse",
         report.artifact_unchanged_after_reverse},
        {"artifact.after-single-worker",
         report.artifact_unchanged_after_single_worker},
        {"artifact.final", report.artifact_unchanged_final},
    };
    for (std::size_t index = 0;
         index < report.captures.size(); ++index) {
        add_capture_invariants(
            result, kCaptureNames[index],
            report.captures[index]);
    }
    result.push_back({
        "equality.repeat.capture-bit-identical",
        report.repeated_capture_bit_identical,
    });
    result.push_back({
        "equality.reverse.capture-bit-identical",
        report.reversed_capture_bit_identical,
    });
    result.push_back({
        "equality.worker.capture-bit-identical",
        report.worker_capture_bit_identical,
    });
    return result;
}

bool mechanically_clean(const Report& report) {
    const std::vector<NamedInvariant> invariants =
        named_invariants(report);
    return std::all_of(
        invariants.begin(), invariants.end(),
        [](const NamedInvariant& invariant) {
            return invariant.passed;
        });
}

Report run(std::ostream& progress) {
    require_engineering_seed(kEngineeringSeed);
    const std::string artifact_path =
        learned_value_challenger_cache_path(
            kTrainingGames, kDefaultLearnedTrainingSeed,
            kSelfPlayGenerations);

    Report report;
    report.seed = kEngineeringSeed;
    report.generation = kGeneration;
    report.balanced_blocks = kBalancedBlocks;
    report.exact_engineering_seed =
        report.seed == kEngineeringSeed;
    report.quarantined_seed_excluded =
        report.seed != rb0::kAuditSeed;
    report.exact_generation =
        report.generation == kGeneration;
    report.exact_block_count =
        report.balanced_blocks == kBalancedBlocks;
    report.artifact_before =
        rb0::require_artifact_snapshot(artifact_path);
    report.artifact_snapshot_bound =
        report.artifact_before.path == artifact_path &&
        report.artifact_before.size > 0 &&
        is_lower_hex_digest(
            report.artifact_before.content_hash);

    progress << "Loading exact frozen Environment-v3 C16..."
             << std::flush;
    const auto artifact =
        load_learned_value_challenger_artifact(
            artifact_path, kTrainingGames,
            kDefaultLearnedTrainingSeed,
            kSelfPlayGenerations);
    const auto parent = artifact.model();
    report.parent_fingerprint =
        learned_model_fingerprint(parent);
    report.parent_fingerprint_exact =
        report.parent_fingerprint == rb0::kParentFingerprint;
    if (!report.parent_fingerprint_exact) {
        throw std::runtime_error(
            "RB0-E1 frozen C16 fingerprint mismatch at '" +
            artifact_path + "': expected " +
            std::string(rb0::kParentFingerprint) + ", got " +
            report.parent_fingerprint);
    }
    report.parent_schema_exact =
        learned_critic_schema(parent) ==
        LearnedCriticSchema::LegacyStateOnly;
    if (!report.parent_schema_exact) {
        throw std::runtime_error(
            "RB0-E1 frozen C16 at '" + artifact_path +
            "' uses the wrong critic schema");
    }
    report.artifact_unchanged_after_load =
        rb0::require_artifact_snapshot(artifact_path) ==
        report.artifact_before;
    progress << " done\n";

    const std::vector<rb0::AuditTask> tasks =
        rb0::audit_schedule(
            kEngineeringSeed, kGeneration,
            kBalancedBlocks);
    const rb0::CaptureConfig parallel_config = {
        .max_game_turns = kMaximumGameTurns,
        .worker_count = kWorkerCount,
        .verify_hidden_repartition = true,
        .schedule_seed = kEngineeringSeed,
        .schedule_generation = kGeneration,
    };
    progress
        << "Constructing RB0-E1 mechanical corpus four times "
           "(2,400 games each: repeat, reverse, and fixed "
           "1-vs-4 workers; K=1/H=4)..."
        << std::flush;
    const rb0::Capture canonical =
        rb0::collect(tasks, parent, parallel_config);
    report.artifact_unchanged_after_canonical =
        rb0::require_artifact_snapshot(artifact_path) ==
        report.artifact_before;

    const rb0::Capture repeated =
        rb0::collect(tasks, parent, parallel_config);
    report.artifact_unchanged_after_repeat =
        rb0::require_artifact_snapshot(artifact_path) ==
        report.artifact_before;

    std::vector<rb0::AuditTask> reversed_tasks = tasks;
    std::reverse(
        reversed_tasks.begin(), reversed_tasks.end());
    const rb0::Capture reversed =
        rb0::collect(
            reversed_tasks, parent, parallel_config);
    report.artifact_unchanged_after_reverse =
        rb0::require_artifact_snapshot(artifact_path) ==
        report.artifact_before;

    const rb0::Capture single_worker =
        rb0::collect(
            tasks, parent,
            {
                .max_game_turns = kMaximumGameTurns,
                .worker_count = 1,
                .verify_hidden_repartition = true,
                .schedule_seed = kEngineeringSeed,
                .schedule_generation = kGeneration,
            });
    report.artifact_unchanged_after_single_worker =
        rb0::require_artifact_snapshot(artifact_path) ==
        report.artifact_before;
    report.artifact_after =
        rb0::require_artifact_snapshot(artifact_path);
    report.artifact_unchanged_final =
        report.artifact_after == report.artifact_before;

    const std::array<const rb0::Capture*, kCaptureCount>
        captures = {
            &canonical,
            &repeated,
            &reversed,
            &single_worker,
        };
    for (std::size_t index = 0;
         index < captures.size(); ++index) {
        report.captures[index] =
            inspect_capture(
                *captures[index], kEngineeringSeed,
                kGeneration, kBalancedBlocks);
    }
    report.repeated_capture_bit_identical =
        canonical == repeated;
    report.reversed_capture_bit_identical =
        canonical == reversed;
    report.worker_capture_bit_identical =
        canonical == single_worker;
    progress << " done ("
             << canonical.records.size()
             << " trace-perspective rows per capture)\n";
    return report;
}

void write_report(const Report& report, std::ostream& output) {
    output.imbue(std::locale::classic());
    output << std::setprecision(
        std::numeric_limits<double>::max_digits10);
    output
        << "RB0-E1 full-scale mechanical preflight\n"
        << "configuration\tseed\t" << report.seed << '\n'
        << "configuration\tgeneration\t"
        << report.generation << '\n'
        << "configuration\tbalanced_blocks\t"
        << report.balanced_blocks << '\n'
        << "artifact\tpath\t"
        << report.artifact_before.path << '\n'
        << "artifact\tsize\t"
        << report.artifact_before.size << '\n'
        << "artifact\tmodification_time_ticks\t"
        << report.artifact_before.modification_time_ticks
        << '\n'
        << "artifact\tcontent_digest\t"
        << report.artifact_before.content_hash << '\n'
        << "artifact\tparent_fingerprint\t"
        << report.parent_fingerprint << '\n';

    for (std::size_t index = 0;
         index < report.captures.size(); ++index) {
        write_capture(
            output, kCaptureNames[index],
            report.captures[index]);
    }
    for (const NamedInvariant& invariant :
         named_invariants(report)) {
        write_boolean(
            output, invariant.name, invariant.passed);
    }
    output << "mechanical\tcomplete\t"
           << (mechanically_clean(report) ? "PASS" : "FAIL")
           << '\n';
}

} // namespace old_school::rb0_mechanical_preflight
