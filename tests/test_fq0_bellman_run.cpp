#include "old_school/fq0_bellman_audit.hpp"

#include "old_school/fq0_bellman_science.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace old_school::fq0_bellman_audit::testing {

ScientificEvidence take_reduced_construction_core_for_test(
    fq0_bellman_science::Construction construction,
    const ac1_teacher_audit::Manifest& manifest);

std::tuple<ScientificEvidence, std::size_t, std::size_t>
reconstruct_reduced_information_sets_for_test(
    const fq0_bellman_science::Construction& construction,
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> model,
    fq0_bellman_science::testing::ReducedRecipe recipe);

void validate_reconstruction_bindings_for_test(
    const ScientificEvidence& evidence);

bool dominance_pair_passes_for_test(
    GateRole role, std::size_t matching_worlds,
    bool monotonic, bool all_settlements_valid);

} // namespace old_school::fq0_bellman_audit::testing

namespace {

namespace audit = old_school::fq0_bellman_audit;
namespace science = old_school::fq0_bellman_science;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Callback>
void expect_runtime_error(
    Callback&& callback, std::string_view message) {
    bool threw = false;
    try {
        std::forward<Callback>(callback)();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, message);
}

old_school::probes::DecisionProbe test_probe() {
    std::vector<old_school::probes::DecisionProbe> probes =
        old_school::probes::make_probe_dev_v3();
    const auto found = std::find_if(
        probes.begin(), probes.end(),
        [](const old_school::probes::DecisionProbe& probe) {
            return probe.stable_id ==
                   "white.establish-millstone.v3";
        });
    if (found == probes.end()) {
        throw std::runtime_error(
            "runner fixture Priority root is missing");
    }
    return *found;
}

old_school::ac1_teacher_audit::Manifest test_manifest() {
    const old_school::probes::DecisionProbe probe =
        test_probe();
    old_school::ac1_teacher_audit::Manifest manifest;
    manifest.roots.push_back({
        .probe = probe,
        .information_action_fingerprint =
            old_school::probes::
                bsr_information_action_fingerprint(probe),
        .factory_contract_fingerprint =
            "runner-test-only-contract",
        .from_dev_v3 = true,
    });
    manifest.physical_roots_by_deck[
        static_cast<std::size_t>(
            old_school::DeckId::White)] = 1;
    return manifest;
}

void test_cli_rejects_knobs_without_running() {
    char program[] = "old-school-fq0-bellman-audit";
    char knob[] = "--seed";
    char* arguments[] = {program, knob};
    std::ostringstream output;
    std::ostringstream error;
    const int status = audit::run_cli(
        2, arguments, output, error);
    expect(
        status == 2 && output.str().empty() &&
            error.str().find("accepts no paths, seeds") !=
                std::string::npos,
        "sealed CLI accepted a knob or touched the runner");
}

void test_reduced_core_adapter_is_lossless() {
    const auto model =
        old_school::train_learned_value_champion(
            1, 0xF00D00000000C161ULL);
    const auto manifest = test_manifest();
    science::Construction construction =
        science::testing::construct_reduced(
            manifest, model,
            {
                .root_seed_base =
                    0xF00D000000002001ULL,
                .bank_a_seed_base =
                    0xF00D000000002002ULL,
                .bank_b_seed_base =
                    0xF00D000000002003ULL,
                .root_worlds =
                    old_school::fq0_bellman::kBlockCount,
                .successor_worlds = 1,
                .workers = 2,
            });
    const science::Construction original = construction;
    const audit::ScientificEvidence evidence =
        audit::testing::
            take_reduced_construction_core_for_test(
                std::move(construction), manifest);

    expect(
        evidence.manifest == manifest &&
            evidence.model_fingerprint ==
                original.model_fingerprint &&
            evidence.roots_by_deck ==
                original.roots_by_deck &&
            evidence.feature_rows ==
                original.feature_rows &&
            evidence.feature_collisions ==
                original.feature_collisions &&
            evidence.roots.size() ==
                original.roots.size() &&
            evidence
                    .successor_feature_evaluations
                    .size() ==
                original
                    .successor_feature_evaluations
                    .size(),
        "runner core adapter dropped top-level evidence");

    const science::Root& source_root =
        original.roots.front();
    const audit::RootEvidence& mapped_root =
        evidence.roots.front();
    expect(
        mapped_root.stable_id ==
                source_root.stable_id &&
            mapped_root
                    .manifest_information_action_fingerprint ==
                source_root
                    .manifest_information_action_fingerprint &&
            mapped_root.root_deck ==
                source_root.root_deck &&
            mapped_root.root_player ==
                source_root.root_player &&
            mapped_root.exact_support ==
                source_root.exact_support &&
            mapped_root.actions.size() ==
                source_root.actions.size(),
        "runner core adapter changed root identity");

    bool saw_leaf = false;
    for (std::size_t action_index = 0;
         action_index < source_root.actions.size();
         ++action_index) {
        const science::RootAction& source_action =
            source_root.actions[action_index];
        const audit::RootActionEvidence& mapped_action =
            mapped_root.actions[action_index];
        expect(
            mapped_action.descriptor ==
                    source_action.descriptor &&
                mapped_action.action ==
                    source_action.action &&
                mapped_action.feature_row_id ==
                    source_action.feature_row_id &&
                mapped_action.target ==
                    source_action.target &&
                mapped_action.policy_features ==
                    source_action.policy_features &&
                mapped_action
                        .canonical_consequence_fingerprint ==
                    source_action
                        .canonical_consequence_fingerprint &&
                mapped_action.root_transitions.size() ==
                    source_action.root_transitions.size() &&
                mapped_action.scopes.size() ==
                    source_action.scopes.size(),
            "runner core adapter changed a root action");
        for (std::size_t world = 0;
             world <
             source_action.root_transitions.size();
             ++world) {
            const science::RootTransition& source =
                source_action.root_transitions[world];
            const audit::RootTransitionParticleEvidence&
                mapped =
                    mapped_action.root_transitions[world];
            expect(
                mapped.world_index == source.world_index &&
                    mapped.determinization_seed ==
                        source.determinization_seed &&
                    mapped.macro_seed ==
                        source.macro_seed &&
                    mapped.redacted_result_hash ==
                        source.redacted_result_hash &&
                    mapped.terminal == source.terminal &&
                    mapped.forced_root_action_applied ==
                        source.forced_action_applied &&
                    mapped.actions_applied ==
                        source.actions_applied &&
                    mapped.priority_actions_applied ==
                        source.priority_actions_applied,
                "runner core adapter changed a root "
                "transition");
            if (source.terminal) {
                expect(
                    mapped
                            .terminal_root_owner_value_bits ==
                        std::bit_cast<std::uint64_t>(
                            source
                                .terminal_root_owner_value),
                    "runner adapter changed a terminal bit "
                    "pattern");
            } else {
                expect(
                    mapped
                            .terminal_root_owner_value_bits ==
                            0 &&
                        mapped
                                .successor_information_set_fingerprint ==
                            source
                                .successor_information_set_fingerprint,
                    "runner adapter malformed a successor "
                    "transition");
            }
        }

        for (std::size_t scope_index = 0;
             scope_index < source_action.scopes.size();
             ++scope_index) {
            const science::Scope& source_scope =
                source_action.scopes[scope_index];
            const audit::ScopeEvidence& mapped_scope =
                mapped_action.scopes[scope_index];
            expect(
                mapped_scope.kind ==
                    (source_scope.kind ==
                             science::ScopeKind::Full
                         ? audit::ScopeKind::FullK64
                         : audit::ScopeKind::BlockK8) &&
                    mapped_scope.block ==
                        source_scope.block &&
                    mapped_scope.root_world_indices ==
                        source_scope
                            .root_world_indices &&
                    mapped_scope.terminals ==
                        source_scope.terminals &&
                    mapped_scope.target ==
                        source_scope.target &&
                    mapped_scope
                            .exact_particle_partition ==
                        source_scope
                            .exact_particle_partition &&
                    mapped_scope.groups.size() ==
                        source_scope.groups.size(),
                "runner core adapter changed a target "
                "scope");
            for (std::size_t group_index = 0;
                 group_index < source_scope.groups.size();
                 ++group_index) {
                const science::SuccessorGroup& source_group =
                    source_scope.groups[group_index];
                const audit::SuccessorGroupEvidence&
                    mapped_group =
                        mapped_scope.groups[group_index];
                expect(
                    mapped_group
                            .information_set_fingerprint ==
                            source_group
                                .information_set_fingerprint &&
                        mapped_group.successor_owner ==
                            source_group.successor_owner &&
                        mapped_group.relation ==
                            source_group.relation &&
                        mapped_group.root_world_indices ==
                            source_group
                                .root_world_indices &&
                        mapped_group.cross_fit ==
                            source_group.cross_fit &&
                        mapped_group.bank_a.actions.size() ==
                            source_group
                                .bank_a.actions.size(),
                    "runner adapter changed a successor "
                    "group");
                for (std::size_t row = 0;
                     row <
                     source_group.bank_a.actions.size();
                     ++row) {
                    const auto& source_row =
                        source_group.bank_a.actions[row];
                    const auto& mapped_row =
                        mapped_group.bank_a.actions[row];
                    expect(
                        mapped_row.descriptor ==
                                source_row.descriptor &&
                            mapped_row.action ==
                                source_row.action &&
                            mapped_row.samples.size() ==
                                source_row.samples.size(),
                        "runner adapter changed a bank row");
                    for (std::size_t sample = 0;
                         sample <
                         source_row.samples.size();
                         ++sample) {
                        const auto& source_leaf =
                            source_row.samples[sample];
                        const auto& mapped_leaf =
                            mapped_row.samples[sample];
                        saw_leaf = true;
                        expect(
                            mapped_leaf.score_bits ==
                                    std::bit_cast<
                                        std::uint64_t>(
                                        source_leaf.score) &&
                                mapped_leaf
                                        .contextual_score_bits ==
                                    source_leaf
                                        .contextual_score_bits &&
                                mapped_leaf
                                        .legacy_score_bits ==
                                    source_leaf
                                        .legacy_score_bits &&
                                mapped_leaf
                                        .forced_action_applied ==
                                    source_leaf
                                        .forced_action_applied &&
                                mapped_leaf
                                        .critic_evaluated ==
                                    source_leaf
                                        .critic_evaluated,
                            "runner adapter changed a leaf "
                            "sample bit pattern");
                    }
                }
            }
        }
    }
    expect(
        saw_leaf,
        "reduced runner fixture did not exercise a leaf");
}

std::vector<old_school::fq0_bellman::ActionSamples>
bank_action_samples(const audit::GroupBankEvidence& bank) {
    std::vector<old_school::fq0_bellman::ActionSamples>
        result;
    result.reserve(bank.actions.size());
    for (const audit::GroupActionEvidence& action :
         bank.actions) {
        old_school::fq0_bellman::ActionSamples row{
            .descriptor = action.descriptor,
            .sample_stream_key = bank.stream_key,
        };
        row.world_indices.reserve(action.samples.size());
        row.samples.reserve(action.samples.size());
        for (const audit::LeafSampleEvidence& sample :
             action.samples) {
            row.world_indices.push_back(sample.world_index);
            row.samples.push_back(
                std::bit_cast<double>(sample.score_bits));
        }
        result.push_back(std::move(row));
    }
    return result;
}

old_school::fq0_bellman::CrossFitValue recompute_cross_fit(
    const audit::GroupBankEvidence& bank_a,
    const audit::GroupBankEvidence& bank_b) {
    return old_school::fq0_bellman::cross_fit_v0(
        bank_action_samples(bank_a),
        bank_action_samples(bank_b));
}

std::string group_coordinate(
    const audit::RootEvidence& root,
    std::size_t action, std::size_t scope,
    std::size_t group) {
    return root.stable_id + "/action/" +
           std::to_string(action) + "/scope/" +
           std::to_string(scope) + "/group/" +
           std::to_string(group);
}

std::string feature_scope_coordinate(
    const audit::SuccessorFeatureEvaluationEvidence&
        evaluation,
    std::size_t scope) {
    return evaluation.root_stable_id + "/" +
           evaluation.information_set_fingerprint +
           "/feature-scope/" + std::to_string(scope);
}

struct ReconstructionFixture {
    audit::ScientificEvidence evidence;
    std::size_t expected_feature_scopes = 0;
    std::size_t expected_group_occurrences = 0;
    std::size_t observed_feature_scopes = 0;
    std::size_t observed_group_occurrences = 0;
};

const ReconstructionFixture& reconstruction_fixture() {
    static const ReconstructionFixture fixture = [] {
        const auto model =
            old_school::train_learned_value_champion(
                1, 0xF00D00000000C162ULL);
        const auto manifest = test_manifest();
        const science::testing::ReducedRecipe recipe{
            .root_seed_base =
                0xF00D000000003001ULL,
            .bank_a_seed_base =
                0xF00D000000003002ULL,
            .bank_b_seed_base =
                0xF00D000000003003ULL,
            .root_worlds =
                old_school::fq0_bellman::kBlockCount,
            .successor_worlds = 1,
            .workers = 2,
        };
        const science::Construction construction =
            science::testing::construct_reduced(
                manifest, model, recipe);

        std::size_t expected_feature_scopes = 0;
        for (const auto& evaluation :
             construction.successor_feature_evaluations) {
            expected_feature_scopes +=
                evaluation.scopes.size();
        }
        std::size_t expected_group_occurrences = 0;
        for (const auto& root : construction.roots) {
            for (const auto& action : root.actions) {
                for (const auto& scope : action.scopes) {
                    expected_group_occurrences +=
                        scope.groups.size();
                }
            }
        }
        expect(
            expected_feature_scopes > 0 &&
                expected_group_occurrences > 0,
            "reduced reuse fixture has no successor work");

        auto [evidence, observed_feature_scopes,
              observed_group_occurrences] =
            audit::testing::
                reconstruct_reduced_information_sets_for_test(
                    construction, manifest, model, recipe);
        return ReconstructionFixture{
            .evidence = std::move(evidence),
            .expected_feature_scopes =
                expected_feature_scopes,
            .expected_group_occurrences =
                expected_group_occurrences,
            .observed_feature_scopes =
                observed_feature_scopes,
            .observed_group_occurrences =
                observed_group_occurrences,
        };
    }();
    return fixture;
}

void test_reconstruction_reuses_feature_scope_work() {
    const ReconstructionFixture& fixture =
        reconstruction_fixture();
    expect(
        fixture.observed_feature_scopes ==
                fixture.expected_feature_scopes &&
            fixture.observed_group_occurrences ==
                fixture.expected_group_occurrences,
        "runner repeated empirical-group work instead of "
        "reusing the feature-scope reconstruction");
}

void test_reconstruction_hashes_bind_raw_banks_and_coordinates() {
    const audit::ScientificEvidence& evidence =
        reconstruction_fixture().evidence;
    bool saw_group = false;
    for (const audit::RootEvidence& root : evidence.roots) {
        for (std::size_t action_index = 0;
             action_index < root.actions.size();
             ++action_index) {
            const audit::RootActionEvidence& action =
                root.actions[action_index];
            for (std::size_t scope_index = 0;
                 scope_index < action.scopes.size();
                 ++scope_index) {
                const audit::ScopeEvidence& scope =
                    action.scopes[scope_index];
                for (std::size_t group_index = 0;
                     group_index < scope.groups.size();
                     ++group_index) {
                    saw_group = true;
                    const audit::SuccessorGroupEvidence&
                        group = scope.groups[group_index];
                    const std::string coordinate =
                        group_coordinate(
                            root, action_index,
                            scope_index, group_index);
                    const std::string baseline =
                        audit::binding::
                            successor_operator_bank_pair_payload_sha256(
                                group.bank_a, group.bank_b,
                                group.cross_fit);
                    expect(
                        group.hidden_repartition_witness
                                    .coordinate ==
                                audit::binding::
                                    hidden_repartition_coordinate(
                                        coordinate,
                                        group
                                            .representative_root_world,
                                        group
                                            .representative_root_action_descriptor) &&
                            group.hidden_repartition_witness
                                    .baseline_sha256 ==
                                baseline &&
                            group.hidden_repartition_witness
                                    .comparison_sha256 ==
                                baseline,
                        "group hidden witness is not bound "
                        "to its exact action/world banks");
                    expect(
                        group
                                .representative_reconstruction_witnesses
                                .size() ==
                            group.root_world_indices.size(),
                        "group representative witness census "
                        "is incomplete");
                    for (std::size_t member = 0;
                         member <
                         group.root_world_indices.size();
                         ++member) {
                        const auto& witness =
                            group
                                .representative_reconstruction_witnesses
                                    [member];
                        expect(
                            witness.coordinate ==
                                    coordinate + "/member/" +
                                        std::to_string(
                                            group
                                                .root_world_indices
                                                    [member]) &&
                                witness.baseline_sha256 ==
                                    baseline &&
                                witness.comparison_sha256 ==
                                    baseline,
                            "group member witness is not "
                            "bound to retained raw banks");
                    }
                }
            }
        }
    }

    bool saw_feature_scope = false;
    for (const auto& evaluation :
         evidence.successor_feature_evaluations) {
        for (std::size_t scope_index = 0;
             scope_index < evaluation.scopes.size();
             ++scope_index) {
            saw_feature_scope = true;
            const auto& scope =
                evaluation.scopes[scope_index];
            const std::string coordinate =
                feature_scope_coordinate(
                    evaluation, scope_index);
            const auto cross_fit = recompute_cross_fit(
                scope.bank_a, scope.bank_b);
            const std::string baseline =
                audit::binding::
                    successor_operator_bank_pair_payload_sha256(
                        scope.bank_a, scope.bank_b,
                        cross_fit);
            expect(
                scope.hidden_repartition_witness.coordinate ==
                        audit::binding::
                            hidden_repartition_coordinate(
                                coordinate,
                                evaluation
                                    .representative_root_world,
                                evaluation
                                    .representative_root_action_descriptor) &&
                    scope.hidden_repartition_witness
                            .baseline_sha256 ==
                        baseline &&
                    scope.hidden_repartition_witness
                            .comparison_sha256 ==
                        baseline,
                "feature hidden witness is not bound to "
                "its exact action/world banks");
            expect(
                scope.representative_catalog.size() ==
                    scope
                        .representative_reconstruction_witnesses
                        .size(),
                "feature representative witness census is "
                "incomplete");
            for (std::size_t member = 0;
                 member <
                 scope.representative_catalog.size();
                 ++member) {
                const auto& representative =
                    scope.representative_catalog[member];
                const auto& witness =
                    scope
                        .representative_reconstruction_witnesses
                            [member];
                expect(
                    witness.coordinate ==
                            coordinate + "/member/" +
                                std::to_string(
                                    representative
                                        .root_world) +
                                "/" +
                                representative
                                    .root_action_descriptor &&
                        witness.baseline_sha256 == baseline &&
                        witness.comparison_sha256 == baseline,
                    "feature member witness is not bound to "
                    "retained raw banks");
            }
        }
    }
    expect(
        saw_group && saw_feature_scope,
        "reduced binding fixture did not exercise both "
        "witness domains");
    audit::testing::
        validate_reconstruction_bindings_for_test(evidence);
}

void test_operator_binding_omits_only_consequence() {
    const auto first_group =
        [](audit::ScientificEvidence& evidence)
            -> audit::SuccessorGroupEvidence& {
            for (audit::RootEvidence& root : evidence.roots) {
                for (audit::RootActionEvidence& action :
                     root.actions) {
                    for (audit::ScopeEvidence& scope :
                         action.scopes) {
                        if (!scope.groups.empty()) {
                            return scope.groups.front();
                        }
                    }
                }
            }
            throw std::runtime_error(
                "operator binding fixture has no group");
        };

    audit::ScientificEvidence consequence =
        reconstruction_fixture().evidence;
    audit::SuccessorGroupEvidence& consequence_group =
        first_group(consequence);
    const std::string operator_before =
        audit::binding::
            successor_operator_bank_pair_payload_sha256(
                consequence_group.bank_a,
                consequence_group.bank_b,
                consequence_group.cross_fit);
    const std::string full_before =
        audit::binding::successor_bank_pair_payload_sha256(
            consequence_group.bank_a,
            consequence_group.bank_b,
            consequence_group.cross_fit);
    consequence_group.bank_a.actions.front()
        .canonical_consequence_fingerprint += "-local";
    expect(
        audit::binding::
                successor_operator_bank_pair_payload_sha256(
                    consequence_group.bank_a,
                    consequence_group.bank_b,
                    consequence_group.cross_fit) ==
                operator_before &&
            audit::binding::
                successor_bank_pair_payload_sha256(
                    consequence_group.bank_a,
                    consequence_group.bank_b,
                    consequence_group.cross_fit) !=
                full_before,
        "runner did not separate operator v2 from full v1");
    audit::testing::
        validate_reconstruction_bindings_for_test(
            consequence);

    const auto expect_operator_rejects =
        [&](std::string_view field,
            const std::function<void(
                audit::SuccessorGroupEvidence&)>& mutate) {
            audit::ScientificEvidence changed =
                reconstruction_fixture().evidence;
            mutate(first_group(changed));
            expect_runtime_error(
                [&] {
                    audit::testing::
                        validate_reconstruction_bindings_for_test(
                            changed);
                },
                std::string(
                    "operator witness ignored ") +
                    std::string(field));
        };
    expect_operator_rejects(
        "descriptor",
        [](auto& group) {
            group.bank_a.actions.front().descriptor +=
                "-changed";
        });
    expect_operator_rejects(
        "leaf hash",
        [](auto& group) {
            group.bank_a.actions.front()
                .samples.front()
                .redacted_leaf_hash += "-changed";
        });
    expect_operator_rejects(
        "score bits",
        [](auto& group) {
            group.bank_a.actions.front()
                .samples.front()
                .score_bits =
                std::bit_cast<std::uint64_t>(0.125);
        });
    expect_operator_rejects(
        "determinization seed",
        [](auto& group) {
            ++group.bank_a.actions.front()
                  .samples.front()
                  .determinization_seed;
        });
    expect_operator_rejects(
        "transition counter",
        [](auto& group) {
            ++group.bank_a.actions.front()
                  .samples.front()
                  .actions_applied;
        });
    expect_operator_rejects(
        "cross-fit",
        [](auto& group) {
            group.cross_fit.value = 0.125;
        });
}

void test_comparison_hash_mutation_fails_binding() {
    audit::ScientificEvidence mutated =
        reconstruction_fixture().evidence;
    audit::BitIdentityEvidence* witness = nullptr;
    for (audit::RootEvidence& root : mutated.roots) {
        for (audit::RootActionEvidence& action :
             root.actions) {
            for (audit::ScopeEvidence& scope :
                 action.scopes) {
                for (audit::SuccessorGroupEvidence& group :
                     scope.groups) {
                    if (!group
                             .representative_reconstruction_witnesses
                             .empty()) {
                        witness =
                            &group
                                 .representative_reconstruction_witnesses
                                 .front();
                        break;
                    }
                }
                if (witness != nullptr) {
                    break;
                }
            }
            if (witness != nullptr) {
                break;
            }
        }
        if (witness != nullptr) {
            break;
        }
    }
    expect(
        witness != nullptr &&
            !witness->comparison_sha256.empty(),
        "reduced mutation fixture has no reconstruction "
        "comparison hash");
    witness->comparison_sha256.front() =
        witness->comparison_sha256.front() == '0' ? '1' : '0';
    expect_runtime_error(
        [&] {
            audit::testing::
                validate_reconstruction_bindings_for_test(
                    mutated);
        },
        "mutated science comparison hash was accepted");
}

void test_hidden_witness_cannot_be_reused_for_distinct_rep() {
    audit::ScientificEvidence relabeled =
        reconstruction_fixture().evidence;
    struct HiddenReference {
        audit::BitIdentityEvidence* witness = nullptr;
        std::size_t world = 0;
        std::string descriptor;
    };
    std::vector<HiddenReference> hidden;
    for (audit::RootEvidence& root : relabeled.roots) {
        for (audit::RootActionEvidence& action :
             root.actions) {
            for (audit::ScopeEvidence& scope :
                 action.scopes) {
                for (audit::SuccessorGroupEvidence& group :
                     scope.groups) {
                    hidden.push_back({
                        .witness =
                            &group
                                 .hidden_repartition_witness,
                        .world =
                            group.representative_root_world,
                        .descriptor =
                            group
                                .representative_root_action_descriptor,
                    });
                }
            }
        }
    }
    std::size_t second = hidden.size();
    for (std::size_t candidate = 1;
         candidate < hidden.size(); ++candidate) {
        if (hidden[candidate].world != hidden.front().world ||
            hidden[candidate].descriptor !=
                hidden.front().descriptor) {
            second = candidate;
            break;
        }
    }
    expect(
        !hidden.empty() && second < hidden.size(),
        "reduced relabel fixture has no two distinct "
        "empirical representatives");
    *hidden[second].witness = *hidden.front().witness;
    expect_runtime_error(
        [&] {
            audit::testing::
                validate_reconstruction_bindings_for_test(
                    relabeled);
        },
        "one representative's hidden witness was reusable "
        "for a distinct action/world representative");
}

void test_invalid_gated_dominance_is_scientific_failure() {
    expect(
        !audit::testing::
            dominance_pair_passes_for_test(
                audit::GateRole::Primary,
                audit::kRootWorlds, true, false) &&
            audit::testing::
                dominance_pair_passes_for_test(
                    audit::GateRole::Primary,
                    audit::kRootWorlds, true, true) &&
            audit::testing::
                dominance_pair_passes_for_test(
                    audit::GateRole::Descriptive,
                    audit::kRootWorlds, true, false),
        "invalid gated dominance did not become a complete "
        "scientific failure");
}

} // namespace

int main() {
    const std::vector<
        std::pair<std::string_view, std::function<void()>>>
        tests = {
            {"sealed CLI rejects knobs",
             test_cli_rejects_knobs_without_running},
            {"reduced construction adapter",
             test_reduced_core_adapter_is_lossless},
            {"feature reconstruction reuse and binding",
             test_reconstruction_reuses_feature_scope_work},
            {"reconstruction raw-bank binding",
             test_reconstruction_hashes_bind_raw_banks_and_coordinates},
            {"operator v2 field boundary",
             test_operator_binding_omits_only_consequence},
            {"comparison mutation rejects",
             test_comparison_hash_mutation_fails_binding},
            {"hidden representative relabel rejects",
             test_hidden_witness_cannot_be_reused_for_distinct_rep},
            {"invalid gated dominance rejects",
             test_invalid_gated_dominance_is_scientific_failure},
        };
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& failure) {
            std::cerr << "[FAIL] " << name << ": "
                      << failure.what() << '\n';
        }
    }
    std::cout << passed << "/" << tests.size()
              << " tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
