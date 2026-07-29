#include "old_school/action_q_nested_actor_anchor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace aq =
    old_school::action_q_nested_actor_anchor;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void expect_close(
    double actual, double expected, double tolerance,
    std::string_view message) {
    if (!std::isfinite(actual) ||
        std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(std::string(message));
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
    throw std::runtime_error(std::string(message));
}

template <typename Function>
void expect_invalid_batch(
    Function function, std::string_view message) {
    try {
        if (!function().valid(true)) {
            return;
        }
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

old_school::LearnedValuePriorityTrainingExample
training_example(
    std::size_t options, double weight,
    double marker) {
    old_school::LearnedValuePriorityTrainingExample result;
    result.options.reserve(options);
    result.base_scores.reserve(options);
    result.target_probabilities.assign(
        options, 1.0 / static_cast<double>(options));
    for (std::size_t action = 0;
         action < options; ++action) {
        std::vector<double> features(
            aq::g1::kPolicyFeatureCount, 0.0);
        features[0] =
            marker + static_cast<double>(action) / 1000.0;
        result.options.push_back(std::move(features));
        result.base_scores.push_back(
            marker / 10.0 +
            static_cast<double>(action) / 100.0);
    }
    result.weight = weight;
    return result;
}

std::vector<aq::TrainingRow> exact_training_rows() {
    std::vector<aq::TrainingRow> rows;
    rows.reserve(aq::kAnchoredExamples);
    std::size_t teacher_index = 0;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t row = 0; row < 64; ++row) {
            const std::size_t options =
                teacher_index < 63 ? 4 : 3;
            rows.push_back({
                .owner_deck =
                    static_cast<old_school::DeckId>(deck),
                .source =
                    aq::TrainingSourceKind::Teacher,
                .example = training_example(
                    options, aq::kTeacherRowWeight,
                    1000.0 +
                        static_cast<double>(
                            teacher_index)),
            });
            ++teacher_index;
        }
    }
    std::size_t anchor_index = 0;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t row = 0;
             row < aq::kRowsPerDeck; ++row) {
            const std::size_t options =
                anchor_index < 119 ? 3 : 2;
            rows.push_back({
                .owner_deck =
                    static_cast<old_school::DeckId>(deck),
                .source =
                    aq::TrainingSourceKind::Anchor,
                .example = training_example(
                    options, aq::kAnchorRowWeight,
                    2000.0 +
                        static_cast<double>(
                            anchor_index)),
            });
            ++anchor_index;
        }
    }
    return rows;
}

std::size_t neutral_options_for_row(
    std::size_t deck, std::size_t row);

struct ProductionBatchFixture {
    aq::g1::Corpus corpus;
    aq::neutral_eval::PreparedNeutralCorpus neutral;
};

ProductionBatchFixture production_batch_fixture() {
    const std::vector<aq::TrainingRow> rows =
        exact_training_rows();
    ProductionBatchFixture result;
    result.corpus.fit.reserve(aq::kTeacherExamples);
    result.neutral.fit.reserve(aq::kAnchorExamples);
    for (std::size_t index = 0;
         index < aq::kTeacherExamples; ++index) {
        const aq::TrainingRow& row = rows[index];
        aq::g1::RootExample root;
        root.manifest.coordinate.actor = 0;
        root.manifest.coordinate.seat_decks[0] =
            row.owner_deck;
        root.manifest.coordinate.split =
            aq::g1::Split::Fit;
        root.manifest.actions.assign(
            row.example.options.size(),
            old_school::PriorityAction::pass());
        root.manifest.action_descriptors.assign(
            row.example.options.size(), "synthetic");
        root.manifest.options = row.example.options;
        root.base_scores = row.example.base_scores;
        root.teacher_scores.assign(
            row.example.options.size(), 0.5);
        root.target_probabilities =
            row.example.target_probabilities;
        root.weight = row.example.weight;
        result.corpus.fit.push_back(std::move(root));
    }
    for (std::size_t index = aq::kTeacherExamples;
         index < rows.size(); ++index) {
        const aq::TrainingRow& row = rows[index];
        aq::neutral_eval::PreparedNeutralRow neutral_row;
        neutral_row.split =
            old_school::fq4_dev_bundle::Split::Fit;
        neutral_row.owner_deck =
            static_cast<std::uint8_t>(row.owner_deck);
        neutral_row.actions.reserve(
            row.example.options.size());
        for (std::size_t action = 0;
             action < row.example.options.size();
             ++action) {
            neutral_row.actions.push_back({
                .base_score =
                    row.example.base_scores[action],
                .parent_residual =
                    (action % 2 == 0 ? 0.01 : -0.02),
                .features = row.example.options[action],
            });
        }
        result.neutral.fit.push_back(
            std::move(neutral_row));
    }

    result.corpus.check.reserve(aq::kDevExamples);
    for (std::size_t index = 0;
         index < aq::kDevExamples; ++index) {
        const std::size_t options =
            index < 61 ? 4 : 3;
        const auto example = training_example(
            options, aq::kTeacherRowWeight,
            -900001.0 -
                static_cast<double>(index));
        aq::g1::RootExample root;
        root.manifest.coordinate.actor = 0;
        root.manifest.coordinate.seat_decks[0] =
            static_cast<old_school::DeckId>(
                index % old_school::kDeckCount);
        root.manifest.coordinate.split =
            aq::g1::Split::Check;
        root.manifest.actions.assign(
            options, old_school::PriorityAction::pass());
        root.manifest.action_descriptors.assign(
            options, "dev-sentinel");
        root.manifest.options = example.options;
        root.base_scores = example.base_scores;
        root.teacher_scores.assign(options, 0.5);
        root.target_probabilities =
            example.target_probabilities;
        root.weight = example.weight;
        result.corpus.check.push_back(std::move(root));
    }
    result.neutral.check.reserve(
        aq::kNeutralDevExamples);
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t row = 0;
             row < aq::kRowsPerDeck; ++row) {
            aq::neutral_eval::PreparedNeutralRow neutral_row;
            neutral_row.split =
                old_school::fq4_dev_bundle::Split::Check;
            neutral_row.owner_deck =
                static_cast<std::uint8_t>(deck);
            const std::size_t options =
                neutral_options_for_row(deck, row);
            neutral_row.actions.reserve(options);
            for (std::size_t action = 0;
                 action < options; ++action) {
                std::vector<double> features(
                    aq::g1::kPolicyFeatureCount, 0.0);
                features[0] =
                    -900002.0 -
                    static_cast<double>(
                        100 * deck + row) -
                    static_cast<double>(action) / 100.0;
                neutral_row.actions.push_back({
                    .base_score =
                        static_cast<double>(action) / 10.0,
                    .parent_residual = 0.0,
                    .features = std::move(features),
                });
            }
            result.neutral.check.push_back(
                std::move(neutral_row));
        }
    }
    return result;
}

