#include "old_school/fq0_causal_quotient.hpp"
#include "old_school/artifact_integrity.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace causal = old_school::fq0_causal_quotient;
namespace probes = old_school::probes;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::shared_ptr<const old_school::LearnedModel> tiny_value_model() {
    static const auto model =
        old_school::train_learned_value_champion(
            1, 0xF001CA05ULL);
    return model;
}

std::vector<causal::RegisteredPairSpec>
expected_fr2_specs() {
    constexpr std::string_view root =
        "white.mill-before-draw.v3";
    return {
        {
            std::string(root),
            "13373b5ab30f0eb1832601a9cacd66a95bb846e4f06b9cffb73445eeecd4c5bb",
            "9cbc2446029f4738a1ed05baac7fa3230d6015056c047a7264aa83ee2e7dbb55",
            2,
        },
        {
            std::string(root),
            "37314baad7d9941ec619e1fe48e211e61581a5c416e85405ef5d81112704ba66",
            "bf17dc395deedc0e40da5a0864766c7a64f3c1847f821ed03cd9bb20c0d8c1f2",
            2,
        },
        {
            std::string(root),
            "435fc94759197bc8204b1d652a1c0ce6db33984083b15e517bbb7790657d8e61",
            "f16da3e2eeef23a8a45dc221245353987095c6e125e1aadef02ee49b3fe7992e",
            2,
        },
        {
            std::string(root),
            "520832b961bda1af51034d20b5b35c63c6f7a3d5c688dd60eb55c73988dbd7b2",
            "a3ebe4d0768400b927e3394cb3dbf219689ab93f472008f28a7d2a098aa1e1e7",
            2,
        },
        {
            std::string(root),
            "5d4cdf24b01f6a5179e4c21b98c4fd3b5d921c3115b184a0cebbb9d410185176",
            "e515cc9a6c2b9492ae9ec32be6c80970f7bce395a54e22591b1d75ed159cc275",
            2,
        },
    };
}

bool same_multiset(
    std::vector<old_school::CardId> first,
    std::vector<old_school::CardId> second) {
    std::sort(first.begin(), first.end());
    std::sort(second.begin(), second.end());
    return first == second;
}

std::string independently_derive_fr3_catalog_sha256(
    const std::vector<causal::ResidualCatalogRow>& rows) {
    std::string bytes =
        "old-school-fr3-registered-residual-catalog-v1\n";
    for (const causal::ResidualCatalogRow& row : rows) {
        bytes.append(row.root_stable_id)
            .push_back('\t');
        bytes.append(row.first_information_set)
            .push_back('\t');
        bytes.append(row.second_information_set)
            .push_back('\t');
        bytes.append(
                 row.first_quotient_information_set)
            .push_back('\t');
        bytes.append(row.action_descriptor)
            .push_back('\t');
        bytes.append(row.first_legacy_consequence)
            .push_back('\t');
        bytes.append(row.second_legacy_consequence)
            .push_back('\t');
        bytes.append(row.first_quotient_consequence)
            .push_back('\n');
    }
    return old_school::artifact_integrity::sha256_string(
        bytes);
}

void test_registered_identity_census_is_exact() {
    const auto& specs = causal::registered_pair_specs();
    const std::size_t blue_pairs =
        static_cast<std::size_t>(std::count_if(
            specs.begin(), specs.end(),
            [](const causal::RegisteredPairSpec& spec) {
                return spec.root_stable_id ==
                       "control.blue.counter-same-target-after-"
                       "intervening-counter.v1";
            }));
    const std::size_t blue_rows =
        std::accumulate(
            specs.begin(), specs.end(), std::size_t{0},
            [](std::size_t total,
               const causal::RegisteredPairSpec& spec) {
                return total +
                       (spec.root_stable_id.starts_with(
                            "control.blue.")
                            ? spec.legacy_collision_rows
                            : 0);
            });
    const std::size_t all_rows =
        std::accumulate(
            specs.begin(), specs.end(), std::size_t{0},
            [](std::size_t total,
               const causal::RegisteredPairSpec& spec) {
                return total +
                       spec.legacy_collision_rows;
            });
    expect(
        specs.size() == 44 && blue_pairs == 38 &&
            blue_rows == 163 &&
            all_rows == 177,
        "registered FR1 pair/row anatomy drifted");
}

