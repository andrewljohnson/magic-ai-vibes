#include "old_school/fq4_d1_field_gate.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_information_set.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace gate = old_school::fq4_d1_field_gate;
namespace dominance = old_school::fq0_dominance;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

gate::ReplayRootManifest manifest(
    std::size_t ordinal = 3) {
    gate::ReplayRootManifest result{
        .locator = {
            .source_block = 0,
            .source_seed_base = 790,
            .schedule_index = 2,
            .game_seed = 1234567,
            .owner_seat = 0,
            .trace_ordinal = ordinal,
        },
        .owner_deck = old_school::DeckId::Green,
        .opponent_deck = old_school::DeckId::Red,
        .information_action_fingerprint =
            std::string(64, 'a'),
        .canonical_descriptors = {
            "cast",
            "pass",
        },
        .pass_index = 1,
    };
    result.stable_id =
        gate::stable_root_id(
            result.locator,
            result.information_action_fingerprint);
    return result;
}

gate::ParentClassInput class_input(
    double dominated_combined,
    double safe_combined,
    const std::array<double, gate::kDominanceWorlds>&
        differences) {
    gate::ParentClassInput input{
        .canonical_descriptors = {
            "dominated",
            "pass",
        },
        .base_scores = {
            dominated_combined,
            safe_combined,
        },
        .combined_scores = {
            dominated_combined,
            safe_combined,
        },
        .base_samples = {
            {},
            std::vector<double>(
                gate::kDominanceWorlds, 0.5),
        },
        .robustly_pass_dominated = {
            true,
            false,
        },
    };
    for (const double difference : differences) {
        input.base_samples[0].push_back(
            0.5 + difference);
    }
    return input;
}

void test_schedule_is_frozen_and_balanced() {
    const auto first = gate::source_schedule();
    const auto second = gate::source_schedule();
    expect(first == second,
           "source schedule is not deterministic");
    const std::string bytes =
        gate::serialize_source_schedule(first);
    expect(bytes.size() == gate::kExpectedScheduleBytes,
           "frozen source schedule byte count drifted");
    expect(
        old_school::artifact_integrity::sha256_string(bytes) ==
            gate::kExpectedScheduleSha256,
        "frozen source schedule SHA-256 drifted");
    expect(
        gate::source_schedule_sha256() ==
            gate::kExpectedScheduleSha256,
        "source schedule helper returned wrong digest");
    const auto balance =
        gate::audit_schedule_balance(first);
    expect(
        balance.exact &&
            balance.physical_games ==
                gate::kExpectedPhysicalGames &&
            balance.owner_perspectives ==
                gate::kExpectedOwnerPerspectives,
        "source schedule failed physical-game balance");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect(
            balance.perspectives_by_deck[deck] ==
                    gate::kExpectedPerspectivesPerDeck &&
                balance.seat_zero_by_deck[deck] ==
                    gate::
                        kExpectedSeatZeroPerspectivesPerDeck &&
                balance.on_play_by_deck[deck] ==
                    gate::
                        kExpectedOnPlayPerspectivesPerDeck,
            "source schedule failed all-five seat/play balance");
    }
}

void test_retention_is_earliest_deduped_and_chronological() {
    std::vector<gate::RetentionCandidate> candidates;
    for (std::size_t index = 0; index < 20; ++index) {
        candidates.push_back({
            .trace_ordinal = index * 2,
            .information_action_fingerprint =
                "hash-" + std::to_string(index),
            .information_action_bytes =
                "bytes-" + std::to_string(index),
            .stable_id =
                "root-" + std::to_string(index),
        });
    }
    candidates.insert(
        candidates.begin() + 3,
        {
            .trace_ordinal = 5,
            .information_action_fingerprint = "hash-1",
            .information_action_bytes = "bytes-1",
            .stable_id = "root-duplicate",
        });
    const auto retained =
        gate::retain_owner_game_roots(candidates);
    expect(retained.valid &&
               retained.duplicate_count == 1 &&
               retained.hash_collision_count == 0 &&
               retained.unique_input_indices.size() == 20 &&
               retained.retained_input_indices.size() ==
                   gate::kMaximumRootsPerOwnerGame,
           "retention census or earliest dedupe failed");
    expect(
        retained.unique_input_indices[1] == 1 &&
            std::find(
                retained.unique_input_indices.begin(),
                retained.unique_input_indices.end(),
                3) ==
                retained.unique_input_indices.end(),
        "retention did not preserve earliest duplicate");
    expect(
        std::is_sorted(
            retained.retained_input_indices.begin(),
            retained.retained_input_indices.end()),
        "retained roots lost chronological order");

    auto collision = candidates;
    collision[3].information_action_bytes =
        "different-full-owner-observation";
    const auto rejected =
        gate::retain_owner_game_roots(collision);
    expect(!rejected.valid &&
               rejected.hash_collision_count == 1,
           "retention accepted a digest collision");
}