aq::g1::Metrics metrics(
    std::size_t roots, std::size_t options,
    double agreement, double regret) {
    aq::g1::Metrics result;
    result.roots = roots;
    result.options = options;
    result.equal_deck_top_one_expected_agreement =
        agreement;
    result.equal_deck_mean_regret = regret;
    return result;
}

aq::g1::FitReport frozen_control_fit() {
    aq::g1::FitReport fit;
    fit.candidate_fingerprint =
        std::string(aq::kRequiredControlFingerprint);
    fit.optimizer = aq::g1::optimizer_config();
    fit.fit_examples = aq::kTeacherExamples;
    fit.fit_options = aq::kTeacherOptions;
    fit.parent_fit = metrics(
        aq::kTeacherExamples,
        aq::kTeacherOptions,
        aq::g2::kExpectedParentFitAgreement,
        aq::g2::kExpectedParentFitRegret);
    fit.candidate_fit = metrics(
        aq::kTeacherExamples,
        aq::kTeacherOptions,
        aq::g2::kExpectedControlFitAgreement,
        aq::g2::kExpectedControlFitRegret);
    fit.parent_check = metrics(
        aq::kDevExamples,
        aq::kDevOptions,
        aq::g2::kExpectedParentDevAgreement,
        aq::g2::kExpectedParentDevRegret);
    fit.candidate_check = metrics(
        aq::kDevExamples,
        aq::kDevOptions,
        aq::g2::kExpectedControlDevAgreement,
        aq::g2::kExpectedControlDevRegret);
    return fit;
}

old_school::action_q_offline_gate::AncestralGate
passing_ancestral() {
    using old_school::CardId;
    using old_school::PriorityAction;
    using old_school::Target;
    old_school::action_q_offline_gate::AncestralGate gate;
    gate.self_score = 0.9;
    gate.opponent_score = 0.1;
    gate.legal_actions = {
        PriorityAction::pass(),
        PriorityAction::play_land(CardId::Island),
        PriorityAction::cast_artifact(CardId::SolRing),
        PriorityAction::cast_ancestral_recall(
            Target::player_target(0)),
        PriorityAction::cast_ancestral_recall(
            Target::player_target(1)),
    };
    gate.selected_support = {
        PriorityAction::cast_ancestral_recall(
            Target::player_target(0)),
    };
    gate.information_action_fingerprint =
        std::string(
            old_school::action_q_offline_gate::
                kAncestralInformationActionFingerprint);
    gate.complete_legal_actions_exact = true;
    gate.information_action_fingerprint_exact = true;
    gate.hidden_repartition_bit_identical = true;
    gate.self_strictly_above_opponent = true;
    gate.opponent_absent_from_support = true;
    expect(
        gate.gate_passed(),
        "AQ4-G3 Ancestral fixture is invalid");
    return gate;
}

aq::g1::OfflineReport passing_g1_offline() {
    aq::g1::OfflineReport report;
    report.parent_immutable = true;
    report.repeated_fit_bit_identical = true;
    report.only_priority_component_changed = true;
    report.fit_regret_strictly_improved = true;
    report.check_regret_strictly_improved = true;
    report.check_deck_regret_guard.fill(true);
    report.descriptor_order_identity = true;
    report.redundant_counter_pass = true;
    report.braingeyser_x_zero_excluded = true;
    report.sick_bear_growth_pass = true;
    report.live_force_spike = true;
    report.ancestral = passing_ancestral();
    expect(
        report.gate_passed(),
        "AQ4-G3 G1-offline fixture is invalid");
    return report;
}

