#include "old_school/output_calibration.hpp"

#include "old_school/audit_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace oc = old_school::output_calibration;
using old_school::CardId;
using old_school::DeckId;
using old_school::GameState;

class TestRunner {
  public:
    void run(
        std::string_view name,
        const std::function<void()>& test) {
        try {
            test();
            ++passed_;
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "FAIL " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        if (failed_ != 0) {
            std::cerr << failed_ << " test(s) failed; "
                      << passed_ << " passed\n";
            return 1;
        }
        std::cout << passed_
                  << " output-calibration tests passed\n";
        return 0;
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

void expect_near(
    double actual, double expected, double tolerance,
    std::string_view message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            std::string(message) + ": expected " +
            std::to_string(expected) + ", got " +
            std::to_string(actual));
    }
}

template <typename Exception, typename Function>
void expect_throws(
    Function&& function, std::string_view message) {
    bool threw = false;
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        threw = true;
    }
    expect(threw, message);
}

oc::CollectionConfig test_config(
    std::uint64_t seed, std::size_t generation) {
    return {
        .seed = seed,
        .generation = generation,
        .balanced_blocks = 1,
        .max_game_turns = 7,
        .pilot_training_games = 1,
        .worker_count = 1,
    };
}

std::vector<oc::HoldoutRecord> synthetic_records(
    const oc::CollectionConfig& config,
    std::size_t trace_size = 1) {
    if (trace_size == 0) {
        throw std::invalid_argument(
            "synthetic trace size must be positive");
    }
    std::vector<oc::HoldoutRecord> records;
    for (const oc::CollectionTask& task :
         oc::collection_schedule(config)) {
        for (std::size_t perspective = 0;
             perspective < 2; ++perspective) {
            for (std::size_t trace_index = 0;
                 trace_index < trace_size;
                 ++trace_index) {
                records.push_back({
                    .physical_game = task.physical_game,
                    .perspective = perspective,
                    .deck =
                        task.scheduled
                            .seat_decks[perspective],
                    .trace_index = trace_index,
                    .trace_size = trace_size,
                    .target = 0.5,
                    .weight =
                        1.0 /
                        static_cast<double>(trace_size),
                    .parent_leaf_predictions = {0.7, 0.7},
                    .parent_prediction = 0.7,
                    .candidate_leaf_predictions = {
                        0.55, 0.55},
                    .candidate_prediction = 0.55,
                });
            }
        }
    }
    return records;
}

void set_hidden_hashes(
    oc::HiddenRepartitionReport& hidden,
    char fill) {
    const std::string digest(64, fill);
    hidden.original_owner_visible_rows_hash = digest;
    hidden.repartitioned_owner_visible_rows_hash = digest;
    hidden.original_encoded_rows_hash = digest;
    hidden.repartitioned_encoded_rows_hash = digest;
    hidden.original_parent_leaf_hash = digest;
    hidden.repartitioned_parent_leaf_hash = digest;
    hidden.original_parent_prediction_hash = digest;
    hidden.repartitioned_parent_prediction_hash = digest;
    hidden.original_candidate_leaf_hash = digest;
    hidden.repartitioned_candidate_leaf_hash = digest;
    hidden.original_candidate_prediction_hash = digest;
    hidden.repartitioned_candidate_prediction_hash = digest;
}

oc::CollectionAccounting synthetic_accounting(
    const oc::CollectionConfig& config) {
    const auto tasks = oc::collection_schedule(config);
    oc::CollectionAccounting accounting;
    accounting.config = config;
    accounting.schedule =
        oc::inspect_collection_schedule(
            tasks, config.balanced_blocks);
    accounting.actor_perspectives = 2 * tasks.size();
    accounting.records = accounting.actor_perspectives;
    accounting.total_weight =
        static_cast<double>(accounting.actor_perspectives);
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const std::size_t perspectives =
            accounting.schedule.perspectives_by_deck[deck];
        accounting.by_deck[deck] = {
            .perspectives = perspectives,
            .records = perspectives,
            .total_weight =
                static_cast<double>(perspectives),
        };
        accounting.hidden.by_deck[deck] = {
            .attempted = perspectives,
            .changed = 1,
            .unchanged = perspectives - 1,
        };
        accounting.hidden.pooled.attempted +=
            perspectives;
        ++accounting.hidden.pooled.changed;
        accounting.hidden.pooled.unchanged +=
            perspectives - 1;
    }
    set_hidden_hashes(accounting.hidden, 'a');
    return accounting;
}