void test_replay_manifest_is_bound_and_mutation_sensitive() {
    const std::vector<gate::ReplayRootManifest> roots{
        manifest(3),
        manifest(4),
    };
    auto distinct = roots;
    distinct[1].information_action_fingerprint =
        std::string(64, 'b');
    distinct[1].stable_id =
        gate::stable_root_id(
            distinct[1].locator,
            distinct[1]
                .information_action_fingerprint);
    expect(gate::validate_replay_manifest(distinct),
           "valid replay manifest was rejected");
    const auto& bound = distinct.front();
    const std::string expected_preimage =
        "old-school-fq4-d1-p0-stable-root-v1\n"
        "source_seed_base_index=0\n"
        "source_seed_base=790\n"
        "schedule_index=2\n"
        "game_seed=1234567\n"
        "owner=0\n"
        "trace=3\n"
        "information_action_sha256=" +
        std::string(64, 'a') + "\n";
    expect(
        bound.stable_id ==
            old_school::artifact_integrity::
                sha256_string(expected_preimage),
        "stable-root final-LF serialization drifted");
    const std::string first =
        gate::replay_manifest_sha256(distinct);
    expect(
        first ==
            gate::replay_manifest_sha256(distinct),
        "replay manifest digest is nondeterministic");
    auto changed = distinct;
    changed[1].canonical_descriptors[0] = "changed";
    expect(
        gate::replay_manifest_sha256(changed) != first,
        "replay digest ignored action mutation");
    auto duplicate = distinct;
    duplicate[1] = duplicate[0];
    expect(!gate::validate_replay_manifest(duplicate),
           "replay manifest accepted duplicate stable IDs");
}

void test_dominance_is_common_world_and_fail_closed() {
    std::vector<gate::DominanceWorldRow> worlds(
        gate::kDominanceWorlds);
    for (std::size_t world = 0;
         world < worlds.size(); ++world) {
        worlds[world] = {
            .pass_complete = true,
            .candidate_complete = {
                true,
                true,
                world != worlds.size() - 1,
            },
            .orientations = {
                dominance::Orientation::Incomparable,
                dominance::Orientation::
                    FirstDominatesSecond,
                dominance::Orientation::
                    FirstDominatesSecond,
            },
        };
    }
    const auto result =
        gate::summarize_robust_dominance(
            0, 3, worlds);
    expect(
        result.shape_valid &&
            result.transition_count ==
                gate::kDominanceWorlds * 3 &&
            result.complete_comparisons == 15 &&
            result.strict_world_counts[1] ==
                gate::kDominanceWorlds &&
            result.strict_world_counts[2] ==
                gate::kDominanceWorlds - 1 &&
            result.robustly_pass_dominated[1] &&
            !result.robustly_pass_dominated[2],
        "robust dominance did not fail closed on incomplete row");
    worlds.pop_back();
    expect(
        !gate::summarize_robust_dominance(
             0, 3, worlds)
             .shape_valid,
        "dominance accepted wrong world count");
}