std::size_t neutral_options_for_row(
    std::size_t deck, std::size_t row) {
    constexpr std::array<std::size_t, 5>
        kOptionsByDeck{82, 86, 80, 103, 87};
    const std::size_t extras =
        kOptionsByDeck[deck] - 2 * aq::kRowsPerDeck;
    return 2 +
           (row < std::min(
                      extras, aq::kRowsPerDeck)
                ? 1
                : 0) +
           (row <
                    (extras > aq::kRowsPerDeck
                         ? extras - aq::kRowsPerDeck
                         : 0)
                ? 1
                : 0);
}

std::vector<aq::neutral_eval::NeutralScoreTriplet>
neutral_triplets(double anchored_second_score) {
    std::vector<
        aq::neutral_eval::NeutralScoreTriplet>
        rows;
    rows.reserve(aq::kNeutralDevExamples);
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t row = 0;
             row < aq::kRowsPerDeck; ++row) {
            const std::size_t options =
                neutral_options_for_row(deck, row);
            aq::neutral_eval::NeutralScoreTriplet triplet;
            triplet.owner_deck =
                static_cast<std::uint8_t>(deck);
            triplet.parent_combined_scores.assign(
                options, -2.0);
            triplet.baseline_combined_scores.assign(
                options, -2.0);
            triplet.anchored_combined_scores.assign(
                options, -2.0);
            triplet.parent_combined_scores[0] = 1.0;
            triplet.parent_combined_scores[1] = 0.0;
            triplet.baseline_combined_scores[0] = 0.0;
            triplet.baseline_combined_scores[1] = 1.0;
            triplet.anchored_combined_scores[0] = 1.0;
            triplet.anchored_combined_scores[1] =
                anchored_second_score;
            rows.push_back(std::move(triplet));
        }
    }
    return rows;
}

void test_cli_and_frozen_coordinates_are_sealed() {
    const std::vector<std::string_view> run{"--run"};
    const std::vector<std::string_view> empty;
    const std::vector<std::string_view> census{"--census"};
    const std::vector<std::string_view> extra{
        "--run", "--again"};
    expect(
        aq::parse_command(run) == aq::Command::Run &&
            !aq::parse_command(empty).has_value() &&
            !aq::parse_command(census).has_value() &&
            !aq::parse_command(extra).has_value(),
        "AQ4-G3 CLI accepted an undeclared shape");
    std::ostringstream usage;
    aq::print_usage(usage);
    expect(
        usage.str() ==
            "Usage: old-school-action-q-nested-actor-anchor "
            "--run\n",
        "AQ4-G3 usage drifted");
    expect(
        aq::kSelectorSeed == 202607282201ULL &&
            aq::kSelectorSeed != aq::g1::kSelectorSeed &&
            aq::kSelectorSeed != aq::g2::kSelectorSeed &&
            aq::kTeacherExamples == 320 &&
            aq::kTeacherOptions == 1023 &&
            aq::kDevExamples == 319 &&
            aq::kDevOptions == 1018 &&
            aq::kAnchorExamples == 160 &&
            aq::kAnchorOptions == 439 &&
            aq::kNeutralDevExamples == 160 &&
            aq::kNeutralDevOptions == 438 &&
            aq::kAnchoredExamples == 480 &&
            aq::kAnchoredOptions == 1462,
        "AQ4-G3 frozen coordinate changed");
    expect(
        aq::g1::optimizer_config() ==
            aq::g2::optimizer_for_epochs(64),
        "AQ4-G3 optimizer differs from exact G1");
}