void test_direct_controls_are_causally_equal() {
    const causal::DirectControlReport first =
        causal::compare_direct_controls(tiny_value_model());
    const causal::DirectControlReport second =
        causal::compare_direct_controls(tiny_value_model());
    expect(
        first.passed() && first == second,
        "FR1 direct controls failed or were not repeatable");
}

void test_registered_reconstruction_rejects_literal_hypothesis() {
    const causal::RegisteredAnatomyReport first =
        causal::testing::reconstruct_registered_anatomy(
            tiny_value_model());
    expect(
        first.bounded_root_macros == 448 &&
            first.incomplete_root_macros == 0 &&
            first.registered_pairs == 44 &&
            first.reconstructed_pairs == 44 &&
            first.graveyard_only_pairs == 39 &&
            first.graveyard_only_rows == 167 &&
            first.additional_public_difference_pairs == 5 &&
            first.additional_public_difference_rows == 10 &&
            first.equivalent_pairs == 39 &&
            first.legacy_collision_rows == 177 &&
            first.blue_collision_rows == 163 &&
            first.white_collision_rows == 14 &&
            first.registered_row_identity_sha256 ==
                causal::kRegisteredRowIdentitySha256 &&
            first.additional_public_representatives.size() ==
                5 &&
            first.exact_registered_anatomy() &&
            first.exact_registered_rejection() &&
            !first.passed(),
        "registered FR1 rejection anatomy drifted");
    const std::vector<std::string> expected_failures{
        "white.mill-before-draw.v3:non-graveyard-public-difference-"
        "observer-hand-order-only:"
        "13373b5ab30f0eb1832601a9cacd66a95bb846e4f06b9cffb73445eeecd4c5bb:"
        "9cbc2446029f4738a1ed05baac7fa3230d6015056c047a7264aa83ee2e7dbb55",
        "white.mill-before-draw.v3:non-graveyard-public-difference-"
        "observer-hand-order-only:"
        "37314baad7d9941ec619e1fe48e211e61581a5c416e85405ef5d81112704ba66:"
        "bf17dc395deedc0e40da5a0864766c7a64f3c1847f821ed03cd9bb20c0d8c1f2",
        "white.mill-before-draw.v3:non-graveyard-public-difference-"
        "observer-hand-order-only:"
        "435fc94759197bc8204b1d652a1c0ce6db33984083b15e517bbb7790657d8e61:"
        "f16da3e2eeef23a8a45dc221245353987095c6e125e1aadef02ee49b3fe7992e",
        "white.mill-before-draw.v3:non-graveyard-public-difference-"
        "observer-hand-order-only:"
        "520832b961bda1af51034d20b5b35c63c6f7a3d5c688dd60eb55c73988dbd7b2:"
        "a3ebe4d0768400b927e3394cb3dbf219689ab93f472008f28a7d2a098aa1e1e7",
        "white.mill-before-draw.v3:non-graveyard-public-difference-"
        "observer-hand-order-only:"
        "5d4cdf24b01f6a5179e4c21b98c4fd3b5d921c3115b184a0cebbb9d410185176:"
        "e515cc9a6c2b9492ae9ec32be6c80970f7bce395a54e22591b1d75ed159cc275",
    };
    expect(
        first.reconstruction_failures == expected_failures,
        "FR1 extra-public-difference identities drifted");
    const causal::RegisteredAnatomyReport second =
        causal::testing::reconstruct_registered_anatomy(
            tiny_value_model());
    expect(
        first == second && !second.passed(),
        "registered FR1 replay was not bit-identical");
}