void test_parent_classes_are_exact_and_exclusive() {
    const std::array<double, gate::kDominanceWorlds>
        constant{
            0.1, 0.1, 0.1, 0.1,
            0.1, 0.1, 0.1, 0.1,
        };
    const auto class1 =
        gate::classify_parent(
            class_input(0.6, 0.5, constant));
    expect(
        class1.valid &&
            class1.classification ==
                gate::ParentClass::Class1 &&
            class1.paired_standard_error == 0.0,
        "exact-zero SE did not produce Class 1");

    const std::array<double, gate::kDominanceWorlds>
        varied{
            0.1, 0.2, 0.1, 0.2,
            0.1, 0.2, 0.1, 0.2,
        };
    const auto class2 =
        gate::classify_parent(
            class_input(0.6, 0.5, varied));
    expect(
        class2.valid &&
            class2.classification ==
                gate::ParentClass::Class2 &&
            class2.paired_standard_error > 0.0 &&
            class2.sigma >= 3.0,
        "three-sigma positive margin did not produce Class 2");

    const auto class3 =
        gate::classify_parent(
            class_input(0.51, 0.5, varied));
    expect(
        class3.valid &&
            class3.classification ==
                gate::ParentClass::Class3 &&
            class3.sigma < 3.0,
        "low-sigma positive margin did not produce Class 3");

    const auto tie =
        gate::classify_parent(
            class_input(0.5, 0.5, varied));
    expect(
        tie.valid &&
            tie.classification ==
                gate::ParentClass::Class3,
        "exact tie did not produce fallback Class 3");

    const auto safe =
        gate::classify_parent(
            class_input(0.4, 0.5, varied));
    expect(
        safe.valid &&
            safe.classification ==
                gate::ParentClass::Safe,
        "negative margin did not produce Safe");

    auto nonfinite =
        class_input(0.6, 0.5, constant);
    nonfinite.base_samples[0][0] =
        std::numeric_limits<double>::infinity();
    expect(
        !gate::classify_parent(nonfinite).valid,
        "nonfinite base sample produced a parent class");
}

void test_count_and_score_accounting_fail_closed() {
    gate::RootCounts counts{
        .raw = 10,
        .trivial = 2,
        .nontrivial = 7,
        .malformed = 1,
        .over_cap = 1,
        .eligible = 6,
        .duplicate = 1,
        .unique = 5,
        .retained = 4,
        .cap_dropped = 1,
        .dominance_positive = 3,
        .parent_classes = {1, 1, 1, 0},
    };
    expect(counts.terminal_cross_sums_valid(),
           "valid root count equations failed");
    ++counts.duplicate;
    expect(!counts.terminal_cross_sums_valid(),
           "root count equations accepted overlap");

    gate::ProductionAccounting accounting{
        .score_calls = 2,
        .scored_actions = 5,
        .sampled_worlds =
            2 * gate::kDominanceWorlds,
        .rollout_evaluations =
            5 * gate::kDominanceWorlds,
        .terminal_evaluations = 7,
        .bootstrapped_evaluations =
            5 * gate::kDominanceWorlds - 7,
        .dominance_transitions = 123,
    };
    expect(accounting.valid(),
           "valid production accounting failed");
    ++accounting.bootstrapped_evaluations;
    expect(!accounting.valid(),
           "production accounting accepted an extra bootstrap");
}

void test_report_exit_classifier_is_conjunctive() {
    auto report =
        gate::testing::complete_synthetic_report();
    expect(
        report.infrastructure_valid() &&
            report.support_floor_met() &&
            gate::classify_exit(report) ==
                gate::ExitClassification::Pass,
        "complete synthetic P0 report did not pass");

    auto underpowered = report;
    underpowered.pooled.parent_classes[1] =
        gate::kMinimumHighConfidenceRoots - 1;
    underpowered.pooled.dominance_positive =
        gate::kMinimumHighConfidenceRoots - 1;
    expect(
        gate::classify_exit(underpowered) ==
            gate::ExitClassification::Underpowered,
        "honest support miss was not underpowered");

    auto infrastructure = report;
    infrastructure.all_replays_exact = false;
    expect(
        gate::classify_exit(infrastructure) ==
            gate::ExitClassification::
                InfrastructureFailure,
        "replay failure did not classify as infrastructure");

    auto missing_hash = report;
    missing_hash.dominance_corpus_sha256.clear();
    expect(
        gate::classify_exit(missing_hash) ==
            gate::ExitClassification::
                InfrastructureFailure,
        "missing dominance digest passed report gate");

    auto wrong_schedule = report;
    wrong_schedule.schedule_sha256 =
        std::string(64, '0');
    expect(
        gate::classify_exit(wrong_schedule) ==
            gate::ExitClassification::
                InfrastructureFailure,
        "wrong schedule digest passed report gate");

    auto nonfinite_mass = report;
    nonfinite_mass.class2_sigma_mass =
        std::numeric_limits<double>::infinity();
    expect(
        gate::classify_exit(nonfinite_mass) ==
            gate::ExitClassification::
                InfrastructureFailure,
        "nonfinite Class-2 sigma mass passed report gate");
}