void test_combined_boundary_and_loss_mass_are_exact() {
    const std::vector<aq::TrainingRow> rows =
        exact_training_rows();
    const aq::TrainingBatch batch =
        aq::build_training_batch(
            rows, aq::g1::optimizer_config());
    expect(
        batch.valid(true) &&
            batch.examples.size() ==
                aq::kAnchoredExamples &&
            batch.sources.size() ==
                aq::kAnchoredExamples &&
            batch.teacher_examples ==
                aq::kTeacherExamples &&
            batch.teacher_options ==
                aq::kTeacherOptions &&
            batch.anchor_examples ==
                aq::kAnchorExamples &&
            batch.anchor_options ==
                aq::kAnchorOptions,
        "AQ4-G3 combined update boundary drifted");
    expect(
        std::all_of(
            batch.sources.begin(),
            batch.sources.begin() +
                static_cast<std::ptrdiff_t>(
                    aq::kTeacherExamples),
            [](aq::TrainingSourceKind source) {
                return source ==
                       aq::TrainingSourceKind::Teacher;
            }) &&
            std::all_of(
                batch.sources.begin() +
                    static_cast<std::ptrdiff_t>(
                        aq::kTeacherExamples),
                batch.sources.end(),
                [](aq::TrainingSourceKind source) {
                    return source ==
                           aq::TrainingSourceKind::Anchor;
                }),
        "AQ4-G3 canonical teacher-then-anchor order drifted");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect(
            batch.teacher_loss_mass_by_deck[deck] ==
                    1.0 &&
                batch.anchor_loss_mass_by_deck[deck] ==
                    1.0,
            "AQ4-G3 per-deck loss mass is not exact 1:1");
    }
    expect(
        batch.digest.size() == 64 &&
            batch.source_identity ==
                aq::required_source_identity() &&
            batch.digest ==
                aq::canonical_training_digest(
                    batch, aq::g1::optimizer_config()),
        "AQ4-G3 mixed-batch digest is not canonical");
    auto source_tamper = batch;
    source_tamper.source_identity.corpus_digest[0] =
        source_tamper.source_identity.corpus_digest[0] ==
                '0'
            ? '1'
            : '0';
    expect(
        !source_tamper.valid(true),
        "AQ4-G3 batch validity omitted source identity");

    auto wrong_weight = rows;
    wrong_weight.back().example.weight =
        std::nextafter(
            wrong_weight.back().example.weight, 1.0);
    expect_invalid_batch(
        [&] {
            return aq::build_training_batch(
                wrong_weight,
                aq::g1::optimizer_config());
        },
        "AQ4-G3 accepted one-bit anchor-mass drift");

    auto wrong_order = rows;
    std::swap(
        wrong_order[0],
        wrong_order[aq::kTeacherExamples]);
    expect_invalid_batch(
        [&] {
            return aq::build_training_batch(
                wrong_order,
                aq::g1::optimizer_config());
        },
        "AQ4-G3 accepted anchor-before-teacher order");

    auto wrong_deck = rows;
    wrong_deck.back().owner_deck =
        old_school::DeckId::Green;
    expect_invalid_batch(
        [&] {
            return aq::build_training_batch(
                wrong_deck,
                aq::g1::optimizer_config());
        },
        "AQ4-G3 accepted a missing anchor deck");

    auto missing = rows;
    missing.pop_back();
    expect_invalid_batch(
        [&] {
            return aq::build_training_batch(
                missing,
                aq::g1::optimizer_config());
        },
        "AQ4-G3 accepted a 479-row treatment");
}

void test_parent_behavior_target_math_is_exact() {
    aq::neutral_eval::PreparedNeutralRow row;
    row.owner_deck = 2;
    row.actions = {
        {
            .base_score = 0.20,
            .parent_residual = 0.01,
            .features = std::vector<double>(
                aq::g1::kPolicyFeatureCount, 0.0),
        },
        {
            .base_score = 0.40,
            .parent_residual = -0.02,
            .features = std::vector<double>(
                aq::g1::kPolicyFeatureCount, 0.0),
        },
        {
            .base_score = 0.30,
            .parent_residual = 0.02,
            .features = std::vector<double>(
                aq::g1::kPolicyFeatureCount, 0.0),
        },
    };
    const std::vector<double> target =
        aq::neutral_eval::neutral_behavior_target(row);
    constexpr std::array<double, 3> kScores{
        0.21, 0.38, 0.32};
    const double maximum =
        *std::max_element(
            kScores.begin(), kScores.end());
    std::array<double, 3> exponentials{};
    for (std::size_t index = 0;
         index < exponentials.size(); ++index) {
        exponentials[index] =
            std::exp(
                (kScores[index] - maximum) / 0.10);
    }
    const double total =
        std::accumulate(
            exponentials.begin(),
            exponentials.end(), 0.0);
    for (std::size_t index = 0;
         index < target.size(); ++index) {
        const double expected =
            0.90 * exponentials[index] / total +
            0.10 / 3.0;
        expect_close(
            target[index], expected, 1.0e-15,
            "AQ4-G3 parent target formula drifted");
        expect(
            target[index] > 0.0,
            "AQ4-G3 parent target lost full support");
    }
    expect_close(
        std::accumulate(
            target.begin(), target.end(), 0.0),
        1.0, 1.0e-15,
        "AQ4-G3 parent target is not normalized");

    row.actions[0].base_score = 0.40;
    row.actions[0].parent_residual = -0.02;
    const std::vector<double> tied =
        aq::neutral_eval::neutral_behavior_target(row);
    expect(
        tied[0] == tied[1],
        "AQ4-G3 exact parent-score tie changed target mass");
}

void test_production_batch_uses_fit_splits_only() {
    const ProductionBatchFixture fixture =
        production_batch_fixture();
    const aq::TrainingBatch anchored =
        aq::testing::build_training_batch_from_splits(
            fixture.corpus.fit,
            fixture.corpus.check,
            fixture.neutral,
            aq::g1::optimizer_config(), true);
    const aq::TrainingBatch control =
        aq::testing::build_training_batch_from_splits(
            fixture.corpus.fit,
            fixture.corpus.check,
            fixture.neutral,
            aq::g1::optimizer_config(), false);
    expect(
        anchored.valid(true) &&
            control.valid(false) &&
            anchored.examples.size() ==
                aq::kAnchoredExamples &&
            control.examples.size() ==
                aq::kTeacherExamples &&
            control.anchor_examples == 0 &&
            control.anchor_options == 0,
        "AQ4-G3 production batch crossed its update "
        "boundary");
    const auto contains_marker =
        [](const aq::TrainingBatch& batch,
           double marker) {
            for (const auto& example : batch.examples) {
                for (const auto& option : example.options) {
                    if (!option.empty() &&
                        option.front() == marker) {
                        return true;
                    }
                }
            }
            return false;
        };
    expect(
        !contains_marker(anchored, -900001.0) &&
            !contains_marker(anchored, -900002.0) &&
            !contains_marker(control, -900001.0) &&
            !contains_marker(control, -900002.0),
        "AQ4-G3 admitted a G1 or neutral DEV row "
        "to the optimizer");
    expect(
        anchored.examples[aq::kTeacherExamples]
                .target_probabilities ==
            aq::neutral_eval::neutral_behavior_target(
                fixture.neutral.fit.front()),
        "AQ4-G3 production anchor target is not "
        "the frozen parent behavior");
}