void test_factorial_inputs_are_exact_and_complete() {
    const causal::RegisteredAnatomyReport anatomy =
        causal::testing::reconstruct_registered_anatomy(
            tiny_value_model());
    std::vector<causal::RegisteredPairSpec> actual_specs;
    const std::vector<old_school::PriorityAction>
        expected_actions{
            old_school::PriorityAction::pass(),
            old_school::PriorityAction::cast_artifact(
                old_school::CardId::MoxSapphire),
        };
    constexpr std::array<std::size_t, 5>
        expected_root_worlds{0, 24, 5, 17, 42};
    constexpr std::array<std::uint64_t, 5>
        expected_continuation_seeds{
            3490941223041417199ULL,
            13999545523412929041ULL,
            5716191040556191100ULL,
            13649792070827152751ULL,
            11293163517197892497ULL,
        };
    std::size_t pair_index = 0;
    for (const causal::RegisteredRepresentativePair& pair :
         anatomy.additional_public_representatives) {
        actual_specs.push_back(pair.spec);
        expect(
            pair.authoritative_actions == expected_actions &&
                pair.observer ==
                    pair.context.decision_player &&
                pair.first_root_world ==
                    expected_root_worlds[pair_index] &&
                pair.first_root_action == "mill-self" &&
                pair.continuation_seed ==
                    expected_continuation_seeds[pair_index],
            "FR2 representative coordinate/action pin drifted");
        const auto& first_hand =
            pair.first_state.players[pair.observer].hand;
        const auto& second_hand =
            pair.second_state.players[pair.observer].hand;
        expect(
            first_hand != second_hand &&
                same_multiset(first_hand, second_hand),
            "FR2 representative hand factor drifted");
        bool graveyard_order_differs = false;
        for (std::size_t player = 0;
             player < pair.first_state.players.size();
             ++player) {
            const auto& first_graveyard =
                pair.first_state.players[player].graveyard;
            const auto& second_graveyard =
                pair.second_state.players[player].graveyard;
            graveyard_order_differs =
                graveyard_order_differs ||
                first_graveyard != second_graveyard;
            expect(
                same_multiset(
                    first_graveyard,
                    second_graveyard),
                "FR2 representative graveyard multiset drifted");
        }
        expect(
            graveyard_order_differs,
            "FR2 representative graveyard factor became trivial");
        ++pair_index;
    }
    expect(
        causal::registered_factorial_pair_specs() ==
                expected_fr2_specs() &&
            actual_specs == expected_fr2_specs(),
        "FR2 ordered representative identities drifted");
}