oc::IntegrityEvidence passing_integrity() {
    oc::IntegrityEvidence evidence = {
        .parent_identity = true,
        .fit_provenance = true,
        .holdout_provenance = true,
        .component_isolation = true,
        .artifact = true,
        .determinism = true,
        .finite_values = true,
    };
    const std::string digest(64, 'b');
    evidence.original_fit_parameters_hash = digest;
    evidence.repartitioned_fit_parameters_hash = digest;
    evidence.original_scientific_report_hash = digest;
    evidence.repartitioned_scientific_report_hash =
        digest;
    return evidence;
}

oc::GateConfig test_gate_config(
    const oc::CollectionConfig& fit,
    const oc::CollectionConfig& holdout) {
    return {
        .expected_fit = fit,
        .expected_holdout = holdout,
        .expected_physical_games =
            old_school::learned_iteration::
                kBalancedScheduleGames,
        .expected_perspectives_per_deck = 16,
        .deck_loss_guard = oc::kDeckLossGuard,
        .other_deck_bias_guard =
            oc::kOtherDeckBiasGuard,
        .material_bias_threshold =
            oc::kMaterialBiasThreshold,
    };
}

void test_schedule_is_deterministic_and_balanced() {
    const oc::CollectionConfig config =
        test_config(9918273ULL, 3);
    const auto first = oc::collection_schedule(config);
    const auto second = oc::collection_schedule(config);
    expect(first == second, "schedule is not deterministic");
    expect(
        first.size() ==
            old_school::learned_iteration::
                kBalancedScheduleGames,
        "one balanced block must contain 40 games");

    const oc::ScheduleAccounting accounting =
        oc::inspect_collection_schedule(
            first, config.balanced_blocks);
    expect(
        accounting.tasks_well_formed,
        "balanced schedule must be well formed");
    expect(
        accounting.exact_balanced_blocks,
        "balanced schedule must pass exact accounting");
    expect(
        accounting.physical_games == 40,
        "physical-game accounting changed");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect(
            accounting.perspectives_by_deck[deck] == 16,
            "deck perspective count is not balanced");
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t starts = 0; starts < 2;
                 ++starts) {
                expect(
                    accounting.deck_seat_start[deck][seat]
                                                      [starts] ==
                        4,
                    "deck/seat/play-draw quadrant is not balanced");
            }
        }
    }

    oc::CollectionConfig other = config;
    other.seed += 1;
    expect(
        oc::collection_schedule(other) != first,
        "schedule seed did not alter the schedule");

    auto malformed = first;
    malformed[3].physical_game = 17;
    const auto malformed_accounting =
        oc::inspect_collection_schedule(
            malformed, config.balanced_blocks);
    expect(
        !malformed_accounting.tasks_well_formed &&
            !malformed_accounting.exact_balanced_blocks,
        "malformed task indices passed schedule accounting");

    auto permuted = first;
    std::swap(permuted[2], permuted[3]);
    for (std::size_t index = 0;
         index < permuted.size(); ++index) {
        permuted[index].physical_game = index;
    }
    const auto permuted_accounting =
        oc::inspect_collection_schedule(
            permuted, config.balanced_blocks);
    expect(
        !permuted_accounting.tasks_well_formed &&
            !permuted_accounting.exact_balanced_blocks,
        "canonical schedule permutation was accepted");
}

