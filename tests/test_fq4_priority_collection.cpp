#include "old_school/fq4_priority_collection.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_dominance.hpp"
#include "old_school/learned_iteration.hpp"
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
#include <variant>
#include <vector>

namespace {

namespace collection =
    old_school::fq4_priority_collection;
namespace dominance = old_school::fq0_dominance;

constexpr std::string_view kOwnerSchema =
    "synthetic-fq4-owner-information-action-v1";
constexpr std::string_view kStableSchema =
    "synthetic-fq4-stable-root-v1";
constexpr std::string_view kManifestSchema =
    "synthetic-fq4-manifest-v1";

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

collection::CollectionSpec spec() {
    return {
        .owner_information_schema = kOwnerSchema,
        .stable_root_schema = kStableSchema,
        .hidden_seed_namespace = 0x1111222233334444ULL,
        .hidden_seed_scope = "synthetic-fq4-hidden-v1",
        .dominance_seed_namespace =
            0x5555666677778888ULL,
        .dominance_seed_scope =
            "synthetic-fq4-dominance-v1",
        .maximum_legal_actions =
            collection::kMaximumLegalActions,
        .maximum_roots_per_owner_game =
            collection::kMaximumRootsPerOwnerGame,
        .dominance_worlds =
            collection::kDominanceWorlds,
    };
}

collection::ReplayRootManifest manifest(
    std::size_t ordinal = 3,
    char fingerprint_byte = 'a') {
    collection::ReplayRootManifest result{
        .locator = {
            .source_block = 0,
            .source_seed_base = 9001,
            .schedule_index = 2,
            .game_seed = 1234567,
            .owner_seat = 0,
            .trace_ordinal = ordinal,
        },
        .owner_deck = old_school::DeckId::Green,
        .opponent_deck = old_school::DeckId::Red,
        .information_action_fingerprint =
            std::string(64, fingerprint_byte),
        .canonical_descriptors = {
            "cast",
            "pass",
        },
        .pass_index = 1,
    };
    result.stable_id = collection::stable_root_id(
        result.locator,
        result.information_action_fingerprint,
        kStableSchema);
    return result;
}

collection::ParentClassInput class_input(
    double dominated_combined, double safe_combined,
    const std::array<double,
                     collection::kDominanceWorlds>&
        differences) {
    collection::ParentClassInput input{
        .canonical_descriptors = {
            "dominated",
            "safe",
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
                collection::kDominanceWorlds, 0.5),
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

struct PriorityFixture {
    old_school::LearnedDecisionTracePoint point;
    collection::SourceGame source;
    std::size_t owner = 0;
};

PriorityFixture priority_fixture() {
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
    if (found == corpus.end()) {
        throw std::runtime_error(
            "portable corpus has no nontrivial Priority root");
    }
    const auto* selected =
        std::get_if<old_school::PriorityAction>(
            &found->candidates.front().action);
    if (selected == nullptr) {
        throw std::runtime_error(
            "portable Priority candidate has wrong type");
    }
    const std::size_t owner = found->root_player;
    return {
        .point = {
            .state = found->state,
            .context = {
                .valid = true,
                .phase = found->phase,
                .decision_player = owner,
                .consecutive_passes =
                    found->consecutive_passes,
                .sorcery_actions =
                    found->phase ==
                        old_school::TurnPhase::FirstMain ||
                    found->phase ==
                        old_school::TurnPhase::SecondMain,
            },
            .selected_priority_action = *selected,
        },
        .source = {
            .source_block = 0,
            .source_seed_base = 9001,
            .schedule_index = 0,
            .pairing_index = 0,
            .seat_decks = {
                owner == 0
                    ? found->root_deck
                    : found->opponent_deck,
                owner == 1
                    ? found->root_deck
                    : found->opponent_deck,
            },
            .starting_player =
                found->state.starting_player,
            .game_seed = 777,
        },
        .owner = owner,
    };
}

void test_domains_and_ids_are_seed_agnostic() {
    const auto valid = spec();
    expect(valid.valid(),
           "synthetic collection spec was rejected");
    auto invalid = valid;
    invalid.hidden_seed_namespace = 0;
    expect(!invalid.valid(),
           "zero hidden namespace was accepted");

    const collection::RootLocator locator{
        .source_block = 2,
        .source_seed_base = 100,
        .schedule_index = 7,
        .game_seed = 123,
        .owner_seat = 1,
        .trace_ordinal = 9,
    };
    const std::string fingerprint(64, 'f');
    const std::string first = collection::stable_root_id(
        locator, fingerprint, kStableSchema);
    expect(
        first == collection::stable_root_id(
                     locator, fingerprint, kStableSchema),
        "stable root ID was nondeterministic");
    expect(
        first != collection::stable_root_id(
                     locator, fingerprint,
                     "another-stable-domain"),
        "stable root ID ignored its caller-owned domain");
    expect(
        collection::physical_game_id(locator) ==
            "source_seed_base=100\nschedule_index=7\n",
        "physical game ID serialization drifted");
}

void test_retention_is_deterministic_and_fail_closed() {
    std::vector<collection::RetentionCandidate> candidates;
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
    const auto first =
        collection::retain_owner_game_roots(candidates);
    const auto repeated =
        collection::retain_owner_game_roots(candidates);
    expect(
        first == repeated && first.valid &&
            first.duplicate_count == 1 &&
            first.unique_input_indices.size() == 20 &&
            first.retained_input_indices.size() ==
                collection::kMaximumRootsPerOwnerGame &&
            std::is_sorted(
                first.retained_input_indices.begin(),
                first.retained_input_indices.end()) &&
            std::find(
                first.unique_input_indices.begin(),
                first.unique_input_indices.end(), 3) ==
                first.unique_input_indices.end(),
        "retention was not deterministic earliest-dedupe");

    auto collision = candidates;
    collision[3].information_action_bytes =
        "different-owner-information";
    const auto rejected_collision =
        collection::retain_owner_game_roots(collision);
    expect(
        !rejected_collision.valid &&
            rejected_collision.hash_collision_count == 1,
        "retention accepted a fingerprint collision");

    auto descending = candidates;
    descending[4].trace_ordinal =
        descending[3].trace_ordinal;
    expect(
        !collection::retain_owner_game_roots(descending)
             .valid,
        "retention accepted non-increasing ordinals");
    auto duplicate_id = candidates;
    duplicate_id[4].stable_id =
        duplicate_id[3].stable_id;
    expect(
        !collection::retain_owner_game_roots(duplicate_id)
             .valid,
        "retention accepted duplicate stable IDs");
    expect(
        !collection::retain_owner_game_roots(candidates, 0)
             .valid,
        "retention accepted a zero cap");
}

void test_manifest_is_bound_and_mutation_sensitive() {
    std::vector<collection::ReplayRootManifest> roots{
        manifest(3, 'a'),
        manifest(4, 'b'),
    };
    expect(
        collection::validate_replay_manifest(
            roots, kStableSchema),
        "valid replay manifest was rejected");
    const std::string bytes =
        collection::serialize_replay_manifest(
            roots, kManifestSchema, kStableSchema);
    const std::string digest =
        collection::replay_manifest_sha256(
            roots, kManifestSchema, kStableSchema);
    expect(
        digest ==
                old_school::artifact_integrity::
                    sha256_string(bytes) &&
            digest ==
                collection::replay_manifest_sha256(
                    roots, kManifestSchema,
                    kStableSchema),
        "manifest digest was nondeterministic or unbound");
    expect(
        !collection::validate_replay_manifest(
            roots, "wrong-stable-domain"),
        "manifest accepted the wrong stable-ID domain");

    auto duplicate = roots;
    duplicate[1] = duplicate[0];
    expect(
        !collection::validate_replay_manifest(
            duplicate, kStableSchema),
        "manifest accepted duplicate stable IDs");
    auto unsorted = roots;
    unsorted[0].canonical_descriptors = {
        "pass",
        "cast",
    };
    expect(
        !collection::validate_replay_manifest(
            unsorted, kStableSchema),
        "manifest accepted unsorted descriptors");
    auto mutated = roots;
    mutated[1].canonical_descriptors[0] = "changed";
    expect(
        collection::replay_manifest_sha256(
            mutated, kManifestSchema, kStableSchema) !=
            digest,
        "manifest digest ignored an action mutation");
}

void test_robust_dominance_is_complete_and_fail_closed() {
    std::vector<collection::DominanceWorldRow> worlds(
        collection::kDominanceWorlds);
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
        collection::summarize_robust_dominance(
            0, 3, worlds);
    expect(
        result.shape_valid &&
            result.transition_count ==
                collection::kDominanceWorlds * 3 &&
            result.complete_comparisons == 15 &&
            result.complete_world_counts[1] ==
                collection::kDominanceWorlds &&
            result.complete_world_counts[2] ==
                collection::kDominanceWorlds - 1 &&
            result.strict_world_counts[1] ==
                collection::kDominanceWorlds &&
            result.strict_world_counts[2] ==
                collection::kDominanceWorlds - 1 &&
            result.robustly_pass_dominated[1] &&
            !result.robustly_pass_dominated[2] &&
            result.any_dominated(),
        "robust dominance did not fail closed by world");

    worlds.pop_back();
    expect(
        !collection::summarize_robust_dominance(
             0, 3, worlds)
             .shape_valid,
        "dominance accepted the wrong world count");
    worlds.push_back({});
    expect(
        !collection::summarize_robust_dominance(
             0, 3, worlds)
             .shape_valid,
        "dominance accepted a malformed world shape");
}

void test_full_dominance_evaluator_is_deterministic() {
    const PriorityFixture fixture = priority_fixture();
    const collection::CollectionSpec synthetic = spec();
    expect(
        fixture.source.source_seed_base != 790 &&
            fixture.source.source_seed_base != 791 &&
            fixture.source.source_seed_base !=
                202607280210ULL &&
            fixture.source.source_seed_base !=
                202607280211ULL,
        "synthetic dominance test used a forbidden source seed");
    const auto built =
        collection::build_canonical_root(
            fixture.point, fixture.source,
            fixture.owner, 7, synthetic);
    expect(
        built.disposition ==
                collection::RootDisposition::
                    RetentionCandidate &&
            built.root.has_value(),
        "synthetic Priority root did not materialize");

    std::vector<std::string> first_failures;
    std::vector<std::string> repeated_failures;
    const auto first =
        collection::evaluate_robust_dominance(
            *built.root, synthetic,
            first_failures);
    const auto repeated =
        collection::evaluate_robust_dominance(
            *built.root, synthetic,
            repeated_failures);
    const std::size_t action_count =
        built.root->manifest
            .canonical_descriptors.size();
    expect(
        first == repeated &&
            first_failures == repeated_failures &&
            first_failures.empty() &&
            first.shape_valid &&
            first.transition_count ==
                synthetic.dominance_worlds *
                    action_count &&
            first.complete_world_counts.size() ==
                action_count &&
            first.strict_world_counts.size() ==
                action_count &&
            first.robustly_pass_dominated.size() ==
                action_count,
        "full dominance evaluator was not deterministic and complete");
    for (std::size_t action = 0;
         action < action_count; ++action) {
        expect(
            first.strict_world_counts[action] <=
                    first.complete_world_counts[action] &&
                first.complete_world_counts[action] <=
                    synthetic.dominance_worlds &&
                first.robustly_pass_dominated[action] ==
                    (action != first.pass_index &&
                     first.strict_world_counts[action] ==
                         synthetic.dominance_worlds),
            "full dominance evaluator produced inconsistent counts");
    }
}

void test_parent_classification_uses_paired_worlds() {
    const std::array<double,
                     collection::kDominanceWorlds>
        constant{
            0.1, 0.1, 0.1, 0.1,
            0.1, 0.1, 0.1, 0.1,
        };
    const auto class1 = collection::classify_parent(
        class_input(0.6, 0.5, constant));
    expect(
        class1.valid &&
            class1.classification ==
                collection::ParentClass::Class1 &&
            class1.paired_standard_error == 0.0 &&
            class1.high_confidence_unsafe(),
        "zero-SE positive margin did not produce Class 1");

    const std::array<double,
                     collection::kDominanceWorlds>
        varied{
            0.1, 0.2, 0.1, 0.2,
            0.1, 0.2, 0.1, 0.2,
        };
    const auto class2 = collection::classify_parent(
        class_input(0.6, 0.5, varied));
    const auto class3 = collection::classify_parent(
        class_input(0.51, 0.5, varied));
    const auto safe = collection::classify_parent(
        class_input(0.4, 0.5, varied));
    expect(
        class2.valid &&
            class2.classification ==
                collection::ParentClass::Class2 &&
            class2.paired_standard_error > 0.0 &&
            class2.sigma >= 3.0 &&
            class3.valid &&
            class3.classification ==
                collection::ParentClass::Class3 &&
            safe.valid &&
            safe.classification ==
                collection::ParentClass::Safe,
        "paired class boundaries drifted");

    auto selected = class_input(0.6, 0.5, constant);
    selected.canonical_descriptors = {
        "dominated-a",
        "dominated-b",
        "safe",
    };
    selected.base_scores = {0.6, 0.7, 0.5};
    selected.combined_scores = {0.6, 0.7, 0.5};
    selected.base_samples = {
        std::vector<double>(
            collection::kDominanceWorlds, 0.6),
        std::vector<double>(
            collection::kDominanceWorlds, 0.7),
        std::vector<double>(
            collection::kDominanceWorlds, 0.5),
    };
    selected.robustly_pass_dominated = {
        true,
        true,
        false,
    };
    const auto selected_result =
        collection::classify_parent(selected);
    expect(
        selected_result.valid &&
            selected_result.best_dominated_index == 1 &&
            selected_result.best_nondominated_index == 2,
        "classification ignored candidate-neutral argmax");

    selected.base_samples[0][0] =
        std::numeric_limits<double>::infinity();
    expect(
        !collection::classify_parent(selected).valid,
        "classification accepted a nonfinite sample");
}

void test_accounting_cross_sums_fail_closed() {
    collection::RootCounts counts{
        .raw = 10,
        .nontrivial = 7,
        .malformed = 1,
        .trivial = 2,
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
           "valid root census cross-sums failed");
    ++counts.duplicate;
    expect(!counts.terminal_cross_sums_valid(),
           "root census accepted overlapping terminals");

    collection::ProductionAccounting accounting{
        .score_calls = 2,
        .scored_actions = 5,
        .sampled_worlds =
            2 * collection::kDominanceWorlds,
        .rollout_evaluations =
            5 * collection::kDominanceWorlds,
        .terminal_evaluations = 7,
        .bootstrapped_evaluations =
            5 * collection::kDominanceWorlds - 7,
        .dominance_transitions = 123,
    };
    expect(accounting.valid(),
           "valid production accounting failed");
    ++accounting.bootstrapped_evaluations;
    expect(!accounting.valid(),
           "accounting accepted an extra bootstrap");
}

void test_blind_selection_is_balanced_and_deterministic() {
    std::vector<collection::BlindSelectionInput> input;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t row = 0; row < 25; ++row) {
            input.push_back({
                .stable_id =
                    "deck-" + std::to_string(deck) +
                    "-root-" + std::to_string(row),
                .owner_deck =
                    static_cast<old_school::DeckId>(deck),
                .dominance_positive =
                    row != 0 || deck != 0,
            });
        }
    }
    const auto first =
        collection::select_development_rows(input);
    const auto repeated =
        collection::select_development_rows(input);
    std::vector<collection::BlindSelectionRow>
        exact_expected;
    const auto retained_positive_positions =
        old_school::learned_iteration::
            evenly_spaced_retained_indices(24, 15);
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        exact_expected.push_back({
            .input_index = deck * 25,
            .roles = static_cast<std::uint8_t>(
                collection::DevelopmentRoleBackground |
                (deck == 0
                     ? collection::DevelopmentRoleNone
                     : collection::
                           DevelopmentRolePositive)),
        });
        for (const std::size_t position :
             retained_positive_positions) {
            exact_expected.push_back({
                .input_index =
                    deck * 25 + 1 + position,
                .roles =
                    collection::DevelopmentRolePositive,
            });
        }
    }
    expect(
        first == repeated && first.valid &&
            first.rows.size() ==
                old_school::kDeckCount * 16,
        "blind selection was not deterministic and bounded");
    expect(
        first.rows == exact_expected,
        "blind selection did not use the exact reserved-background "
        "plus evenly-spaced-15 rule");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect(
            first.rows_by_deck[deck] == 16 &&
                first.positives_by_deck[deck] ==
                    (deck == 0 ? 15 : 16),
            "blind selection deck counts drifted");
        const auto found = std::find_if(
            first.rows.begin(), first.rows.end(),
            [deck](const collection::BlindSelectionRow& row) {
                return row.input_index == deck * 25;
            });
        expect(
            found != first.rows.end() &&
                (found->roles &
                 collection::DevelopmentRoleBackground) != 0 &&
                ((found->roles &
                  collection::DevelopmentRolePositive) != 0) ==
                    (deck != 0),
            "first deck row did not retain background role");
    }
    expect(
        std::is_sorted(
            first.rows.begin(), first.rows.end(),
            [](const collection::BlindSelectionRow& left,
               const collection::BlindSelectionRow& right) {
                return left.input_index <
                       right.input_index;
            }),
        "blind selection lost chronological order");

    auto duplicate = input;
    duplicate.back().stable_id =
        duplicate.front().stable_id;
    expect(
        !collection::select_development_rows(duplicate)
             .valid,
        "blind selection accepted duplicate stable IDs");
    auto missing_deck = input;
    missing_deck.erase(
        std::remove_if(
            missing_deck.begin(), missing_deck.end(),
            [](const collection::BlindSelectionInput& row) {
                return row.owner_deck ==
                       old_school::DeckId::RUAggro;
            }),
        missing_deck.end());
    expect(
        !collection::select_development_rows(missing_deck)
             .valid,
        "blind selection accepted missing deck coverage");
}