void test_factorial_gate_is_exact_and_repeatable() {
    const causal::FactorialReport first =
        causal::testing::evaluate_sequence_factorial(
            tiny_value_model());
    expect(
        first.infrastructure_valid() &&
            first.passed() &&
            first.eligible_pairs == 5 &&
            first.contrasts == 15 &&
            first.action_comparisons == 30 &&
            first.graveyard_contrasts_equal == 5 &&
            first.observer_hand_contrasts_equal == 5 &&
            first.combined_contrasts_equal == 5 &&
            first.wrong_mask_controls_detected == 4 &&
            first.wrong_mask_controls.size() == 4 &&
            first.results.size() == 15 &&
            first.failures.empty(),
        "FR2 factorial census or verdict drifted");
    const std::vector<causal::SequenceTreatment>
        expected_treatments{
            causal::SequenceTreatment::Graveyards,
            causal::SequenceTreatment::ObserverHand,
            causal::SequenceTreatment::
                GraveyardsAndObserverHand,
        };
    const std::vector<old_school::PriorityAction>
        expected_actions{
            old_school::PriorityAction::pass(),
            old_school::PriorityAction::cast_artifact(
                old_school::CardId::MoxSapphire),
        };
    const auto expected_specs = expected_fr2_specs();
    for (std::size_t pair_index = 0;
         pair_index < expected_specs.size();
         ++pair_index) {
        const auto& representative =
            first.anatomy
                .additional_public_representatives[
                    pair_index];
        for (std::size_t treatment_index = 0;
             treatment_index <
                 expected_treatments.size();
             ++treatment_index) {
            const causal::FactorialContrast& contrast =
                first.results[
                    pair_index *
                        expected_treatments.size() +
                    treatment_index];
            const causal::SequenceTreatment treatment =
                expected_treatments[treatment_index];
            const std::string& expected_intervention =
                treatment ==
                        causal::SequenceTreatment::
                            ObserverHand
                    ? expected_specs[pair_index]
                          .first_information_set
                    : expected_specs[pair_index]
                          .second_information_set;
            expect(
                contrast.spec ==
                        expected_specs[pair_index] &&
                    contrast.treatment == treatment &&
                    contrast.continuation_seed ==
                        representative
                            .continuation_seed &&
                    contrast.baseline_information_set ==
                        expected_specs[pair_index]
                            .first_information_set &&
                    contrast.expected_information_set ==
                        expected_intervention &&
                    contrast.intervention_information_set ==
                        expected_intervention &&
                    contrast.equivalent(),
                "FR2 factorial identity/seeding pin drifted");
            std::vector<old_school::PriorityAction>
                actual_actions;
            for (const causal::ActionComparison& action :
                 contrast.actions) {
                actual_actions.push_back(action.action);
                expect(
                    action.equivalent(),
                    "FR2 factorial action was not exact");
            }
            expect(
                actual_actions == expected_actions,
                "FR2 factorial action vector drifted");
        }
    }
    expect(
        first.life_perturbation_detected &&
            first.life_control
                .relevant_multisets_preserved &&
            first.life_control.treatment_nontrivial &&
            !first.life_control.treated_factor_only &&
            !first.life_control.equivalent(),
        "FR2 life-total negative control drifted");
    const std::vector<causal::SequenceTreatment>
        expected_wrong_masks{
            causal::SequenceTreatment::ObserverHand,
            causal::SequenceTreatment::Graveyards,
            causal::SequenceTreatment::Graveyards,
            causal::SequenceTreatment::ObserverHand,
        };
    for (std::size_t index = 0;
         index < expected_wrong_masks.size(); ++index) {
        const causal::FactorialContrast& control =
            first.wrong_mask_controls[index];
        expect(
            control.treatment ==
                    expected_wrong_masks[index] &&
                control.relevant_multisets_preserved &&
                !control.treated_factor_only &&
                !control.equivalent(),
            "FR2 wrong-mask control failed to reject");
    }
    expect(
        !first.wrong_mask_controls[0]
             .treatment_nontrivial &&
            !first.wrong_mask_controls[1]
                 .treatment_nontrivial &&
            first.wrong_mask_controls[2]
                .treatment_nontrivial &&
            first.wrong_mask_controls[3]
                .treatment_nontrivial,
        "FR2 wrong-mask control anatomy drifted");
    causal::FactorialReport incomplete = first;
    incomplete.results[0].actions[0].complete = false;
    expect(
        !incomplete.infrastructure_valid() &&
            !incomplete.passed(),
        "FR2 incomplete macro was not classified as infrastructure");
    causal::FactorialReport causal_rejection = first;
    causal_rejection.results[0]
        .actions[0]
        .disposition_equal = false;
    expect(
        causal_rejection.infrastructure_valid() &&
            !causal_rejection.passed(),
        "FR2 complete causal mismatch was not a scientific reject");
    const causal::FactorialReport second =
        causal::testing::evaluate_sequence_factorial(
            tiny_value_model());
    expect(
        first == second && second.passed(),
        "FR2 factorial replay was not bit-identical");
}