void test_hidden_exchange_is_nonvacuous_and_private() {
    GameState state;
    state.players[0].hand = {
        CardId::Island, CardId::Counterspell};
    state.players[0].library = {
        CardId::FlyingMen, CardId::Island};
    state.players[1].hand = {CardId::Forest};
    state.players[1].library = {
        CardId::Mountain, CardId::Forest};
    state.players[1].graveyard = {CardId::GrizzlyBears};

    const std::vector<double> before =
        old_school::learned_observation(state, 0);
    const oc::HiddenExchange exchanged =
        oc::exchange_opponent_hidden_identity(state, 0);
    expect(exchanged.changed, "distinct hidden exchange was vacuous");
    expect(
        exchanged.state.players[0] == state.players[0],
        "hidden exchange changed the observer's information");
    expect(
        exchanged.state.players[1].hand.size() ==
                state.players[1].hand.size() &&
            exchanged.state.players[1].library.size() ==
                state.players[1].library.size(),
        "hidden exchange changed a zone size");
    expect(
        exchanged.state.players[1].graveyard ==
            state.players[1].graveyard,
        "hidden exchange changed a public zone");
    GameState original_visible = state;
    GameState exchanged_visible = exchanged.state;
    original_visible.players[1].hand.clear();
    original_visible.players[1].library.clear();
    exchanged_visible.players[1].hand.clear();
    exchanged_visible.players[1].library.clear();
    expect(
        original_visible == exchanged_visible,
        "hidden exchange changed owner-visible state");
    expect(
        exchanged.state.players[1].hand[0] ==
                CardId::Mountain &&
            exchanged.state.players[1].library[0] ==
                CardId::Forest,
        "hidden exchange did not swap distinct identities");
    expect(
        old_school::audit_common::bit_identical(
            before,
            old_school::learned_observation(
                exchanged.state, 0)),
        "opponent hidden identity entered learned observation");

    std::vector<CardId> original_pool =
        state.players[1].hand;
    original_pool.insert(
        original_pool.end(),
        state.players[1].library.begin(),
        state.players[1].library.end());
    std::vector<CardId> exchanged_pool =
        exchanged.state.players[1].hand;
    exchanged_pool.insert(
        exchanged_pool.end(),
        exchanged.state.players[1].library.begin(),
        exchanged.state.players[1].library.end());
    std::sort(original_pool.begin(), original_pool.end());
    std::sort(exchanged_pool.begin(), exchanged_pool.end());
    expect(
        original_pool == exchanged_pool,
        "hidden exchange changed the hidden card pool");

    GameState same_identity = state;
    same_identity.players[1].hand = {CardId::Forest};
    same_identity.players[1].library = {
        CardId::Forest, CardId::Forest};
    const oc::HiddenExchange unchanged =
        oc::exchange_opponent_hidden_identity(
            same_identity, 0);
    expect(
        !unchanged.changed &&
            unchanged.state == same_identity,
        "same-identity hidden zones must remain unchanged");
    expect_throws<std::out_of_range>(
        [&] {
            static_cast<void>(
                oc::exchange_opponent_hidden_identity(
                    state, 2));
        },
        "invalid perspective was accepted");
}

void test_weighted_cr1_matches_declared_formula() {
    const std::vector<oc::WeightedClusteredValue> values = {
        {.cluster = 0, .weight = 0.25, .value = 1.0},
        {.cluster = 0, .weight = 0.75, .value = 3.0},
        {.cluster = 1, .weight = 0.5, .value = 5.0},
        {.cluster = 1, .weight = 0.5, .value = 7.0},
    };
    const auto estimate = oc::weighted_cr1_estimate(values);
    expect(estimate.records == 4, "CR1 record count");
    expect(estimate.clusters == 2, "CR1 cluster count");
    expect_near(
        estimate.total_weight, 2.0, 1.0e-15,
        "CR1 total weight");
    expect_near(
        estimate.mean, 4.25, 1.0e-15, "CR1 mean");
    expect_near(
        estimate.standard_error, 1.75, 1.0e-15,
        "CR1 standard error");
    expect_near(
        estimate.confidence_upper_95,
        4.25 + oc::kNormal95CriticalValue * 1.75,
        1.0e-14, "CR1 upper interval");

    expect_throws<std::invalid_argument>(
        [] {
            const std::vector<oc::WeightedClusteredValue>
                empty;
            static_cast<void>(
                oc::weighted_cr1_estimate(empty));
        },
        "empty CR1 input was accepted");
    expect_throws<std::invalid_argument>(
        [] {
            const std::vector<oc::WeightedClusteredValue>
                one_cluster = {
                    {.cluster = 0,
                     .weight = 1.0,
                     .value = 0.5},
                };
            static_cast<void>(
                oc::weighted_cr1_estimate(one_cluster));
        },
        "one-cluster CR1 input was accepted");
    expect_throws<std::invalid_argument>(
        [] {
            const std::vector<oc::WeightedClusteredValue>
                nonfinite = {
                    {.cluster = 0,
                     .weight = 1.0,
                     .value = 0.5},
                    {.cluster = 1,
                     .weight = 1.0,
                     .value =
                         std::numeric_limits<double>::
                             quiet_NaN()},
                };
            static_cast<void>(
                oc::weighted_cr1_estimate(nonfinite));
        },
        "nonfinite CR1 input was accepted");
}