void test_canonical_hidden_root_preserves_owner_boundary() {
    const auto corpus =
        old_school::probes::make_probe_dev_v3();
    const auto found = std::find_if(
        corpus.begin(), corpus.end(),
        [](const old_school::probes::DecisionProbe& probe) {
            return probe.decision_kind ==
                       old_school::probes::DecisionKind::
                           Priority &&
                   probe.candidates.size() >= 2;
        });
    expect(found != corpus.end(),
           "portable corpus has no Priority root");
    const auto* selected =
        std::get_if<old_school::PriorityAction>(
            &found->candidates.front().action);
    expect(selected != nullptr,
           "portable Priority probe has wrong candidate type");
    const old_school::LearnedDecisionTracePoint point{
        .state = found->state,
        .context = {
            .valid = true,
            .phase = found->phase,
            .decision_player = found->root_player,
            .consecutive_passes =
                found->consecutive_passes,
            .sorcery_actions =
                found->phase ==
                    old_school::TurnPhase::FirstMain ||
                found->phase ==
                    old_school::TurnPhase::SecondMain,
        },
        .selected_priority_action = *selected,
    };
    gate::SourceGame source{
        .source_block = 0,
        .source_seed_base = 123,
        .schedule_index = 0,
        .pairing_index = 0,
        .seat_decks = {
            found->root_player == 0
                ? found->root_deck
                : found->opponent_deck,
            found->root_player == 1
                ? found->root_deck
                : found->opponent_deck,
        },
        .starting_player = found->state.starting_player,
        .game_seed = 456,
    };
    const auto diagnostic =
        gate::testing::diagnose_canonical_hidden_root(
            point, source, found->root_player, 7);
    const auto repeated =
        gate::testing::diagnose_canonical_hidden_root(
            point, source, found->root_player, 7);
    old_school::GameState expected_hidden =
        diagnostic.canonical_state;
    auto& opponent =
        expected_hidden.players[1 - found->root_player];
    auto& owner =
        expected_hidden.players[found->root_player];
    bool swapped = false;
    for (std::size_t hand = 0;
         hand < opponent.hand.size() && !swapped; ++hand) {
        for (std::size_t library = 0;
             library < opponent.library.size();
             ++library) {
            if (opponent.hand[hand] !=
                opponent.library[library]) {
                std::swap(
                    opponent.hand[hand],
                    opponent.library[library]);
                swapped = true;
                break;
            }
        }
    }
    const auto swap_first_unequal =
        [&swapped](std::vector<old_school::CardId>& cards) {
            if (swapped) {
                return;
            }
            for (std::size_t first = 0;
                 first < cards.size(); ++first) {
                for (std::size_t second = first + 1;
                     second < cards.size(); ++second) {
                    if (cards[first] != cards[second]) {
                        std::swap(
                            cards[first], cards[second]);
                        swapped = true;
                        return;
                    }
                }
            }
        };
    swap_first_unequal(opponent.library);
    swap_first_unequal(owner.library);
    swap_first_unequal(opponent.hand);
    expect(
        diagnostic == repeated &&
            diagnostic.materialized &&
            diagnostic.owner_hand_preserved &&
            diagnostic.reporting_statistics_zero &&
            diagnostic.second_replay_exact &&
            diagnostic.hidden_feature_bits_identical &&
            diagnostic.hidden_clone_state ==
                expected_hidden &&
            diagnostic.hidden_clone_distinct ==
                diagnostic.hidden_clone_eligible &&
            diagnostic.hidden_clone_distinct == swapped &&
            !diagnostic
                 .information_action_fingerprint.empty(),
        "canonical hidden-root boundary failed");
}