void test_control_is_bit_exact_and_fail_closed() {
    const aq::g1::FitReport fit =
        frozen_control_fit();
    expect(
        aq::control_matches_frozen_result(fit),
        "AQ4-G3 rejected exact G1 control coordinates");

    auto changed = fit;
    changed.candidate_check.equal_deck_mean_regret =
        std::nextafter(
            changed.candidate_check
                .equal_deck_mean_regret,
            1.0);
    expect(
        !aq::control_matches_frozen_result(changed),
        "AQ4-G3 accepted one-bit control metric drift");
    changed = fit;
    changed.optimizer.learning_rate =
        std::nextafter(
            changed.optimizer.learning_rate, 1.0);
    expect(
        !aq::control_matches_frozen_result(changed),
        "AQ4-G3 accepted optimizer drift");
    changed = fit;
    --changed.fit_options;
    expect(
        !aq::control_matches_frozen_result(changed),
        "AQ4-G3 accepted control census drift");
    changed = fit;
    changed.candidate_fingerprint[0] =
        changed.candidate_fingerprint[0] == '0'
            ? '1'
            : '0';
    expect(
        !aq::control_matches_frozen_result(changed),
        "AQ4-G3 accepted control fingerprint drift");

    const aq::ControlReport forged{
        .fit = fit,
        .corpus_digest_exact = true,
        .fingerprint_exact = true,
        .aggregate_metrics_bit_exact = true,
    };
    expect(
        !forged.gate_passed(),
        "AQ4-G3 trusted control summary bits without "
        "a bound exact candidate");
}

template <typename Mutation>
void expect_digest_tamper_detected(
    const aq::TrainingBatch& original,
    Mutation mutation,
    std::string_view message) {
    aq::TrainingBatch changed = original;
    mutation(changed);
    try {
        const std::string digest =
            aq::canonical_training_digest(
                changed, aq::g1::optimizer_config());
        expect(digest != original.digest, message);
    } catch (const std::exception&) {
        return;
    }
}