void test_holdout_metrics_are_weighted_and_deck_balanced() {
    const oc::CollectionConfig config =
        test_config(723451ULL, 5);
    const auto records = synthetic_records(config);
    const oc::HoldoutReport report =
        oc::score_holdout_records(records);

    expect(report.pooled.records == 80, "pooled record count");
    expect(
        report.pooled.physical_games == 40,
        "pooled physical-game count");
    expect(
        report.pooled.actor_perspectives == 80,
        "pooled perspective count");
    expect_near(
        report.pooled.total_weight, 80.0, 1.0e-12,
        "pooled metric mass");
    expect(
        report.pooled.candidate.brier.mean <
                report.pooled.parent.brier.mean &&
            report.pooled.candidate.soft_log_loss.mean <
                report.pooled.parent.soft_log_loss.mean,
        "better synthetic candidate did not improve losses");
    expect(
        report.pooled.candidate_minus_parent
                    .brier_delta.confidence_upper_95 <
                0.0 &&
            report.pooled.candidate_minus_parent
                    .soft_log_loss_delta
                    .confidence_upper_95 <
                0.0,
        "constant improvement did not clear paired CR1");
    expect_near(
        report.pooled.parent.signed_bias.mean,
        0.2, 1.0e-14, "parent signed bias");
    expect_near(
        report.pooled.candidate.signed_bias.mean,
        0.05, 1.0e-14, "candidate signed bias");
    expect(
        report.pooled.parent.saturated_records == 0 &&
            report.pooled.candidate.saturated_records == 0,
        "ordinary predictions were marked saturated");

    for (const oc::ScopeReport& deck : report.by_deck) {
        expect(deck.records == 16, "per-deck record count");
        expect(
            deck.physical_games == 16,
            "per-deck physical-game count");
        expect(
            deck.actor_perspectives == 16,
            "per-deck perspective count");
        expect_near(
            deck.total_weight, 16.0, 1.0e-12,
            "per-deck metric mass");
    }
    expect(
        oc::hash_holdout_report(report) ==
            oc::hash_holdout_report(report),
        "holdout report hash is nondeterministic");

    auto incomplete = records;
    incomplete.front().trace_size = 2;
    incomplete.front().weight = 0.5;
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                oc::score_holdout_records(incomplete));
        },
        "incomplete perspective trace was accepted");

    auto duplicate = records;
    duplicate.push_back(records.front());
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                oc::score_holdout_records(duplicate));
        },
        "duplicate trace row was accepted");
}

void test_high_row_metric_mass_is_stable() {
    constexpr std::size_t trace_size = 997;
    const oc::CollectionConfig config =
        test_config(8934751ULL, 6);
    const auto records =
        synthetic_records(config, trace_size);
    expect(
        records.size() == 80 * trace_size,
        "high-row fixture size");
    const oc::HoldoutReport report =
        oc::score_holdout_records(records);
    expect_near(
        report.pooled.total_weight, 80.0, 1.0e-12,
        "high-row pooled mass");
    for (const oc::ScopeReport& deck : report.by_deck) {
        expect_near(
            deck.total_weight, 16.0, 1.0e-12,
            "high-row per-deck mass");
    }
}