void test_singleton_pass_root_is_trivial() {
    const auto corpus =
        old_school::probes::make_probe_dev_v3();
    const auto found = std::find_if(
        corpus.begin(), corpus.end(),
        [](const old_school::probes::DecisionProbe& probe) {
            return probe.decision_kind ==
                       old_school::probes::DecisionKind::
                           Priority &&
                   probe.state.stack.empty();
        });
    expect(found != corpus.end(),
           "portable corpus has no stack-empty Priority root");

    old_school::GameState state = found->state;
    auto& owner = state.players[found->root_player];
    owner.library.insert(
        owner.library.end(),
        owner.hand.begin(), owner.hand.end());
    owner.hand.clear();
    owner.land_played_this_turn = true;
    for (auto& artifact : owner.artifacts) {
        artifact.tapped = true;
    }
    const bool sorcery_actions =
        found->phase == old_school::TurnPhase::FirstMain ||
        found->phase == old_school::TurnPhase::SecondMain;
    const auto legal =
        old_school::legal_priority_actions(
            state, found->root_player,
            sorcery_actions);
    expect(
        legal.size() == 1 &&
            legal.front().kind ==
                old_school::PriorityActionKind::Pass,
        "portable singleton fixture is not exactly Pass");

    const gate::SourceGame source{
        .source_block = 0,
        .source_seed_base = 123,
        .schedule_index = 0,
        .pairing_index = 0,
        .seat_decks = {
            found->root_player == 0
                ? found->root_deck
                : found->opponent_deck,
            found->root_player == 1
                ? found->root_deck
                : found->opponent_deck,
        },
        .starting_player = state.starting_player,
        .game_seed = 456,
    };
    old_school::LearnedDecisionTracePoint point{
        .state = std::move(state),
        .context = {
            .valid = true,
            .phase = found->phase,
            .decision_player = found->root_player,
            .consecutive_passes =
                found->consecutive_passes,
            .sorcery_actions = sorcery_actions,
        },
        .selected_priority_action =
            old_school::PriorityAction::pass(),
    };
    bool fq0_rejected_singleton = false;
    try {
        static_cast<void>(
            old_school::fq0_information_set::
                make_information_set_key(
                    point.state, point.context, legal));
    } catch (const std::invalid_argument&) {
        fq0_rejected_singleton = true;
    }
    expect(
        fq0_rejected_singleton,
        "FQ0 singleton-key contract was weakened");
    expect(
        gate::testing::diagnose_root_disposition(
            point, source, found->root_player, 9) ==
            gate::testing::RootDisposition::Trivial,
        "singleton Pass root was not classified trivial");
    expect(
        !gate::testing::diagnose_canonical_hidden_root(
             point, source, found->root_player, 9)
             .materialized,
        "singleton Pass root materialized a retention candidate");

    point.selected_priority_action.reset();
    expect(
        gate::testing::diagnose_root_disposition(
            point, source, found->root_player, 9) ==
            gate::testing::RootDisposition::Malformed,
        "missing singleton selected action was not malformed");
}

} // namespace

int main() {
    const std::vector<
        std::pair<std::string_view, std::function<void()>>>
        tests{
            {
                "frozen schedule and balance",
                test_schedule_is_frozen_and_balanced,
            },
            {
                "retention dedupe and chronology",
                test_retention_is_earliest_deduped_and_chronological,
            },
            {
                "replay manifest binding",
                test_replay_manifest_is_bound_and_mutation_sensitive,
            },
            {
                "dominance fail-closed accounting",
                test_dominance_is_common_world_and_fail_closed,
            },
            {
                "exclusive parent classes",
                test_parent_classes_are_exact_and_exclusive,
            },
            {
                "count and score accounting",
                test_count_and_score_accounting_fail_closed,
            },
            {
                "report classifier",
                test_report_exit_classifier_is_conjunctive,
            },
            {
                "canonical hidden root",
                test_canonical_hidden_root_preserves_owner_boundary,
            },
            {
                "singleton Pass is trivial",
                test_singleton_pass_root_is_trivial,
            },
        };
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
        } catch (const std::exception& error) {
            std::cerr
                << "FAIL: " << name << ": "
                << error.what() << '\n';
        }
    }
    std::cout
        << passed << "/" << tests.size()
        << " FQ4-D1-P0 field-gate tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