void test_canonical_hidden_boundary_and_dispositions() {
    const PriorityFixture fixture = priority_fixture();
    const auto first =
        collection::diagnose_canonical_hidden_root(
            fixture.point, fixture.source,
            fixture.owner, 7, spec());
    const auto repeated =
        collection::diagnose_canonical_hidden_root(
            fixture.point, fixture.source,
            fixture.owner, 7, spec());
    expect(
        first == repeated && first.materialized &&
            first.owner_hand_preserved &&
            first.reporting_statistics_zero &&
            first.second_replay_exact &&
            first.hidden_feature_bits_identical &&
            first.hidden_clone_eligible ==
                first.hidden_clone_distinct &&
            !first.information_action_fingerprint.empty(),
        "canonical hidden boundary was not invariant");

    auto missing = fixture.point;
    missing.selected_priority_action.reset();
    const auto malformed =
        collection::build_canonical_root(
            missing, fixture.source,
            fixture.owner, 7, spec());
    expect(
        malformed.disposition ==
                collection::RootDisposition::Malformed &&
            !malformed.root.has_value(),
        "missing selected action was not malformed");

    old_school::LearnedDecisionTracePoint singleton =
        fixture.point;
    auto& owner =
        singleton.state.players[fixture.owner];
    owner.library.insert(
        owner.library.end(),
        owner.hand.begin(), owner.hand.end());
    owner.hand.clear();
    owner.land_played_this_turn = true;
    for (auto& artifact : owner.artifacts) {
        artifact.tapped = true;
    }
    singleton.state.stack.clear();
    singleton.context.consecutive_passes = 0;
    const auto legal = old_school::legal_priority_actions(
        singleton.state, fixture.owner,
        singleton.context.sorcery_actions);
    expect(
        legal.size() == 1 &&
            legal.front().kind ==
                old_school::PriorityActionKind::Pass,
        "synthetic singleton is not exactly Pass");
    singleton.selected_priority_action =
        old_school::PriorityAction::pass();
    const auto trivial =
        collection::build_canonical_root(
            singleton, fixture.source,
            fixture.owner, 9, spec());
    expect(
        trivial.disposition ==
                collection::RootDisposition::Trivial &&
            !trivial.root.has_value() &&
            !trivial.information_action_fingerprint.empty(),
        "singleton Pass was not classified trivial");
}

} // namespace

int main() {
    const std::vector<
        std::pair<std::string_view, std::function<void()>>>
        tests{
            {
                "caller-owned domains and IDs",
                test_domains_and_ids_are_seed_agnostic,
            },
            {
                "retention and collision rejection",
                test_retention_is_deterministic_and_fail_closed,
            },
            {
                "manifest binding",
                test_manifest_is_bound_and_mutation_sensitive,
            },
            {
                "robust dominance",
                test_robust_dominance_is_complete_and_fail_closed,
            },
            {
                "full robust-dominance evaluator",
                test_full_dominance_evaluator_is_deterministic,
            },
            {
                "paired parent classes",
                test_parent_classification_uses_paired_worlds,
            },
            {
                "accounting cross-sums",
                test_accounting_cross_sums_fail_closed,
            },
            {
                "blind development selection",
                test_blind_selection_is_balanced_and_deterministic,
            },
            {
                "canonical hidden boundary",
                test_canonical_hidden_boundary_and_dispositions,
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
        << " FQ4 Priority collection tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