void test_training_digest_binds_every_input_class() {
    const aq::TrainingBatch batch =
        aq::build_training_batch(
            exact_training_rows(),
            aq::g1::optimizer_config());
    expect_digest_tamper_detected(
        batch,
        [](aq::TrainingBatch& changed) {
            changed.sources[0] =
                aq::TrainingSourceKind::Anchor;
        },
        "AQ4-G3 digest omitted source kind");
    expect_digest_tamper_detected(
        batch,
        [](aq::TrainingBatch& changed) {
            changed.owner_decks[0] =
                old_school::DeckId::Red;
        },
        "AQ4-G3 digest omitted owner deck");
    expect_digest_tamper_detected(
        batch,
        [](aq::TrainingBatch& changed) {
            changed.examples[0].options[0][0] =
                std::nextafter(
                    changed.examples[0]
                        .options[0][0],
                    1.0);
        },
        "AQ4-G3 digest omitted feature bits");
    expect_digest_tamper_detected(
        batch,
        [](aq::TrainingBatch& changed) {
            changed.examples[0].base_scores[0] =
                std::nextafter(
                    changed.examples[0]
                        .base_scores[0],
                    1.0);
        },
        "AQ4-G3 digest omitted base-score bits");
    expect_digest_tamper_detected(
        batch,
        [](aq::TrainingBatch& changed) {
            changed.examples[0]
                .target_probabilities[0] =
                std::nextafter(
                    changed.examples[0]
                        .target_probabilities[0],
                    1.0);
        },
        "AQ4-G3 digest omitted target bits");
    expect_digest_tamper_detected(
        batch,
        [](aq::TrainingBatch& changed) {
            changed.examples[0].weight =
                std::nextafter(
                    changed.examples[0].weight,
                    1.0);
        },
        "AQ4-G3 digest omitted weight bits");
    expect_digest_tamper_detected(
        batch,
        [](aq::TrainingBatch& changed) {
            std::swap(
                changed.examples[0],
                changed.examples[1]);
        },
        "AQ4-G3 digest omitted canonical row order");

    auto optimizer = aq::g1::optimizer_config();
    optimizer.learning_rate =
        std::nextafter(
            optimizer.learning_rate, 1.0);
    expect(
        aq::canonical_training_digest(
            batch, optimizer) != batch.digest,
        "AQ4-G3 digest omitted optimizer bits");

    const auto source_mutation =
        [&](auto mutation, std::string_view message) {
            expect_digest_tamper_detected(
                batch,
                [&](aq::TrainingBatch& changed) {
                    mutation(changed.source_identity);
                },
                message);
        };
    source_mutation(
        [](aq::SourceIdentity& identity) {
            identity.corpus_digest[0] =
                identity.corpus_digest[0] == '0'
                    ? '1'
                    : '0';
        },
        "AQ4-G3 digest omitted G1 corpus identity");
    source_mutation(
        [](aq::SourceIdentity& identity) {
            ++identity.dev1_bundle.bytes;
        },
        "AQ4-G3 digest omitted DEV1 bundle identity");
    source_mutation(
        [](aq::SourceIdentity& identity) {
            identity.dev1_bundle.sha256[0] =
                identity.dev1_bundle.sha256[0] == '0'
                    ? '1'
                    : '0';
        },
        "AQ4-G3 digest omitted DEV1 bundle digest");
    source_mutation(
        [](aq::SourceIdentity& identity) {
            ++identity.parent_artifact.bytes;
        },
        "AQ4-G3 digest omitted parent artifact identity");
    source_mutation(
        [](aq::SourceIdentity& identity) {
            identity.parent_artifact.sha256[0] =
                identity.parent_artifact.sha256[0] == '0'
                    ? '1'
                    : '0';
        },
        "AQ4-G3 digest omitted parent artifact digest");
    source_mutation(
        [](aq::SourceIdentity& identity) {
            identity.parent_model_fingerprint[0] =
                identity.parent_model_fingerprint[0] == '0'
                    ? '1'
                    : '0';
        },
        "AQ4-G3 digest omitted parent model identity");
    source_mutation(
        [](aq::SourceIdentity& identity) {
            ++identity.neutral_file.bytes;
        },
        "AQ4-G3 digest omitted neutral artifact identity");
    source_mutation(
        [](aq::SourceIdentity& identity) {
            identity.neutral_file.sha256[0] =
                identity.neutral_file.sha256[0] == '0'
                    ? '1'
                    : '0';
        },
        "AQ4-G3 digest omitted neutral artifact digest");
    source_mutation(
        [](aq::SourceIdentity& identity) {
            identity.neutral_selected_order_sha256[0] =
                identity.neutral_selected_order_sha256[0] ==
                        '0'
                    ? '1'
                    : '0';
        },
        "AQ4-G3 digest omitted neutral selected order");
}

void test_neutral_kl_and_support_gates_are_literal() {
    const auto passing =
        aq::neutral_eval::measure_neutral_check(
            neutral_triplets(0.5));
    expect(
        passing.rows == aq::kNeutralDevExamples &&
            passing.options == aq::kNeutralDevOptions &&
            passing.baseline_equal_deck_kl > 0.0 &&
            2.0 * passing.anchored_equal_deck_kl <=
                passing.baseline_equal_deck_kl &&
            passing.baseline_exact_support_changes ==
                aq::kNeutralDevExamples &&
            passing.anchored_exact_support_changes == 0,
        "AQ4-G3 neutral KL/support fixture is invalid");
    for (const auto& deck : passing.decks) {
        expect(
            deck.rows == aq::kRowsPerDeck &&
                deck.anchored_parent_to_candidate_kl <=
                    deck.baseline_parent_to_candidate_kl &&
                deck.anchored_exact_support_changes <=
                    deck.baseline_exact_support_changes,
            "AQ4-G3 per-deck neutral gate fixture drifted");
    }

    aq::ControlReport unbound_control{
        .fit = frozen_control_fit(),
        .corpus_digest_exact = true,
        .fingerprint_exact = true,
        .aggregate_metrics_bit_exact = true,
    };
    const aq::TrainingBatch batch =
        aq::build_training_batch(
            exact_training_rows(),
            aq::g1::optimizer_config());
    aq::g1::FitReport anchored_fit;
    anchored_fit.optimizer =
        aq::g1::optimizer_config();
    anchored_fit.fit_examples =
        aq::kAnchoredExamples;
    anchored_fit.fit_options =
        aq::kAnchoredOptions;
    anchored_fit.parent_immutable = true;
    anchored_fit.repeated_fit_bit_identical = true;
    anchored_fit.only_priority_component_changed = true;
    const aq::g1::OfflineReport offline =
        passing_g1_offline();
    const aq::GateReport gate =
        aq::evaluate_gate(
            true, unbound_control, batch,
            anchored_fit, offline, passing);
    expect(
        gate.source_identity_exact &&
            !gate.control_exact &&
            !gate.mixed_batch_exact &&
            gate.repeated_fit_bit_identical &&
            gate.parent_immutable &&
            gate.only_priority_component_changed &&
            !gate.g1_offline_passed &&
            gate.ancestral_passed &&
            gate.neutral_baseline_nonzero &&
            gate.neutral_per_deck_nonworsening &&
            gate.neutral_kl_halved &&
            gate.neutral_support_changes_halved &&
            !gate.passed(),
        "AQ4-G3 gate membership is not exact");
    const aq::GateReport bad_source =
        aq::evaluate_gate(
            false, unbound_control, batch,
            anchored_fit, offline, passing);
    expect(
        !bad_source.source_identity_exact &&
            !bad_source.passed(),
        "AQ4-G3 source-authentication failure "
        "passed the offline gate");

    const auto no_retention =
        aq::neutral_eval::measure_neutral_check(
            neutral_triplets(3.0));
    const aq::GateReport failed =
        aq::evaluate_gate(
            true, unbound_control, batch,
            anchored_fit, offline, no_retention);
    expect(
        !failed.neutral_per_deck_nonworsening &&
            !failed.neutral_kl_halved &&
            !failed.neutral_support_changes_halved,
        "AQ4-G3 accepted neutral behavior worse "
        "than unanchored G1");

    auto odd = passing;
    odd.baseline_exact_support_changes = 3;
    odd.anchored_exact_support_changes = 2;
    for (auto& deck : odd.decks) {
        deck.baseline_exact_support_changes = 0;
        deck.anchored_exact_support_changes = 0;
    }
    odd.decks[0].baseline_exact_support_changes = 1;
    odd.decks[0].anchored_exact_support_changes = 1;
    odd.decks[1].baseline_exact_support_changes = 1;
    odd.decks[1].anchored_exact_support_changes = 1;
    odd.decks[2].baseline_exact_support_changes = 1;
    const aq::GateReport odd_gate =
        aq::evaluate_gate(
            true, unbound_control, batch,
            anchored_fit, offline, odd);
    expect(
        odd_gate.neutral_per_deck_nonworsening &&
            !odd_gate.neutral_support_changes_halved,
        "AQ4-G3 did not apply literal 2*A<=B "
        "to an odd support count");

    auto inconsistent = passing;
    inconsistent.baseline_equal_deck_kl =
        std::nextafter(
            inconsistent.baseline_equal_deck_kl,
            1.0);
    const aq::GateReport inconsistent_gate =
        aq::evaluate_gate(
            true, unbound_control, batch,
            anchored_fit, offline, inconsistent);
    expect(
        !inconsistent_gate.neutral_baseline_nonzero &&
            !inconsistent_gate
                 .neutral_per_deck_nonworsening &&
            !inconsistent_gate.neutral_kl_halved &&
            !inconsistent_gate
                 .neutral_support_changes_halved,
        "AQ4-G3 trusted inconsistent neutral "
        "aggregate fields");
}