void test_gate_is_conjunctive_and_thresholds_are_exact() {
    const oc::CollectionConfig fit =
        test_config(111222333ULL, 7);
    const oc::CollectionConfig holdout =
        test_config(444555666ULL, 8);
    const auto fit_accounting =
        synthetic_accounting(fit);
    const auto holdout_accounting =
        synthetic_accounting(holdout);
    const oc::HoldoutReport passing_report =
        oc::score_holdout_records(
            synthetic_records(holdout));
    const oc::IntegrityEvidence integrity =
        passing_integrity();
    const oc::GateConfig config =
        test_gate_config(fit, holdout);

    const oc::GateReport passing =
        oc::evaluate_gate(
            passing_report, fit_accounting,
            holdout_accounting, integrity, config);
    expect(passing.passed, "passing OC1 gate was rejected");
    expect(
        passing.failures.empty(),
        "passing OC1 gate reported failures");

    oc::HoldoutReport boundary = passing_report;
    boundary.by_deck[0]
        .candidate_minus_parent.brier_delta.mean =
        oc::kDeckLossGuard;
    boundary.by_deck[0]
        .candidate_minus_parent.soft_log_loss_delta.mean =
        oc::kDeckLossGuard;
    expect(
        oc::evaluate_gate(
            boundary, fit_accounting,
            holdout_accounting, integrity, config)
            .per_deck_loss_guard,
        "per-deck loss equality must pass");
    boundary.by_deck[0]
        .candidate_minus_parent.brier_delta.mean =
        std::nextafter(
            oc::kDeckLossGuard,
            std::numeric_limits<double>::infinity());
    expect(
        !oc::evaluate_gate(
             boundary, fit_accounting,
             holdout_accounting, integrity, config)
             .per_deck_loss_guard,
        "per-deck loss above guard must fail");

    boundary = passing_report;
    const std::size_t blue =
        static_cast<std::size_t>(DeckId::Blue);
    boundary.by_deck[blue].candidate.signed_bias.mean =
        boundary.by_deck[blue].parent.signed_bias.mean;
    expect(
        !oc::evaluate_gate(
             boundary, fit_accounting,
             holdout_accounting, integrity, config)
             .blue_bias_shrank,
        "Blue bias equality must fail strict shrinkage");

    boundary = passing_report;
    const std::size_t green =
        static_cast<std::size_t>(DeckId::Green);
    boundary.by_deck[green].candidate.signed_bias.mean =
        boundary.by_deck[green].parent.signed_bias.mean;
    expect(
        oc::evaluate_gate(
            boundary, fit_accounting,
            holdout_accounting, integrity, config)
            .green_bias_nonincreasing,
        "Green bias equality must pass");
    boundary.by_deck[green].candidate.signed_bias.mean +=
        1.0e-6;
    expect(
        !oc::evaluate_gate(
             boundary, fit_accounting,
             holdout_accounting, integrity, config)
             .green_bias_nonincreasing,
        "Green bias increase must fail");

    boundary = passing_report;
    const std::size_t red =
        static_cast<std::size_t>(DeckId::Red);
    boundary.by_deck[red].candidate.signed_bias.mean =
        std::nextafter(
            std::abs(
                boundary.by_deck[red]
                    .parent.signed_bias.mean) +
                oc::kOtherDeckBiasGuard,
            -std::numeric_limits<double>::infinity());
    expect(
        oc::evaluate_gate(
            boundary, fit_accounting,
            holdout_accounting, integrity, config)
            .other_deck_bias_guard,
        "other-deck bias equality must pass");
    boundary.by_deck[red].candidate.signed_bias.mean +=
        1.0e-6;
    expect(
        !oc::evaluate_gate(
             boundary, fit_accounting,
             holdout_accounting, integrity, config)
             .other_deck_bias_guard,
        "other-deck bias above guard must fail");

    boundary = passing_report;
    boundary.pooled.candidate_minus_parent
        .brier_delta.confidence_upper_95 = 0.0;
    expect(
        !oc::evaluate_gate(
             boundary, fit_accounting,
             holdout_accounting, integrity, config)
             .pooled_losses_improved,
        "zero pooled upper bound must fail strict improvement");

    oc::IntegrityEvidence incomplete_integrity = integrity;
    incomplete_integrity.artifact = false;
    expect(
        !oc::evaluate_gate(
             passing_report, fit_accounting,
             holdout_accounting, incomplete_integrity,
             config)
             .integrity_passed,
        "missing artifact evidence passed");
    incomplete_integrity = integrity;
    incomplete_integrity
        .repartitioned_scientific_report_hash[0] = 'c';
    expect(
        !oc::evaluate_gate(
             passing_report, fit_accounting,
             holdout_accounting, incomplete_integrity,
             config)
             .integrity_passed,
        "different hidden scientific report passed");

    auto vacuous_fit = fit_accounting;
    vacuous_fit.hidden.by_deck[blue].changed = 0;
    vacuous_fit.hidden.by_deck[blue].unchanged =
        vacuous_fit.hidden.by_deck[blue].attempted;
    --vacuous_fit.hidden.pooled.changed;
    ++vacuous_fit.hidden.pooled.unchanged;
    expect(
        !oc::evaluate_gate(
             passing_report, vacuous_fit,
             holdout_accounting, integrity, config)
             .collection_accounting_exact,
        "vacuous per-deck hidden audit passed");

    auto unhashed_fit = fit_accounting;
    unhashed_fit.hidden.original_parent_leaf_hash.clear();
    unhashed_fit.hidden.repartitioned_parent_leaf_hash.clear();
    expect(
        !oc::evaluate_gate(
             passing_report, unhashed_fit,
             holdout_accounting, integrity, config)
             .collection_accounting_exact,
        "missing hidden prediction hashes passed");

    auto mismatched_holdout = holdout_accounting;
    --mismatched_holdout.by_deck[green].records;
    --mismatched_holdout.records;
    expect(
        !oc::evaluate_gate(
             passing_report, fit_accounting,
             mismatched_holdout, integrity, config)
             .collection_accounting_exact,
        "report/collection record mismatch passed");

    boundary = passing_report;
    auto& parent_bias =
        boundary.by_deck[red].parent.signed_bias;
    auto& candidate_bias =
        boundary.by_deck[red].candidate.signed_bias;
    parent_bias.mean = 0.0;
    parent_bias.confidence_lower_95 = -0.01;
    parent_bias.confidence_upper_95 = 0.01;
    candidate_bias.mean = 0.06;
    candidate_bias.confidence_lower_95 = 0.05;
    candidate_bias.confidence_upper_95 = 0.07;
    expect(
        !oc::evaluate_gate(
             boundary, fit_accounting,
             holdout_accounting, integrity, config)
             .no_new_material_same_sign_bias,
        "new material deck bias passed");
}