void test_residual_conflict_gate_is_exact_and_repeatable() {
    const causal::ResidualConflictReport first =
        causal::testing::
            evaluate_registered_residual_conflicts(
                tiny_value_model());
    expect(
        first.infrastructure_valid() &&
            first.passed() &&
            first.prerequisite.passed() &&
            first.controlled_pairs == 44 &&
            first.paired_actions == 177 &&
            first.source_state_action_instances == 354 &&
            first.exact_legacy_identity_pairs == 44 &&
            first.quotient_information_pairs_equal == 44 &&
            first.policy_feature_rows_bit_identical ==
                177 &&
            first.legacy_leaf_conflict_pairs == 44 &&
            first.quotient_leaf_conflict_pairs == 0 &&
            first.legacy_consequence_conflicts == 177 &&
            first.residual_quotient_conflicts == 0 &&
            first.changed_graveyard_multiset_distinct &&
            first.life_total_distinct &&
            first.hidden_repartition_aliased &&
            first.infrastructure_failures.empty() &&
            first.catalog_rows.size() == 177,
        "FR3 compact residual census or verdict drifted");
    expect(
        first.catalog_sha256 ==
            independently_derive_fr3_catalog_sha256(
                first.catalog_rows),
        "FR3 production/test catalog derivations diverged");
    expect(
        first.catalog_sha256 ==
            causal::kRegisteredResidualCatalogSha256,
        "FR3 portable catalog drifted from the frozen digest");
    std::cout
        << "[INFO] FR3 portable_catalog_sha256="
        << first.catalog_sha256 << '\n';

    const auto& specs = causal::registered_pair_specs();
    std::size_t row_offset = 0;
    for (const causal::RegisteredPairSpec& spec : specs) {
        for (std::size_t action_index = 0;
             action_index < spec.legacy_collision_rows;
             ++action_index) {
            const causal::ResidualCatalogRow& row =
                first.catalog_rows[row_offset++];
            expect(
                row.root_stable_id ==
                        spec.root_stable_id &&
                    row.first_information_set ==
                        spec.first_information_set &&
                    row.second_information_set ==
                        spec.second_information_set &&
                    row.first_quotient_information_set ==
                        row.second_quotient_information_set &&
                    row.first_legacy_consequence !=
                        row.second_legacy_consequence &&
                    row.first_quotient_consequence ==
                        row.second_quotient_consequence &&
                    row.policy_features_bit_identical &&
                    row.action_descriptor ==
                        probes::
                            stable_priority_action_descriptor(
                                row.action),
                "FR3 ordered catalog identity/action pin drifted");
        }
    }
    expect(
        row_offset == first.catalog_rows.size(),
        "FR3 ordered catalog row census drifted");

    const causal::ResidualConflictReport second =
        causal::testing::
            evaluate_registered_residual_conflicts(
                tiny_value_model());
    expect(
        first == second && second.passed(),
        "FR3 compact residual replay was not bit-identical");
}

void test_residual_conflict_gate_classifies_mutations() {
    const causal::ResidualConflictReport baseline =
        causal::testing::
            evaluate_registered_residual_conflicts(
                tiny_value_model());

    causal::ResidualConflictReport scientific = baseline;
    scientific.catalog_rows[0]
        .second_quotient_consequence =
        scientific.catalog_rows[0]
            .second_legacy_consequence;
    scientific.residual_quotient_conflicts = 1;
    expect(
        scientific.infrastructure_valid() &&
            !scientific.passed(),
        "FR3 complete residual conflict was not scientific");

    causal::ResidualConflictReport feature = baseline;
    feature.catalog_rows[0]
        .policy_features_bit_identical = false;
    --feature.policy_feature_rows_bit_identical;
    expect(
        feature.infrastructure_valid() &&
            !feature.passed(),
        "FR3 complete feature conflict was not scientific");

    causal::ResidualConflictReport incomplete = baseline;
    ++incomplete.source_state_action_instances;
    expect(
        !incomplete.infrastructure_valid() &&
            !incomplete.passed(),
        "FR3 source-instance drift was not infrastructure");

    causal::ResidualConflictReport control = baseline;
    control.hidden_repartition_aliased = false;
    expect(
        !control.infrastructure_valid() &&
            !control.passed(),
        "FR3 hidden-repartition control drift was accepted");

    causal::ResidualConflictReport catalog = baseline;
    catalog.catalog_sha256.front() =
        catalog.catalog_sha256.front() == '0' ? '1' : '0';
    expect(
        !catalog.infrastructure_valid() &&
            !catalog.passed(),
        "FR3 catalog mutation was not infrastructure");
}

} // namespace

int main() {
    const std::vector<
        std::pair<std::string, std::function<void()>>>
        tests = {
            {
                "registered identity census is exact",
                test_registered_identity_census_is_exact,
            },
            {
                "direct causal controls",
                test_direct_controls_are_causally_equal,
            },
            {
                "registered reconstruction",
                test_registered_reconstruction_rejects_literal_hypothesis,
            },
            {
                "factorial inputs",
                test_factorial_inputs_are_exact_and_complete,
            },
            {
                "factorial gate",
                test_factorial_gate_is_exact_and_repeatable,
            },
            {
                "residual conflict gate",
                test_residual_conflict_gate_is_exact_and_repeatable,
            },
            {
                "residual conflict mutations",
                test_residual_conflict_gate_classifies_mutations,
            },
        };
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cout
                << "[FAIL] " << name << ": "
                << error.what() << '\n';
        }
    }
    std::cout << passed << '/' << tests.size()
              << " tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