void test_gate_membership_is_exact() {
    aq::GateReport passing{
        .source_identity_exact = true,
        .control_exact = true,
        .mixed_batch_exact = true,
        .repeated_fit_bit_identical = true,
        .parent_immutable = true,
        .only_priority_component_changed = true,
        .g1_offline_passed = true,
        .ancestral_passed = true,
        .neutral_baseline_nonzero = true,
        .neutral_per_deck_nonworsening = true,
        .neutral_kl_halved = true,
        .neutral_support_changes_halved = true,
        .failures = {},
    };
    expect(
        passing.passed(),
        "AQ4-G3 rejected the exact gate conjunction");
    const auto rejects =
        [&](auto mutation, std::string_view message) {
            aq::GateReport changed = passing;
            mutation(changed);
            expect(!changed.passed(), message);
        };
    rejects(
        [](aq::GateReport& report) {
            report.source_identity_exact = false;
        },
        "AQ4-G3 omitted source identity");
    rejects(
        [](aq::GateReport& report) {
            report.control_exact = false;
        },
        "AQ4-G3 omitted exact control");
    rejects(
        [](aq::GateReport& report) {
            report.mixed_batch_exact = false;
        },
        "AQ4-G3 omitted mixed-batch identity");
    rejects(
        [](aq::GateReport& report) {
            report.repeated_fit_bit_identical = false;
        },
        "AQ4-G3 omitted repeat-fit identity");
    rejects(
        [](aq::GateReport& report) {
            report.parent_immutable = false;
        },
        "AQ4-G3 omitted parent immutability");
    rejects(
        [](aq::GateReport& report) {
            report.only_priority_component_changed = false;
        },
        "AQ4-G3 omitted component isolation");
    rejects(
        [](aq::GateReport& report) {
            report.g1_offline_passed = false;
        },
        "AQ4-G3 omitted original G1 gates");
    rejects(
        [](aq::GateReport& report) {
            report.ancestral_passed = false;
        },
        "AQ4-G3 omitted complete Ancestral gate");
    rejects(
        [](aq::GateReport& report) {
            report.neutral_baseline_nonzero = false;
        },
        "AQ4-G3 accepted a vacuous neutral baseline");
    rejects(
        [](aq::GateReport& report) {
            report.neutral_per_deck_nonworsening = false;
        },
        "AQ4-G3 omitted a neutral deck guard");
    rejects(
        [](aq::GateReport& report) {
            report.neutral_kl_halved = false;
        },
        "AQ4-G3 omitted KL halving");
    rejects(
        [](aq::GateReport& report) {
            report.neutral_support_changes_halved = false;
        },
        "AQ4-G3 omitted support-change halving");
    rejects(
        [](aq::GateReport& report) {
            report.failures.emplace_back("forged");
        },
        "AQ4-G3 ignored an explicit gate failure");
}