void test_tiny_collection_is_deterministic_and_weighted() {
    const auto parent =
        old_school::train_learned_value_champion(
            1, 0x0C100001ULL);
    oc::CollectionConfig config = {
        .seed = 0x0C100002ULL,
        .generation = 3,
        .balanced_blocks = 1,
        .max_game_turns = 2,
        .pilot_training_games = 1,
        .worker_count = 1,
    };
    const auto schedule = oc::collection_schedule(config);
    const std::span<const oc::CollectionTask> tiny_tasks(
        schedule.data(), 2);

    const oc::TrainingCorpus training =
        oc::collect_training_corpus(
            parent, tiny_tasks, config);
    expect(
        training.tasks.size() == 2 &&
            training.outcomes.size() == 2 &&
            training.accounting.actor_perspectives == 4,
        "tiny collection task accounting");
    expect(
        !training.records.empty() &&
            training.records.size() % 2 == 0,
        "tiny collection produced no paired records");
    expect_near(
        training.accounting.total_weight,
        4.0, 1.0e-12,
        "tiny collection actor-game mass");
    expect(
        training.accounting.hidden.pooled.attempted ==
                training.records.size() &&
            training.accounting.hidden.bit_identical(),
        "tiny collection hidden audit");

    std::array<std::array<double, 2>, 2>
        perspective_mass{};
    bool saw_multi_state_trace = false;
    for (const oc::TrainingRecord& record :
         training.records) {
        saw_multi_state_trace =
            saw_multi_state_trace ||
            record.trace_size > 1;
        perspective_mass[record.physical_game]
                        [record.perspective] += record.weight;
        expect(
            record.weight ==
                1.0 /
                    static_cast<double>(
                        record.trace_size),
            "tiny collection row weight");
        expect(
            old_school::audit_common::bit_identical(
                record.target,
                old_school::
                    learned_discounted_terminal_target(
                        training.outcomes
                            [record.physical_game],
                        record.perspective)),
            "collected target differs from terminal target");
    }
    expect(
        saw_multi_state_trace,
        "tiny fixture did not exercise trace weighting");
    for (const auto& game : perspective_mass) {
        expect_near(
            game[0], 1.0, 1.0e-12,
            "seat-zero actor-game trace mass");
        expect_near(
            game[1], 1.0, 1.0e-12,
            "seat-one actor-game trace mass");
    }

    const auto examples = oc::training_examples(training);
    expect(
        examples.size() == training.records.size(),
        "optimizer example count");
    for (std::size_t index = 0;
         index < examples.size(); ++index) {
        expect(
            examples[index].features ==
                    training.records[index].features &&
                examples[index].target ==
                    training.records[index].target &&
                examples[index].weight ==
                    training.records[index].weight,
            "optimizer input changed a collected row");
    }

    oc::CollectionConfig parallel_config = config;
    parallel_config.worker_count = 2;
    const oc::TrainingCorpus parallel =
        oc::collect_training_corpus(
            parent, tiny_tasks, parallel_config);
    expect(
        parallel.tasks == training.tasks &&
            parallel.outcomes == training.outcomes &&
            parallel.records == training.records &&
            parallel.hashes == training.hashes &&
            parallel.accounting.schedule ==
                training.accounting.schedule &&
            parallel.accounting.actor_perspectives ==
                training.accounting.actor_perspectives &&
            parallel.accounting.records ==
                training.accounting.records &&
            parallel.accounting.total_weight ==
                training.accounting.total_weight &&
            parallel.accounting.by_deck ==
                training.accounting.by_deck &&
            parallel.accounting.hidden ==
                training.accounting.hidden,
        "worker count changed deterministic collection");

    std::vector<old_school::LearnedCriticTrainingExample>
        update_examples;
    update_examples.reserve(examples.size());
    for (const auto& example : examples) {
        update_examples.push_back({
            .features = example.features,
            .target = example.target,
        });
    }
    const auto calibrated =
        old_school::update_learned_value_model(
            parent, update_examples,
            {
                .epochs = 1,
                .learning_rate = 0.001,
                .root_seed = 0x0C100003ULL,
                .member_training_tag = 0x0C100004ULL,
            });
    expect(
        old_school::learned_model_fingerprint(
            calibrated) !=
            old_school::learned_model_fingerprint(parent),
        "tiny update did not produce a distinct model");

    const oc::HoldoutCorpus holdout =
        oc::collect_holdout_corpus(
            parent, calibrated, tiny_tasks, config);
    expect(
        holdout.records.size() == training.records.size(),
        "repeated source collection changed trace size");
    expect(
        holdout.outcomes == training.outcomes &&
            holdout.hashes.schedule ==
                training.hashes.schedule &&
            holdout.hashes.outcomes ==
                training.hashes.outcomes &&
            holdout.hashes.features ==
                training.hashes.features &&
            holdout.hashes.targets ==
                training.hashes.targets &&
            holdout.hashes.weights ==
                training.hashes.weights &&
            holdout.hashes.parent_leaf_predictions ==
                training.hashes.parent_leaf_predictions &&
            holdout.hashes.parent_predictions ==
                training.hashes.parent_predictions,
        "repeated source collection was not bit deterministic");
    bool saw_candidate_prediction_change = false;
    for (const oc::HoldoutRecord& record :
         holdout.records) {
        expect(
            old_school::audit_common::bit_identical(
                record.target,
                old_school::
                    learned_discounted_terminal_target(
                        holdout.outcomes
                            [record.physical_game],
                        record.perspective)),
            "holdout target differs from terminal target");
        saw_candidate_prediction_change =
            saw_candidate_prediction_change ||
            record.parent_leaf_predictions !=
                record.candidate_leaf_predictions ||
            record.parent_prediction !=
                record.candidate_prediction;
    }
    expect(
        saw_candidate_prediction_change,
        "distinct holdout candidate was not evaluated");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "schedule is deterministic and balanced",
        test_schedule_is_deterministic_and_balanced);
    tests.run(
        "hidden exchange is nonvacuous and private",
        test_hidden_exchange_is_nonvacuous_and_private);
    tests.run(
        "weighted CR1 matches declared formula",
        test_weighted_cr1_matches_declared_formula);
    tests.run(
        "holdout metrics are weighted and deck balanced",
        test_holdout_metrics_are_weighted_and_deck_balanced);
    tests.run(
        "high-row metric mass is stable",
        test_high_row_metric_mass_is_stable);
    tests.run(
        "gate is conjunctive and thresholds are exact",
        test_gate_is_conjunctive_and_thresholds_are_exact);
    tests.run(
        "tiny collection is deterministic and weighted",
        test_tiny_collection_is_deterministic_and_weighted);
    return tests.finish();
}
