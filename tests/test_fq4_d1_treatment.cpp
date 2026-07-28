#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_d1_treatment.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace field = old_school::fq4_d1_field_gate;
namespace treatment = old_school::fq4_d1_treatment;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool same_double(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

std::vector<double> residuals(
    const std::vector<std::vector<double>>& options,
    const std::shared_ptr<const old_school::LearnedModel>& model) {
    const std::vector<double> logits =
        old_school::learned_policy_head_logits(
            options,
            old_school::LearnedPolicyDecisionKind::Priority,
            model);
    double total = 0.0;
    for (const double logit : logits) {
        total += logit;
    }
    const double mean =
        total / static_cast<double>(logits.size());
    std::vector<double> result;
    result.reserve(logits.size());
    for (const double logit : logits) {
        result.push_back(
            field::kParentResidualWeight *
            std::tanh(logit - mean));
    }
    return result;
}

std::vector<double> combined(
    const std::vector<double>& base,
    const std::vector<double>& policy_residuals) {
    std::vector<double> result;
    result.reserve(base.size());
    for (std::size_t index = 0;
         index < base.size(); ++index) {
        result.push_back(
            base[index] + policy_residuals[index]);
    }
    return result;
}

struct ModelFixture {
    std::shared_ptr<const old_school::LearnedModel> parent;
    std::shared_ptr<const old_school::LearnedModel> candidate;
    std::vector<std::vector<double>> options;
    std::vector<std::string> descriptors;
};

const ModelFixture& models() {
    static const ModelFixture fixture = [] {
        ModelFixture result;
        result.parent =
            old_school::train_learned_value_champion(
                1, 0xF004D100ULL);
        const old_school::GameState state =
            old_school::white_lock_plan_diagnostic_state();
        const auto actions =
            old_school::legal_priority_actions(
                state, 0, true);
        if (actions.size() < 3) {
            throw std::runtime_error(
                "treatment fixture needs three Priority actions");
        }
        const std::size_t count =
            std::min<std::size_t>(actions.size(), 6);
        result.options.reserve(count);
        result.descriptors.reserve(count);
        for (std::size_t index = 0;
             index < count; ++index) {
            result.options.push_back(
                old_school::
                    learned_priority_policy_features(
                        state, 0, actions[index], true,
                        old_school::TurnPhase::FirstMain,
                        1));
            result.descriptors.push_back(
                "action-" +
                std::to_string(
                    static_cast<unsigned>(index)));
        }
        std::vector<double> targets(count, 0.0);
        targets.back() = 1.0;
        const old_school::LearnedValuePriorityTrainingExample
            example{
                .options = result.options,
                .base_scores =
                    std::vector<double>(count, 0.0),
                .target_probabilities = targets,
                .weight = 1.0,
            };
        result.candidate =
            old_school::update_learned_value_priority_head(
                result.parent, {example},
                {
                    .batch_size = 64,
                    .epochs = 16,
                    .learning_rate = 0.01,
                    .beta1 = 0.9,
                    .beta2 = 0.999,
                    .epsilon = 1.0e-8,
                    .global_gradient_norm_clip = 5.0,
                    .seed = 0xD100ULL,
                    .residual_weight = 0.10,
                    .policy_temperature = 0.10,
                });
        const auto parent_components =
            old_school::learned_model_component_fingerprints(
                result.parent);
        const auto candidate_components =
            old_school::learned_model_component_fingerprints(
                result.candidate);
        if (parent_components.priority ==
                candidate_components.priority ||
            parent_components.critic !=
                candidate_components.critic ||
            parent_components.attack !=
                candidate_components.attack ||
            parent_components.block !=
                candidate_components.block ||
            parent_components.damage_order !=
                candidate_components.damage_order) {
            throw std::runtime_error(
                "treatment fixture candidate is not Priority-only");
        }
        return result;
    }();
    return fixture;
}

treatment::TreatmentRow make_row(
    std::size_t ordinal, old_school::DeckId deck,
    std::vector<double> base_scores,
    std::vector<bool> dominated,
    bool variable_samples = true) {
    const ModelFixture& fixture = models();
    const std::size_t count = fixture.options.size();
    if (base_scores.size() != count ||
        dominated.size() != count) {
        throw std::runtime_error(
            "invalid synthetic treatment row shape");
    }
    treatment::TreatmentRow row{
        .stable_id =
            old_school::artifact_integrity::sha256_string(
                "synthetic-root-" +
                std::to_string(ordinal)),
        .physical_game_id =
            old_school::artifact_integrity::sha256_string(
                "synthetic-game-" +
                std::to_string(ordinal)),
        .owner_deck = deck,
        .canonical_descriptors =
            fixture.descriptors,
        .robustly_pass_dominated =
            std::move(dominated),
        .base_scores = std::move(base_scores),
    };
    row.options.reserve(count);
    row.base_samples.reserve(count);
    for (std::size_t action = 0;
         action < count; ++action) {
        row.options.push_back({
            .descriptor =
                row.canonical_descriptors[action],
            .visible_tensor = fixture.options[action],
            .hidden_tensor = fixture.options[action],
        });
        std::vector<double> samples(
            field::kDominanceWorlds,
            row.base_scores[action]);
        if (variable_samples) {
            for (std::size_t world = 0;
                 world < samples.size(); ++world) {
                const double centered =
                    static_cast<double>(world) - 3.5;
                samples[world] +=
                    centered * 0.002 *
                    static_cast<double>(action + 1);
            }
        }
        row.base_samples.push_back(std::move(samples));
    }
    row.parent_residuals =
        residuals(fixture.options, fixture.parent);
    row.parent_combined_scores =
        combined(
            row.base_scores,
            row.parent_residuals);
    row.parent_class =
        field::classify_parent({
            .canonical_descriptors =
                row.canonical_descriptors,
            .base_scores = row.base_scores,
            .combined_scores =
                row.parent_combined_scores,
            .base_samples = row.base_samples,
            .robustly_pass_dominated =
                row.robustly_pass_dominated,
        });
    if (!row.parent_class.valid) {
        throw std::runtime_error(
            "synthetic parent class is invalid");
    }
    return row;
}

void refresh_parent_derivation(
    treatment::TreatmentRow& row) {
    std::vector<std::vector<double>> canonical_options;
    canonical_options.reserve(
        row.canonical_descriptors.size());
    for (const std::string& descriptor :
         row.canonical_descriptors) {
        const auto found = std::find_if(
            row.options.begin(), row.options.end(),
            [&](const treatment::LabeledOption& option) {
                return option.descriptor == descriptor;
            });
        if (found == row.options.end()) {
            throw std::runtime_error(
                "cannot refresh malformed treatment row");
        }
        canonical_options.push_back(
            found->visible_tensor);
    }
    row.parent_residuals =
        residuals(canonical_options, models().parent);
    row.parent_combined_scores =
        combined(
            row.base_scores,
            row.parent_residuals);
    row.parent_class =
        field::classify_parent({
            .canonical_descriptors =
                row.canonical_descriptors,
            .base_scores = row.base_scores,
            .combined_scores =
                row.parent_combined_scores,
            .base_samples = row.base_samples,
            .robustly_pass_dominated =
                row.robustly_pass_dominated,
        });
    if (!row.parent_class.valid) {
        throw std::runtime_error(
            "refreshed treatment row is invalid");
    }
}

std::pair<std::size_t, std::size_t>
candidate_improved_pair() {
    const ModelFixture& fixture = models();
    const auto parent =
        residuals(fixture.options, fixture.parent);
    const auto candidate =
        residuals(fixture.options, fixture.candidate);
    for (std::size_t first = 0;
         first < parent.size(); ++first) {
        for (std::size_t second = 0;
             second < parent.size(); ++second) {
            if (first == second) {
                continue;
            }
            const double parent_difference =
                parent[first] - parent[second];
            const double candidate_difference =
                candidate[first] - candidate[second];
            if (parent_difference >
                candidate_difference) {
                return {first, second};
            }
        }
    }
    throw std::runtime_error(
        "Priority update did not change a relative residual");
}

treatment::TreatmentRow make_full_repair_row(
    std::size_t ordinal, old_school::DeckId deck) {
    const ModelFixture& fixture = models();
    const auto [dominated_index, safe_index] =
        candidate_improved_pair();
    const auto parent =
        residuals(fixture.options, fixture.parent);
    const auto candidate =
        residuals(fixture.options, fixture.candidate);
    const double parent_difference =
        parent[dominated_index] - parent[safe_index];
    const double candidate_difference =
        candidate[dominated_index] -
        candidate[safe_index];
    const double base_difference =
        -(parent_difference + candidate_difference) /
        2.0;
    std::vector<double> base(
        fixture.options.size(), -10.0);
    base[dominated_index] =
        base_difference / 2.0;
    base[safe_index] =
        -base_difference / 2.0;
    std::vector<bool> dominated(
        fixture.options.size(), false);
    dominated[dominated_index] = true;
    treatment::TreatmentRow row =
        make_row(
            ordinal, deck, std::move(base),
            std::move(dominated), false);
    if (!row.parent_class.high_confidence_unsafe()) {
        throw std::runtime_error(
            "repair fixture parent is not high-confidence unsafe");
    }
    return row;
}

treatment::TreatmentRow make_safe_row(
    std::size_t ordinal, old_school::DeckId deck) {
    const ModelFixture& fixture = models();
    const auto [dominated_index, safe_index] =
        candidate_improved_pair();
    std::vector<double> base(
        fixture.options.size(), -10.0);
    base[dominated_index] = -10.0;
    base[safe_index] = 10.0;
    std::vector<bool> dominated(
        fixture.options.size(), false);
    dominated[dominated_index] = true;
    treatment::TreatmentRow row =
        make_row(
            ordinal, deck, std::move(base),
            std::move(dominated), false);
    if (row.parent_class.classification !=
        field::ParentClass::Safe) {
        throw std::runtime_error(
            "safe fixture parent is not Safe");
    }
    return row;
}

std::vector<treatment::TreatmentRow> passing_rows() {
    std::vector<treatment::TreatmentRow> rows;
    for (std::size_t index = 0; index < 5; ++index) {
        rows.push_back(
            make_full_repair_row(
                index,
                index < 3
                    ? old_school::DeckId::Green
                    : old_school::DeckId::Blue));
    }
    for (std::size_t index = 0; index < 8; ++index) {
        rows.push_back(
            make_safe_row(
                100 + index,
                old_school::DeckId::Red));
    }
    return rows;
}

treatment::GateInput passing_gate_input() {
    treatment::GateInput input{
        .full_repairs = 5,
        .distinct_repair_games = 5,
        .distinct_repair_decks = 2,
        .severity_regressions = 0,
        .candidate_class2_sigma_mass =
            treatment::kParentClass2SigmaMass,
    };
    input.decks[static_cast<std::size_t>(
        old_school::DeckId::Green)]
        .candidate.classes = {8, 1, 2, 2};
    input.decks[static_cast<std::size_t>(
        old_school::DeckId::Red)]
        .candidate.classes = {8, 0, 0, 0};
    input.decks[static_cast<std::size_t>(
        old_school::DeckId::Blue)]
        .candidate.classes = {35, 1, 6, 2};
    input.decks[static_cast<std::size_t>(
        old_school::DeckId::White)]
        .candidate.classes = {1, 5, 0, 0};
    input.decks[static_cast<std::size_t>(
        old_school::DeckId::RUAggro)]
        .candidate.classes = {36, 2, 5, 0};
    input.pooled.candidate.classes =
        {88, 9, 13, 4};
    return input;
}

void test_pure_evaluator_reproduces_parent_and_controls() {
    std::vector<double> base(
        models().options.size(), 0.0);
    for (std::size_t index = 0;
         index < base.size(); ++index) {
        base[index] =
            0.01 * static_cast<double>(index);
    }
    std::vector<bool> dominated(
        models().options.size(), false);
    dominated.front() = true;
    const treatment::TreatmentRow row =
        make_row(
            500, old_school::DeckId::Green,
            std::move(base), std::move(dominated));
    const treatment::TreatmentReport report =
        treatment::testing::evaluate(
            {row}, models().parent,
            models().candidate);
    expect(report.infrastructure_valid(),
           "valid stripped-row evaluation failed infrastructure");
    expect(!report.passed(),
           "one-row evaluation unexpectedly passed field gate");
    expect(report.roots.size() == 1,
           "valid evaluator omitted its root");
    expect(report.parent_reproduced,
           "parent residual/class was not reproduced");
    expect(report.hidden_bit_identical,
           "hidden tensor control failed");
    expect(report.reverse_order_bit_identical,
           "reverse labeled-option control failed");
    expect(report.repeated_evaluation_bit_identical,
           "repeat tensor evaluation failed");
    expect(report.treatment_accounting.zero(),
           "pure tensor evaluator reported rollout work");
    expect(
        report.treatment_input_sha256 ==
            treatment::testing::treatment_input_sha256(
                report.parent_reconstruction, {row},
                models().parent),
        "report input digest omitted rederived parent anchors");
    expect(
        treatment::classify_exit(report) ==
            treatment::ExitClassification::
                ScientificReject,
        "valid gate miss did not exit 1");

    auto reversed = row;
    std::reverse(
        reversed.options.begin(),
        reversed.options.end());
    const treatment::TreatmentReport reordered =
        treatment::testing::evaluate(
            {reversed}, models().parent,
            models().candidate);
    expect(reordered.infrastructure_valid(),
           "reversed labeled input failed");
    expect(reordered.treatment_input_sha256 ==
               report.treatment_input_sha256,
           "canonical input digest depends on option order");
    expect(reordered.evidence_sha256 ==
               report.evidence_sha256,
           "canonical evidence depends on option order");
}

void test_full_gate_pass_and_exit_precedence() {
    const treatment::TreatmentReport report =
        treatment::testing::evaluate(
            passing_rows(), models().parent,
            models().candidate);
    expect(report.infrastructure_valid(),
           "synthetic pass report failed infrastructure");
    expect(report.passed(),
           "5/5/2 plus Red protection did not pass");
    expect(report.full_repairs == 5,
           "full-repair count drifted");
    expect(report.distinct_repair_games == 5,
           "repair-game coverage drifted");
    expect(report.distinct_repair_decks == 2,
           "repair-deck coverage drifted");
    expect(
        report.decks[static_cast<std::size_t>(
            old_school::DeckId::Red)]
                .candidate.classes ==
            std::array<std::size_t, 4>{8, 0, 0, 0},
        "Red did not remain 8/0/0/0");
    expect(
        treatment::classify_exit(report) ==
            treatment::ExitClassification::Pass,
        "complete synthetic gate did not exit 0");

    auto invalid = report;
    invalid.treatment_accounting.search_calls = 1;
    expect(
        treatment::classify_exit(invalid) ==
            treatment::ExitClassification::
                InfrastructureFailure,
        "accounting mutation did not take exit-2 precedence");
}

void test_gate_boundaries_are_conjunctive() {
    const treatment::GateInput passing =
        passing_gate_input();
    expect(
        treatment::testing::
            evaluate_scientific_gates(passing)
                .passed(),
        "complete aggregate gate did not pass");

    const auto fails =
        [&](const std::function<void(
                      treatment::GateInput&)>& mutate,
            const std::function<bool(
                const treatment::ScientificGates&)>&
                field) {
            treatment::GateInput changed = passing;
            mutate(changed);
            const auto gates =
                treatment::testing::
                    evaluate_scientific_gates(changed);
            expect(!field(gates),
                   "aggregate boundary mutation was ignored");
            expect(!gates.passed(),
                   "conjunctive gate passed a failed boundary");
        };
    fails(
        [](auto& input) {
            input.full_repairs = 4;
        },
        [](const auto& gates) {
            return gates.repair_root_floor;
        });
    fails(
        [](auto& input) {
            input.distinct_repair_games = 4;
        },
        [](const auto& gates) {
            return gates.repair_game_floor;
        });
    fails(
        [](auto& input) {
            input.distinct_repair_decks = 1;
        },
        [](const auto& gates) {
            return gates.repair_deck_floor;
        });
    fails(
        [](auto& input) {
            input.severity_regressions = 1;
        },
        [](const auto& gates) {
            return gates.zero_severity_regressions;
        });
    fails(
        [](auto& input) {
            input.decks[0].candidate.classes =
                {6, 2, 3, 2};
        },
        [](const auto& gates) {
            return gates.per_deck_nonregression;
        });
    fails(
        [](auto& input) {
            input.decks[1].candidate.classes =
                {7, 0, 0, 1};
        },
        [](const auto& gates) {
            return gates.red_protected;
        });
    fails(
        [](auto& input) {
            input.pooled.candidate.classes =
                {87, 9, 14, 4};
        },
        [](const auto& gates) {
            return gates.pooled_high_confidence_bound;
        });
    fails(
        [](auto& input) {
            input.pooled.candidate.classes =
                {85, 9, 13, 7};
        },
        [](const auto& gates) {
            return gates.pooled_unsafe_bound;
        });
    fails(
        [](auto& input) {
            input.pooled.candidate.classes =
                {86, 11, 11, 6};
        },
        [](const auto& gates) {
            return gates.pooled_class1_bound;
        });
    fails(
        [](auto& input) {
            input.candidate_class2_sigma_mass =
                std::nextafter(
                    treatment::kParentClass2SigmaMass,
                    std::numeric_limits<double>::
                        infinity());
        },
        [](const auto& gates) {
            return gates.class2_sigma_nonregression;
        });
    for (const double invalid_sigma : {
             -1.0,
             std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(),
         }) {
        fails(
            [invalid_sigma](auto& input) {
                input.candidate_class2_sigma_mass =
                    invalid_sigma;
            },
            [](const auto& gates) {
                return gates.class2_sigma_nonregression;
            });
    }
    fails(
        [](auto& input) {
            input.pooled.candidate.classes =
                {87, 10, 13, 4};
        },
        [](const auto& gates) {
            return gates.strict_registered_improvement;
        });
    auto sigma_strict = passing;
    sigma_strict.pooled.candidate.classes =
        {88, 10, 12, 4};
    sigma_strict.candidate_class2_sigma_mass =
        std::nextafter(
            treatment::kParentClass2SigmaMass, 0.0);
    expect(
        treatment::testing::
            evaluate_scientific_gates(sigma_strict)
                .passed(),
        "strict sigma improvement did not satisfy disjunction");
    sigma_strict.candidate_class2_sigma_mass = -1.0;
    expect(
        !treatment::testing::
             evaluate_scientific_gates(sigma_strict)
             .strict_registered_improvement,
        "negative sigma satisfied strict-improvement branch");
}

void test_malformed_rows_fail_closed() {
    treatment::TreatmentRow row =
        make_safe_row(
            600, old_school::DeckId::Green);
    const auto rejects =
        [&](const treatment::TreatmentRow& malformed) {
            const auto report =
                treatment::testing::evaluate(
                    {malformed}, models().parent,
                    models().candidate);
            expect(!report.infrastructure_valid(),
                   "malformed row passed infrastructure");
            expect(
                treatment::classify_exit(report) ==
                    treatment::ExitClassification::
                        InfrastructureFailure,
                "malformed row did not exit 2");
        };

    auto changed = row;
    changed.options[1].descriptor =
        changed.options[0].descriptor;
    rejects(changed);
    changed = row;
    changed.options[0].descriptor = "unknown";
    rejects(changed);
    changed = row;
    changed.options[0].visible_tensor.pop_back();
    changed.options[0].hidden_tensor.pop_back();
    rejects(changed);
    changed = row;
    changed.options[0].hidden_tensor[0] =
        std::nextafter(
            changed.options[0].hidden_tensor[0],
            std::numeric_limits<double>::infinity());
    rejects(changed);
    changed = row;
    changed.base_samples[0].pop_back();
    rejects(changed);
    changed = row;
    std::fill(
        changed.robustly_pass_dominated.begin(),
        changed.robustly_pass_dominated.end(), true);
    rejects(changed);
    changed = row;
    changed.base_scores[0] =
        std::numeric_limits<double>::quiet_NaN();
    rejects(changed);
    changed = row;
    changed.parent_residuals[0] =
        std::nextafter(
            changed.parent_residuals[0],
            std::numeric_limits<double>::infinity());
    rejects(changed);

    const auto unchanged_head =
        treatment::testing::evaluate(
            {row}, models().parent, models().parent);
    expect(!unchanged_head.infrastructure_valid(),
           "unchanged Priority component was accepted");
    const auto unrelated_model =
        old_school::train_learned_value_champion(
            1, 0xF004D101ULL);
    const auto broad_change =
        treatment::testing::evaluate(
            {row}, models().parent, unrelated_model);
    expect(!broad_change.infrastructure_valid(),
           "non-Priority component mutation was accepted");

    treatment::TreatmentRow malformed_last =
        make_safe_row(
            601, old_school::DeckId::Blue);
    malformed_last.parent_residuals[0] =
        std::nextafter(
            malformed_last.parent_residuals[0],
            std::numeric_limits<double>::infinity());
    const auto two_phase =
        treatment::testing::evaluate(
            {row, malformed_last},
            models().parent, models().candidate);
    expect(
        !two_phase.infrastructure_valid() &&
            two_phase.roots.empty() &&
            two_phase.treatment_input_sha256.empty() &&
            !two_phase.parent_reproduced,
        "candidate phase began before every parent row reproduced");
}

void test_candidate_pair_change_recomputes_se() {
    const ModelFixture& fixture = models();
    const auto parent_residual =
        residuals(fixture.options, fixture.parent);
    const auto candidate_residual =
        residuals(fixture.options, fixture.candidate);
    std::size_t first = 0;
    std::size_t second = 0;
    bool found = false;
    for (std::size_t left = 0;
         left < fixture.options.size() && !found; ++left) {
        for (std::size_t right = 0;
             right < fixture.options.size(); ++right) {
            if (left != right &&
                parent_residual[left] -
                        parent_residual[right] >
                    candidate_residual[left] -
                        candidate_residual[right]) {
                first = left;
                second = right;
                found = true;
                break;
            }
        }
    }
    expect(found, "no candidate pair change is available");
    std::size_t nondominated = 0;
    while (nondominated == first ||
           nondominated == second) {
        ++nondominated;
    }
    const double parent_difference =
        parent_residual[first] -
        parent_residual[second];
    const double candidate_difference =
        candidate_residual[first] -
        candidate_residual[second];
    const double base_difference =
        -(parent_difference + candidate_difference) /
        2.0;
    std::vector<double> base(
        fixture.options.size(), -10.0);
    base[first] = base_difference / 2.0;
    base[second] = -base_difference / 2.0;
    base[nondominated] = -1.0;
    std::vector<bool> dominated(
        fixture.options.size(), false);
    dominated[first] = true;
    dominated[second] = true;
    treatment::TreatmentRow row =
        make_row(
            700, old_school::DeckId::Blue,
            std::move(base), std::move(dominated));
    for (std::size_t world = 0;
         world < field::kDominanceWorlds; ++world) {
        row.base_samples[first][world] =
            static_cast<double>(world) * 0.001;
        row.base_samples[second][world] =
            (world % 2 == 0 ? 1.0 : -1.0) * 0.1;
        row.base_samples[nondominated][world] = 0.0;
    }
    row.parent_class =
        field::classify_parent({
            .canonical_descriptors =
                row.canonical_descriptors,
            .base_scores = row.base_scores,
            .combined_scores =
                row.parent_combined_scores,
            .base_samples = row.base_samples,
            .robustly_pass_dominated =
                row.robustly_pass_dominated,
        });
    expect(row.parent_class.valid,
           "pair-change parent class is invalid");
    const auto report =
        treatment::testing::evaluate(
            {row}, fixture.parent,
            fixture.candidate);
    expect(report.infrastructure_valid(),
           "pair-change row failed infrastructure");
    const auto& result = report.roots.front();
    expect(
        result.parent_class.best_dominated_index !=
            result.candidate_class.best_dominated_index,
        "candidate did not independently select its best pair");
    expect(
        !same_double(
            result.parent_class.paired_standard_error,
            result.candidate_class
                .paired_standard_error),
        "candidate reused parent SE after pair index changed");
}

void test_explicit_severity_regressions() {
    const ModelFixture& fixture = models();
    const auto parent_residual =
        residuals(fixture.options, fixture.parent);
    const auto candidate_residual =
        residuals(fixture.options, fixture.candidate);
    std::size_t dominated_index = 0;
    std::size_t nondominated_index = 0;
    bool found = false;
    for (std::size_t first = 0;
         first < fixture.options.size() && !found; ++first) {
        for (std::size_t second = 0;
             second < fixture.options.size(); ++second) {
            if (first == second) {
                continue;
            }
            const double parent_difference =
                parent_residual[first] -
                parent_residual[second];
            const double candidate_difference =
                candidate_residual[first] -
                candidate_residual[second];
            if (candidate_difference >
                parent_difference) {
                dominated_index = first;
                nondominated_index = second;
                found = true;
                break;
            }
        }
    }
    expect(found, "no worsening residual pair is available");
    const double parent_difference =
        parent_residual[dominated_index] -
        parent_residual[nondominated_index];
    const double candidate_difference =
        candidate_residual[dominated_index] -
        candidate_residual[nondominated_index];
    const double shift =
        candidate_difference - parent_difference;
    expect(shift > 0.0,
           "severity fixture has no positive candidate shift");

    const auto fixed_pair_row =
        [&](std::size_t ordinal, double parent_margin,
            double paired_se) {
            std::vector<double> base(
                fixture.options.size(), -10.0);
            const double base_difference =
                parent_margin - parent_difference;
            base[dominated_index] =
                base_difference / 2.0;
            base[nondominated_index] =
                -base_difference / 2.0;
            std::vector<bool> dominated(
                fixture.options.size(), false);
            dominated[dominated_index] = true;
            treatment::TreatmentRow row =
                make_row(
                    ordinal, old_school::DeckId::Green,
                    std::move(base),
                    std::move(dominated), false);
            if (paired_se > 0.0) {
                const double amplitude =
                    paired_se * std::sqrt(7.0);
                for (std::size_t world = 0;
                     world < field::kDominanceWorlds;
                     ++world) {
                    row.base_samples[dominated_index][world] =
                        world % 2 == 0
                            ? amplitude
                            : -amplitude;
                    row.base_samples[nondominated_index][world] =
                        0.0;
                }
            }
            row.parent_class =
                field::classify_parent({
                    .canonical_descriptors =
                        row.canonical_descriptors,
                    .base_scores = row.base_scores,
                    .combined_scores =
                        row.parent_combined_scores,
                    .base_samples = row.base_samples,
                    .robustly_pass_dominated =
                        row.robustly_pass_dominated,
                });
            expect(row.parent_class.valid,
                   "severity parent class is invalid");
            return row;
        };

    const treatment::TreatmentRow safe_to_unsafe =
        fixed_pair_row(710, -shift / 2.0, 0.0);
    auto report =
        treatment::testing::evaluate(
            {safe_to_unsafe}, fixture.parent,
            fixture.candidate);
    expect(report.infrastructure_valid(),
           "Safe-to-unsafe transition failed infrastructure");
    expect(
        report.roots.front().parent_class.classification ==
                field::ParentClass::Safe &&
            report.roots.front()
                    .candidate_class.classification !=
                field::ParentClass::Safe &&
            report.roots.front().severity_regression,
        "Safe-to-unsafe severity regression was not detected");

    const double target_se = shift / 4.0;
    const treatment::TreatmentRow class3_to_class2 =
        fixed_pair_row(
            711, 2.5 * target_se, target_se);
    report =
        treatment::testing::evaluate(
            {class3_to_class2}, fixture.parent,
            fixture.candidate);
    expect(report.infrastructure_valid(),
           "Class3-to-Class2 transition failed infrastructure");
    expect(
        report.roots.front().parent_class.classification ==
                field::ParentClass::Class3 &&
            report.roots.front()
                    .candidate_class.classification ==
                field::ParentClass::Class2 &&
            report.roots.front().severity_regression,
        "Class3-to-Class2 severity regression was not detected");

    const auto [first, second] =
        candidate_improved_pair();
    std::size_t safe_index = 0;
    while (safe_index == first ||
           safe_index == second) {
        ++safe_index;
    }
    const double first_parent_difference =
        parent_residual[first] -
        parent_residual[second];
    const double first_candidate_difference =
        candidate_residual[first] -
        candidate_residual[second];
    const double pair_base_difference =
        -(first_parent_difference +
          first_candidate_difference) /
        2.0;
    std::vector<double> base(
        fixture.options.size(), -10.0);
    base[first] = pair_base_difference / 2.0;
    base[second] = -pair_base_difference / 2.0;
    base[safe_index] = -1.0;
    std::vector<bool> dominated(
        fixture.options.size(), false);
    dominated[first] = true;
    dominated[second] = true;
    treatment::TreatmentRow class2_to_class1 =
        make_row(
            712, old_school::DeckId::Blue,
            std::move(base), std::move(dominated),
            false);
    for (std::size_t world = 0;
         world < field::kDominanceWorlds; ++world) {
        class2_to_class1.base_samples[first][world] =
            (world % 2 == 0 ? 1.0 : -1.0) *
            0.001;
        class2_to_class1.base_samples[second][world] =
            -(
                candidate_residual[second] -
                candidate_residual[safe_index]);
        class2_to_class1.base_samples[safe_index][world] =
            0.0;
    }
    class2_to_class1.parent_class =
        field::classify_parent({
            .canonical_descriptors =
                class2_to_class1.canonical_descriptors,
            .base_scores =
                class2_to_class1.base_scores,
            .combined_scores =
                class2_to_class1
                    .parent_combined_scores,
            .base_samples =
                class2_to_class1.base_samples,
            .robustly_pass_dominated =
                class2_to_class1
                    .robustly_pass_dominated,
        });
    expect(
        class2_to_class1.parent_class.classification ==
            field::ParentClass::Class2,
        "pair-change severity parent is not Class2");
    report =
        treatment::testing::evaluate(
            {class2_to_class1}, fixture.parent,
            fixture.candidate);
    expect(report.infrastructure_valid(),
           "Class2-to-Class1 transition failed infrastructure");
    expect(
        report.roots.front().candidate_class.classification ==
                field::ParentClass::Class1 &&
            report.roots.front().severity_regression,
        "Class2-to-Class1 severity regression was not detected");
}

void test_digest_framing_and_mutations() {
    treatment::TreatmentRow row =
        make_safe_row(
            800, old_school::DeckId::Green);
    treatment::ParentReconstructionSummary parent;
    parent.artifact_sha256 = std::string(64, 'a');
    parent.model_fingerprint = std::string(64, 'b');
    parent.model_components = {
        .critic = std::string(64, 'c'),
        .priority = std::string(64, 'd'),
        .attack = std::string(64, 'e'),
        .block = std::string(64, 'f'),
        .damage_order = std::string(64, '1'),
    };
    const std::string input =
        treatment::testing::treatment_input_sha256(
            parent, {row}, models().parent);
    expect(input.size() == 64,
           "input digest is not SHA-256");
    expect(
        input ==
            "2918653418797588e1294246e102693a8e573b132f75a0302f8bdaeca4510c86",
        "input serializer golden hash drifted");
    auto reversed = row;
    std::reverse(
        reversed.options.begin(),
        reversed.options.end());
    expect(
        treatment::testing::treatment_input_sha256(
            parent, {reversed}, models().parent) == input,
        "input digest ignored canonical option order");
    auto changed = row;
    changed.base_samples[0][0] =
        std::nextafter(
            changed.base_samples[0][0],
            std::numeric_limits<double>::infinity());
    refresh_parent_derivation(changed);
    expect(
        treatment::testing::treatment_input_sha256(
            parent, {changed}, models().parent) != input,
        "input digest ignored sample-bit mutation");
    changed = row;
    changed.canonical_descriptors[0] =
        "action-0-renamed";
    changed.options[0].descriptor =
        "action-0-renamed";
    refresh_parent_derivation(changed);
    expect(
        treatment::testing::treatment_input_sha256(
            parent, {changed}, models().parent) != input,
        "input digest ignored descriptor mutation");
    changed = row;
    changed.options[0].visible_tensor[0] =
        std::nextafter(
            changed.options[0].visible_tensor[0],
            std::numeric_limits<double>::infinity());
    changed.options[0].hidden_tensor[0] =
        changed.options[0].visible_tensor[0];
    refresh_parent_derivation(changed);
    expect(
        treatment::testing::treatment_input_sha256(
            parent, {changed}, models().parent) != input,
        "input digest ignored tensor-bit mutation");
    changed = row;
    std::swap(
        changed.robustly_pass_dominated[0],
        changed.robustly_pass_dominated[1]);
    refresh_parent_derivation(changed);
    expect(
        treatment::testing::treatment_input_sha256(
            parent, {changed}, models().parent) != input,
        "input digest ignored dominance-mask mutation");
    auto positive_zero = row;
    positive_zero.base_scores[0] = 0.0;
    refresh_parent_derivation(positive_zero);
    const std::string positive_zero_digest =
        treatment::testing::treatment_input_sha256(
            parent, {positive_zero}, models().parent);
    changed = positive_zero;
    changed.base_scores[0] = -0.0;
    refresh_parent_derivation(changed);
    expect(
        treatment::testing::treatment_input_sha256(
            parent, {changed}, models().parent) !=
                positive_zero_digest,
        "input digest ignored signed-zero mutation");
    auto parent_changed = parent;
    parent_changed.model_fingerprint[0] = 'c';
    expect(
        treatment::testing::treatment_input_sha256(
            parent_changed, {row}, models().parent) != input,
        "input digest ignored parent fingerprint mutation");
    parent_changed = parent;
    parent_changed.model_components.priority[0] = 'e';
    expect(
        treatment::testing::treatment_input_sha256(
            parent_changed, {row}, models().parent) != input,
        "input digest ignored component mutation");
    bool rejected_wrong_parent = false;
    try {
        static_cast<void>(
            treatment::testing::treatment_input_sha256(
                parent, {row}, models().candidate));
    } catch (const std::invalid_argument&) {
        rejected_wrong_parent = true;
    }
    expect(
        rejected_wrong_parent,
        "input serializer trusted stored parent anchors");

    const treatment::TreatmentReport report =
        treatment::testing::evaluate(
            {row}, models().parent,
            models().candidate);
    expect(report.infrastructure_valid(),
           "digest fixture report is invalid");
    expect(
        report.evidence_sha256 ==
            "245a299541c556b79c997bc74fcc1d043b562e588a3270487a83de65ea185153",
        "evidence serializer golden hash drifted");
    expect(
        treatment::testing::treatment_evidence_sha256(
            report) == report.evidence_sha256,
        "evidence digest does not reproduce");
    auto evidence_changed = report;
    evidence_changed.gates.repair_root_floor =
        !evidence_changed.gates.repair_root_floor;
    expect(
        treatment::testing::treatment_evidence_sha256(
            evidence_changed) != report.evidence_sha256,
        "evidence digest ignored gate mutation");
    evidence_changed = report;
    evidence_changed.candidate_fingerprint[0] =
        evidence_changed.candidate_fingerprint[0] == 'a'
            ? 'b'
            : 'a';
    expect(
        treatment::testing::treatment_evidence_sha256(
            evidence_changed) != report.evidence_sha256,
        "evidence digest ignored candidate fingerprint mutation");
    evidence_changed = report;
    evidence_changed.candidate_components.priority[0] =
        evidence_changed.candidate_components.priority[0] ==
                'a'
            ? 'b'
            : 'a';
    expect(
        treatment::testing::treatment_evidence_sha256(
            evidence_changed) != report.evidence_sha256,
        "evidence digest ignored candidate component mutation");
    evidence_changed = report;
    evidence_changed.timings.total_seconds += 1.0;
    expect(
        treatment::testing::treatment_evidence_sha256(
            evidence_changed) == report.evidence_sha256,
        "evidence digest included runtime timing");
    evidence_changed = report;
    evidence_changed.scientific_failures.push_back(
        "diagnostic wording");
    expect(
        treatment::testing::treatment_evidence_sha256(
            evidence_changed) == report.evidence_sha256,
        "evidence digest included diagnostic text");
}

field::ScoredRoot scored_root_from(
    const treatment::TreatmentRow& row) {
    field::ScoredRoot root{
        .manifest = {
            .locator = {
                .source_block = 0,
                .source_seed_base = 790,
                .schedule_index = 3,
                .game_seed = 123456,
                .owner_seat = 0,
                .trace_ordinal = 7,
            },
            .owner_deck = row.owner_deck,
            .opponent_deck = old_school::DeckId::Red,
            .stable_id = row.stable_id,
            .information_action_fingerprint =
                std::string(64, 'a'),
            .canonical_descriptors =
                row.canonical_descriptors,
            .pass_index = 0,
        },
        .dominance = {
            .pass_index = 0,
            .strict_world_counts =
                std::vector<std::size_t>(
                    row.options.size(), 0),
            .robustly_pass_dominated =
                row.robustly_pass_dominated,
            .shape_valid = true,
        },
        .base_score = {
            .stable_id = row.stable_id,
            .decision_kind =
                old_school::probes::DecisionKind::Priority,
            .score_mode =
                old_school::oc1_action_scoring::ScoreMode::
                    ProductionPrioritySearch,
        },
        .base_scores = row.base_scores,
        .neutral_priority_options = {},
        .hidden_neutral_priority_options = {},
        .residuals = row.parent_residuals,
        .combined_scores =
            row.parent_combined_scores,
        .parent_class = row.parent_class,
        .hidden_replay_bit_identical = true,
        .hidden_feature_bits_identical = true,
    };
    for (std::size_t index = 0;
         index < row.options.size(); ++index) {
        root.base_score.actions.push_back({
            .descriptor =
                row.canonical_descriptors[index],
            .raw_samples = row.base_samples[index],
            .raw_score = row.base_scores[index],
        });
        root.neutral_priority_options.push_back(
            row.options[index].visible_tensor);
        root.hidden_neutral_priority_options.push_back(
            row.options[index].hidden_tensor);
    }
    double maximum = row.base_scores.front();
    for (const double score : row.base_scores) {
        maximum = std::max(maximum, score);
    }
    for (std::size_t index = 0;
         index < row.base_scores.size(); ++index) {
        if (row.base_scores[index] == maximum) {
            root.base_exact_support.push_back(
                row.canonical_descriptors[index]);
        }
    }
    root.base_score.selected_support =
        root.base_exact_support;
    return root;
}

void test_scored_root_adapter_is_stripped_and_fail_closed() {
    const treatment::TreatmentRow row =
        make_safe_row(
            900, old_school::DeckId::Green);
    const field::ScoredRoot scored =
        scored_root_from(row);
    const treatment::testing::RowAdaptation adapted =
        treatment::testing::strip_scored_roots({scored});
    expect(adapted.valid() && adapted.rows.size() == 1,
           "valid scored root did not strip");
    expect(adapted.rows.front().physical_game_id.size() == 64,
           "physical-game ID is not opaque SHA-256");
    expect(
        adapted.rows.front().physical_game_id.find("790") ==
            std::string::npos,
        "stripped physical-game ID leaked source seed");
    expect(adapted.rows.front().base_samples ==
               row.base_samples,
           "adapter changed frozen base samples");

    auto malformed = scored;
    malformed.hidden_neutral_priority_options[0][0] =
        std::nextafter(
            malformed
                .hidden_neutral_priority_options[0][0],
            std::numeric_limits<double>::infinity());
    expect(
        !treatment::testing::
             strip_scored_roots({malformed})
             .valid(),
        "adapter accepted hidden tensor bit drift");
    malformed = scored;
    malformed.base_score.actions[0].descriptor =
        "wrong";
    expect(
        !treatment::testing::
             strip_scored_roots({malformed})
             .valid(),
        "adapter accepted descriptor/sample misalignment");
    malformed = scored;
    malformed.dominance.pass_index =
        (malformed.manifest.pass_index + 1) %
        malformed.manifest.canonical_descriptors.size();
    expect(
        !treatment::testing::
             strip_scored_roots({malformed})
             .valid(),
        "adapter accepted Pass-index drift");
    malformed = scored;
    std::fill(
        malformed.dominance
            .robustly_pass_dominated.begin(),
        malformed.dominance
            .robustly_pass_dominated.end(),
        true);
    expect(
        !treatment::testing::
             strip_scored_roots({malformed})
             .valid(),
        "adapter accepted malformed dominance mask");
    malformed = scored;
    malformed.base_score.stable_id =
        std::string(64, 'f');
    expect(
        !treatment::testing::
             strip_scored_roots({malformed})
             .valid(),
        "adapter accepted base-score identity drift");
    malformed = scored;
    malformed.base_score.actions[0].raw_score =
        std::nextafter(
            malformed.base_score.actions[0].raw_score,
            std::numeric_limits<double>::infinity());
    expect(
        !treatment::testing::
             strip_scored_roots({malformed})
             .valid(),
        "adapter accepted base-score bit drift");
}

void test_class_and_severity_boundaries() {
    const std::vector<std::string> descriptors{"a", "b"};
    const std::vector<std::vector<double>> zero_samples(
        2,
        std::vector<double>(
            field::kDominanceWorlds, 0.0));
    const auto classify =
        [&](double margin,
            const std::vector<std::vector<double>>& samples) {
            return field::classify_parent({
                .canonical_descriptors = descriptors,
                .base_scores = {margin, 0.0},
                .combined_scores = {margin, 0.0},
                .base_samples = samples,
                .robustly_pass_dominated = {true, false},
            });
        };
    expect(
        classify(-0.001, zero_samples).classification ==
            field::ParentClass::Safe,
        "negative margin is not Safe");
    expect(
        classify(0.001, zero_samples).classification ==
            field::ParentClass::Class1,
        "positive zero-SE margin is not Class1");
    expect(
        classify(0.0, zero_samples).classification ==
            field::ParentClass::Class3,
        "zero margin is not Class3");

    auto noisy = zero_samples;
    for (std::size_t world = 0;
         world < field::kDominanceWorlds; ++world) {
        noisy[0][world] =
            world % 2 == 0 ? 1.0 : -1.0;
    }
    const auto probe = classify(1.0, noisy);
    expect(probe.valid &&
               probe.paired_standard_error > 0.0,
           "nonzero-SE boundary fixture is invalid");
    const double threshold =
        3.0 * probe.paired_standard_error;
    expect(
        classify(
            std::nextafter(
                threshold,
                std::numeric_limits<double>::infinity()),
            noisy)
                .classification ==
            field::ParentClass::Class2,
        "one ULP above three sigma is not Class2");
    expect(
        classify(
            std::nextafter(threshold, 0.0),
            noisy)
                .classification ==
            field::ParentClass::Class3,
        "below-three-sigma boundary is not Class3");
}

} // namespace

int main() {
    const std::vector<
        std::pair<std::string_view, std::function<void()>>>
        tests{
            {
                "pure tensor evaluator controls",
                test_pure_evaluator_reproduces_parent_and_controls,
            },
            {
                "full gate and exit precedence",
                test_full_gate_pass_and_exit_precedence,
            },
            {
                "aggregate gate boundaries",
                test_gate_boundaries_are_conjunctive,
            },
            {
                "malformed rows fail closed",
                test_malformed_rows_fail_closed,
            },
            {
                "candidate pair change recomputes SE",
                test_candidate_pair_change_recomputes_se,
            },
            {
                "explicit severity transitions",
                test_explicit_severity_regressions,
            },
            {
                "digest framing and mutations",
                test_digest_framing_and_mutations,
            },
            {
                "stripped scored-root adapter",
                test_scored_root_adapter_is_stripped_and_fail_closed,
            },
            {
                "class and severity boundaries",
                test_class_and_severity_boundaries,
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
        << " FQ4-D1 treatment tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