void test_full_offline_binding_and_selector_authorization() {
    aq::OfflineRunReport report;
    expect(
        !report.gate_passed() &&
            !report.selection_ready(),
        "AQ4-G3 default report authorized selection");

    report.corpus_digest =
        std::string(aq::kRequiredCorpusDigest);
    report.corpus_reconstructions = 1;
    report.neutral_identity = {
        .bytes = aq::kNeutralArtifactBytes,
        .sha256 =
            std::string(aq::kNeutralArtifactSha256),
    };
    report.neutral_selected_order_sha256 =
        std::string(aq::kNeutralSelectedOrderSha256);
    report.source_identity_exact = true;
    report.source_identity =
        aq::required_source_identity();
    report.control = {
        .fit = frozen_control_fit(),
        .corpus_digest_exact = true,
        .fingerprint_exact = true,
        .aggregate_metrics_bit_exact = true,
    };
    report.anchored_training =
        aq::build_training_batch(
            exact_training_rows(),
            aq::g1::optimizer_config());
    report.anchored_fit.optimizer =
        aq::g1::optimizer_config();
    report.anchored_fit.parent_fingerprint_before =
        std::string(aq::g1::kRequiredParentFingerprint);
    report.anchored_fit.parent_fingerprint_after =
        std::string(aq::g1::kRequiredParentFingerprint);
    report.anchored_fit.candidate_fingerprint =
        std::string(64, 'a');
    report.anchored_fit.fit_examples =
        aq::kAnchoredExamples;
    report.anchored_fit.fit_options =
        aq::kAnchoredOptions;
    report.anchored_fit.parent_immutable = true;
    report.anchored_fit.repeated_fit_bit_identical = true;
    report.anchored_fit.only_priority_component_changed = true;
    report.anchored_offline =
        passing_g1_offline();
    report.anchored_offline.parent_fingerprint =
        std::string(aq::g1::kRequiredParentFingerprint);
    report.anchored_offline.candidate_fingerprint =
        report.anchored_fit.candidate_fingerprint;
    report.anchored_offline.parent_fit =
        report.anchored_fit.parent_fit;
    report.anchored_offline.candidate_fit =
        report.anchored_fit.candidate_fit;
    report.anchored_offline.parent_check =
        report.anchored_fit.parent_check;
    report.anchored_offline.candidate_check =
        report.anchored_fit.candidate_check;
    report.neutral_parent_fingerprint =
        std::string(aq::g1::kRequiredParentFingerprint);
    report.neutral_baseline_fingerprint =
        std::string(aq::kRequiredControlFingerprint);
    report.neutral_anchored_fingerprint =
        report.anchored_fit.candidate_fingerprint;
    report.neutral_dev =
        aq::neutral_eval::measure_neutral_check(
            neutral_triplets(0.5));
    report.gate = {
        .source_identity_exact = true,
        .control_exact = true,
        .mixed_batch_exact = true,
        .repeated_fit_bit_identical = true,
        .parent_immutable = true,
        .only_priority_component_changed = true,
        .g1_offline_passed = true,
        .ancestral_passed = true,
        .neutral_baseline_nonzero = true,
        .neutral_per_deck_nonworsening = true,
        .neutral_kl_halved = true,
        .neutral_support_changes_halved = true,
        .failures = {},
    };
    expect(
        !report.gate_passed() &&
            !report.selection_ready(),
        "AQ4-G3 trusted a forged passing gate "
        "without bound model identities");
    expect_rejected(
        [&] {
            static_cast<void>(
                aq::run_selector(
                    std::shared_ptr<
                        const old_school::LearnedModel>{},
                    report));
        },
        "AQ4-G3 selector opened without a bound "
        "offline candidate");
}

} // namespace

int main() {
    std::size_t passed = 0;
    const auto run =
        [&passed](std::string_view name, auto test) {
            try {
                test();
                ++passed;
            } catch (const std::exception& error) {
                std::cerr
                    << "FAIL " << name << ": "
                    << error.what() << '\n';
                throw;
            }
        };
    run(
        "cli_and_frozen_coordinates_are_sealed",
        test_cli_and_frozen_coordinates_are_sealed);
    run(
        "combined_boundary_and_loss_mass_are_exact",
        test_combined_boundary_and_loss_mass_are_exact);
    run(
        "parent_behavior_target_math_is_exact",
        test_parent_behavior_target_math_is_exact);
    run(
        "production_batch_uses_fit_splits_only",
        test_production_batch_uses_fit_splits_only);
    run(
        "control_is_bit_exact_and_fail_closed",
        test_control_is_bit_exact_and_fail_closed);
    run(
        "training_digest_binds_every_input_class",
        test_training_digest_binds_every_input_class);
    run(
        "neutral_kl_and_support_gates_are_literal",
        test_neutral_kl_and_support_gates_are_literal);
    run(
        "gate_membership_is_exact",
        test_gate_membership_is_exact);
    run(
        "full_offline_binding_and_selector_authorization",
        test_full_offline_binding_and_selector_authorization);
    std::cout
        << "action-Q nested-actor anchor tests: "
        << passed << "/9 passed\n";
    return 0;
}
