#include "old_school/target_factorial_audit.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::target_factorial_audit {
namespace {

constexpr double kLogClamp = 1.0e-12;
constexpr double kMaterialBias = 0.05;

using ActorKey = std::pair<std::size_t, std::size_t>;

constexpr std::size_t arm_index(TargetArm arm) {
    return static_cast<std::size_t>(arm);
}

constexpr std::size_t contrast_index(Contrast contrast) {
    return static_cast<std::size_t>(contrast);
}

constexpr std::pair<TargetArm, TargetArm> contrast_arms(
    Contrast contrast) {
    switch (contrast) {
    case Contrast::RecordOffset8MinusRecordOffset4:
        return {TargetArm::RecordOffset4,
                TargetArm::RecordOffset8};
    case Contrast::CalendarTurn4MinusRecordOffset4:
        return {TargetArm::RecordOffset4,
                TargetArm::CalendarTurn4};
    case Contrast::CalendarTurn8MinusRecordOffset4:
        return {TargetArm::RecordOffset4,
                TargetArm::CalendarTurn8};
    case Contrast::CalendarTurn8MinusCalendarTurn4:
        return {TargetArm::CalendarTurn4,
                TargetArm::CalendarTurn8};
    case Contrast::CalendarTurn8MinusRecordOffset8:
        return {TargetArm::RecordOffset8,
                TargetArm::CalendarTurn8};
    }
    throw std::invalid_argument("CT8 contrast is out of range");
}

double target(const AuditRecord& record, TargetArm arm) {
    switch (arm) {
    case TargetArm::RecordOffset4:
        return record.control_target;
    case TargetArm::RecordOffset8:
        return record.record_offset8_target;
    case TargetArm::CalendarTurn4:
        return record.treatment_target;
    case TargetArm::CalendarTurn8:
        return record.calendar_turn8_target;
    }
    throw std::invalid_argument("CT8 target arm is out of range");
}

const std::optional<std::size_t>& future_index(
    const AuditRecord& record, TargetArm arm) {
    switch (arm) {
    case TargetArm::RecordOffset4:
        return record.control_future_index;
    case TargetArm::RecordOffset8:
        return record.record_offset8_future_index;
    case TargetArm::CalendarTurn4:
        return record.treatment_future_index;
    case TargetArm::CalendarTurn8:
        return record.calendar_turn8_future_index;
    }
    throw std::invalid_argument("CT8 target arm is out of range");
}

bool bit_identical(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

void require_probability(double value, std::string_view field) {
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
        throw std::invalid_argument(
            std::string(field) +
            " must be finite and in [0, 1]");
    }
}

std::size_t deck_index(DeckId deck) {
    const std::size_t index = static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::invalid_argument(
            "CT8 record contains an invalid deck");
    }
    return index;
}

std::string_view audit_deck_name(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return "Green";
    case DeckId::Red:
        return "Red";
    case DeckId::Blue:
        return "Blue";
    case DeckId::White:
        return "White";
    case DeckId::RUAggro:
        return "RU Aggro";
    }
    throw std::invalid_argument("CT8 deck is out of range");
}

RootTurnStratum root_stratum(std::size_t turn) {
    if (turn <= 3) {
        return RootTurnStratum::Early;
    }
    if (turn <= 7) {
        return RootTurnStratum::Middle;
    }
    return RootTurnStratum::Late;
}

std::size_t stratum_index(RootTurnStratum stratum) {
    return static_cast<std::size_t>(stratum);
}

double soft_log_loss(double prediction, double outcome) {
    prediction =
        std::clamp(prediction, kLogClamp, 1.0 - kLogClamp);
    return -outcome * std::log(prediction) -
           (1.0 - outcome) * std::log(1.0 - prediction);
}

template <typename Function>
ClusteredEstimate estimate_rows(
    const std::vector<const AuditRecord*>& rows,
    Function metric) {
    std::vector<terminal_weight_eval::ClusteredValue> values;
    values.reserve(rows.size());
    for (const AuditRecord* row : rows) {
        values.push_back({
            .cluster = row->physical_game,
            .value = metric(*row),
        });
    }
    return terminal_weight_eval::cr1_clustered_estimate(values);
}

template <typename Function>
ClusteredEstimate estimate_actor_games(
    const std::vector<const AuditRecord*>& rows,
    Function metric) {
    struct Sum {
        long double value = 0.0L;
        std::size_t count = 0;
    };
    std::map<ActorKey, Sum> sums;
    for (const AuditRecord* row : rows) {
        auto& sum =
            sums[{row->physical_game, row->perspective}];
        sum.value += metric(*row);
        ++sum.count;
    }
    std::vector<terminal_weight_eval::ClusteredValue> values;
    values.reserve(sums.size());
    for (const auto& [key, sum] : sums) {
        if (sum.count == 0) {
            throw std::logic_error(
                "CT8 actor-game metric has zero rows");
        }
        values.push_back({
            .cluster = key.first,
            .value = static_cast<double>(
                sum.value /
                static_cast<long double>(sum.count)),
        });
    }
    return terminal_weight_eval::cr1_clustered_estimate(values);
}

template <typename Function>
ClusteredEstimate estimate(
    const std::vector<const AuditRecord*>& rows,
    bool equal_actor_games, Function metric) {
    return equal_actor_games
               ? estimate_actor_games(rows, metric)
               : estimate_rows(rows, metric);
}

TargetMetrics score_target(
    const std::vector<const AuditRecord*>& rows,
    TargetArm arm, bool equal_actor_games) {
    TargetMetrics result;
    result.brier = estimate(
        rows, equal_actor_games,
        [&](const AuditRecord& row) {
            const double error =
                target(row, arm) - row.terminal_target;
            return error * error;
        });
    result.soft_log_loss = estimate(
        rows, equal_actor_games,
        [&](const AuditRecord& row) {
            return soft_log_loss(
                target(row, arm), row.terminal_target);
        });
    result.signed_bias = estimate(
        rows, equal_actor_games,
        [&](const AuditRecord& row) {
            return target(row, arm) - row.terminal_target;
        });

    std::map<ActorKey, std::pair<long double, std::size_t>>
        actor_targets;
    long double sum = 0.0L;
    long double squared_sum = 0.0L;
    std::size_t count = 0;
    for (const AuditRecord* row : rows) {
        const double value = target(*row, arm);
        if (equal_actor_games) {
            auto& actor = actor_targets[
                {row->physical_game, row->perspective}];
            actor.first += value;
            ++actor.second;
            continue;
        }
        sum += value;
        squared_sum +=
            static_cast<long double>(value) * value;
        ++count;
        if (value <= 0.01 || value >= 0.99) {
            ++result.saturated_targets;
        }
    }
    if (equal_actor_games) {
        for (const auto& [key, actor] : actor_targets) {
            static_cast<void>(key);
            const double value = static_cast<double>(
                actor.first /
                static_cast<long double>(actor.second));
            sum += value;
            squared_sum +=
                static_cast<long double>(value) * value;
            ++count;
            if (value <= 0.01 || value >= 0.99) {
                ++result.saturated_targets;
            }
        }
    }
    if (count == 0) {
        throw std::invalid_argument(
            "CT8 target metric scope is empty");
    }
    result.target_mean = static_cast<double>(
        sum / static_cast<long double>(count));
    result.target_variance = std::max(
        0.0,
        static_cast<double>(
            squared_sum / static_cast<long double>(count) -
            static_cast<long double>(result.target_mean) *
                result.target_mean));
    result.saturation_fraction =
        static_cast<double>(result.saturated_targets) /
        static_cast<double>(count);
    return result;
}

DeltaMetrics score_delta(
    const std::vector<const AuditRecord*>& rows,
    TargetArm baseline, TargetArm candidate,
    bool equal_actor_games) {
    DeltaMetrics result;
    result.brier_delta = estimate(
        rows, equal_actor_games,
        [&](const AuditRecord& row) {
            const double baseline_error =
                target(row, baseline) - row.terminal_target;
            const double candidate_error =
                target(row, candidate) - row.terminal_target;
            return candidate_error * candidate_error -
                   baseline_error * baseline_error;
        });
    result.soft_log_loss_delta = estimate(
        rows, equal_actor_games,
        [&](const AuditRecord& row) {
            return soft_log_loss(
                       target(row, candidate),
                       row.terminal_target) -
                   soft_log_loss(
                       target(row, baseline),
                       row.terminal_target);
        });
    result.signed_bias_delta = estimate(
        rows, equal_actor_games,
        [&](const AuditRecord& row) {
            return target(row, candidate) -
                   target(row, baseline);
        });
    return result;
}

DeltaMetrics score_interaction(
    const std::vector<const AuditRecord*>& rows,
    bool equal_actor_games) {
    const auto interaction =
        [](const AuditRecord& row,
           const auto& per_arm_metric) {
            return (per_arm_metric(
                        row, TargetArm::CalendarTurn8) -
                    per_arm_metric(
                        row, TargetArm::CalendarTurn4)) -
                   (per_arm_metric(
                        row, TargetArm::RecordOffset8) -
                    per_arm_metric(
                        row, TargetArm::RecordOffset4));
        };
    DeltaMetrics result;
    result.brier_delta = estimate(
        rows, equal_actor_games,
        [&](const AuditRecord& row) {
            return interaction(
                row,
                [](const AuditRecord& item, TargetArm arm) {
                    const double error =
                        target(item, arm) -
                        item.terminal_target;
                    return error * error;
                });
        });
    result.soft_log_loss_delta = estimate(
        rows, equal_actor_games,
        [&](const AuditRecord& row) {
            return interaction(
                row,
                [](const AuditRecord& item, TargetArm arm) {
                    return soft_log_loss(
                        target(item, arm),
                        item.terminal_target);
                });
        });
    result.signed_bias_delta = estimate(
        rows, equal_actor_games,
        [&](const AuditRecord& row) {
            return interaction(
                row,
                [](const AuditRecord& item, TargetArm arm) {
                    return target(item, arm) -
                           item.terminal_target;
                });
        });
    return result;
}

WeightingMetrics score_weighting(
    const std::vector<const AuditRecord*>& rows,
    bool equal_actor_games) {
    WeightingMetrics result;
    for (std::size_t index = 0;
         index < kTargetArmCount; ++index) {
        result.arms[index] = score_target(
            rows, static_cast<TargetArm>(index),
            equal_actor_games);
    }
    for (std::size_t index = 0;
         index < kContrastCount; ++index) {
        const auto [baseline, candidate] =
            contrast_arms(static_cast<Contrast>(index));
        result.contrasts[index] = score_delta(
            rows, baseline, candidate,
            equal_actor_games);
    }
    result.interaction =
        score_interaction(rows, equal_actor_games);
    return result;
}

RowSetMetrics score_row_set(
    const std::vector<const AuditRecord*>& rows,
    const std::set<ActorKey>& actor_universe) {
    RowSetMetrics result;
    if (rows.empty()) {
        result.excluded_actor_games = actor_universe.size();
        return result;
    }
    result.available = true;
    result.records = rows.size();
    std::set<ActorKey> actors;
    std::set<std::size_t> physical_games;
    for (const AuditRecord* row : rows) {
        actors.emplace(row->physical_game, row->perspective);
        physical_games.insert(row->physical_game);
    }
    result.actor_games = actors.size();
    result.excluded_actor_games =
        actor_universe.size() - actors.size();
    result.physical_games = physical_games.size();
    result.record_weighted =
        score_weighting(rows, false);
    result.equal_actor_game =
        score_weighting(rows, true);
    return result;
}

ScopeMetrics score_scope(
    const std::vector<const AuditRecord*>& rows) {
    if (rows.empty()) {
        throw std::invalid_argument(
            "CT8 metric scope contains no records");
    }
    std::set<ActorKey> actors;
    std::vector<const AuditRecord*> four_common;
    std::array<std::vector<const AuditRecord*>,
               kContrastCount>
        pair_common;
    std::array<std::vector<const AuditRecord*>,
               kRootTurnStratumCount>
        strata;
    ScopeMetrics result;

    for (const AuditRecord* row : rows) {
        actors.emplace(row->physical_game, row->perspective);
        std::array<bool, kTargetArmCount> eligible{};
        for (std::size_t arm = 0;
             arm < kTargetArmCount; ++arm) {
            eligible[arm] =
                future_index(
                    *row, static_cast<TargetArm>(arm))
                    .has_value();
            if (eligible[arm]) {
                ++result.bootstrapped_records[arm];
            } else {
                ++result.terminal_tail_records[arm];
            }
        }

        if (eligible[arm_index(TargetArm::RecordOffset4)]) {
            ++result
                  .record_offset4_turn_distance_histogram
                      [*row->control_turn_distance];
        }
        if (eligible[arm_index(TargetArm::RecordOffset8)]) {
            ++result
                  .record_offset8_turn_distance_histogram
                      [*row->record_offset8_turn_distance];
        }

        if (std::all_of(
                eligible.begin(), eligible.end(),
                [](bool present) { return present; })) {
            four_common.push_back(row);
            strata[stratum_index(root_stratum(row->root_turn))]
                .push_back(row);
        }
        for (std::size_t contrast = 0;
             contrast < kContrastCount; ++contrast) {
            const auto [baseline, candidate] =
                contrast_arms(
                    static_cast<Contrast>(contrast));
            if (eligible[arm_index(baseline)] &&
                eligible[arm_index(candidate)]) {
                pair_common[contrast].push_back(row);
            }
        }
    }

    result.all_records = score_row_set(rows, actors);
    result.four_arm_common =
        score_row_set(four_common, actors);
    for (std::size_t index = 0;
         index < kContrastCount; ++index) {
        result.pair_common[index] =
            score_row_set(pair_common[index], actors);
    }
    for (std::size_t index = 0;
         index < kRootTurnStratumCount; ++index) {
        result.four_arm_common_by_root_turn[index] =
            score_row_set(strata[index], actors);
    }
    return result;
}

bool has_material_bias(const TargetMetrics& metrics) {
    return std::abs(metrics.signed_bias.mean) >=
               kMaterialBias &&
           (metrics.signed_bias.confidence_lower_95 > 0.0 ||
            metrics.signed_bias.confidence_upper_95 < 0.0);
}

bool same_sign(double first, double second) {
    return (first > 0.0 && second > 0.0) ||
           (first < 0.0 && second < 0.0);
}

bool no_new_material_bias_in_scope(
    const ScopeMetrics& scope) {
    const auto& metrics =
        scope.all_records.record_weighted;
    const auto& control =
        metrics.arms[arm_index(TargetArm::RecordOffset4)];
    const auto& treatment =
        metrics.arms[arm_index(TargetArm::CalendarTurn8)];
    return !has_material_bias(treatment) ||
           (has_material_bias(control) &&
            same_sign(
                treatment.signed_bias.mean,
                control.signed_bias.mean));
}

bool direction_and_shrink(
    const ScopeMetrics& scope, bool upward) {
    if (!scope.four_arm_common.available) {
        return false;
    }
    constexpr std::size_t contrast =
        contrast_index(
            Contrast::CalendarTurn8MinusRecordOffset4);
    constexpr std::size_t control =
        arm_index(TargetArm::RecordOffset4);
    constexpr std::size_t treatment =
        arm_index(TargetArm::CalendarTurn8);
    const auto& record =
        scope.four_arm_common.record_weighted;
    const auto& actor =
        scope.four_arm_common.equal_actor_game;
    const double record_delta =
        record.contrasts[contrast].signed_bias_delta.mean;
    const double actor_delta =
        actor.contrasts[contrast].signed_bias_delta.mean;
    const bool direction =
        upward
            ? record_delta > 0.0 && actor_delta > 0.0
            : record_delta < 0.0 && actor_delta < 0.0;
    return direction &&
           std::abs(
               record.arms[treatment].signed_bias.mean) <
               std::abs(
                   record.arms[control].signed_bias.mean) &&
           std::abs(
               actor.arms[treatment].signed_bias.mean) <
               std::abs(
                   actor.arms[control].signed_bias.mean);
}

bool is_hex_digest(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               });
}

std::string format_real(double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(
                  std::numeric_limits<double>::max_digits10)
           << value;
    return output.str();
}

std::string sanitize_tsv(std::string_view value) {
    std::string result(value);
    for (char& character : result) {
        if (character == '\t' || character == '\n' ||
            character == '\r') {
            character = ' ';
        }
    }
    return result;
}

} // namespace

std::string_view target_arm_name(TargetArm arm) {
    switch (arm) {
    case TargetArm::RecordOffset4:
        return "RO4";
    case TargetArm::RecordOffset8:
        return "RO8";
    case TargetArm::CalendarTurn4:
        return "CT4";
    case TargetArm::CalendarTurn8:
        return "CT8";
    }
    throw std::invalid_argument("CT8 target arm is out of range");
}

std::string_view contrast_name(Contrast contrast) {
    switch (contrast) {
    case Contrast::RecordOffset8MinusRecordOffset4:
        return "RO8-RO4";
    case Contrast::CalendarTurn4MinusRecordOffset4:
        return "CT4-RO4";
    case Contrast::CalendarTurn8MinusRecordOffset4:
        return "CT8-RO4";
    case Contrast::CalendarTurn8MinusCalendarTurn4:
        return "CT8-CT4";
    case Contrast::CalendarTurn8MinusRecordOffset8:
        return "CT8-RO8";
    }
    throw std::invalid_argument("CT8 contrast is out of range");
}

MetricsReport score_four_arm_records(
    std::span<const AuditRecord> records) {
    if (records.empty()) {
        throw std::invalid_argument(
            "CT8 scoring requires records");
    }

    MetricsReport report;
    report.ro4_ct4_reference = ta4::score_records(records);

    std::set<std::tuple<std::size_t, std::size_t,
                        std::size_t>>
        identities;
    std::vector<const AuditRecord*> pooled;
    std::array<std::vector<const AuditRecord*>, kDeckCount>
        by_deck;
    pooled.reserve(records.size());
    for (const AuditRecord& record : records) {
        deck_index(record.deck);
        if (record.perspective >= 2) {
            throw std::invalid_argument(
                "CT8 perspective is out of range");
        }
        require_probability(
            record.record_offset8_target,
            "CT8 RO8 target");
        require_probability(
            record.calendar_turn8_target,
            "CT8 CT8 target");
        const bool ro8 =
            record.record_offset8_future_index.has_value();
        const bool ct8 =
            record.calendar_turn8_future_index.has_value();
        if (ro8 !=
                record.record_offset8_turn_distance
                    .has_value() ||
            ct8 !=
                record.calendar_turn8_turn_distance
                    .has_value()) {
            throw std::invalid_argument(
                "CT8 future-index and turn-distance presence "
                "must match");
        }
        if (ro8 &&
            (record.root_index >
                 std::numeric_limits<std::size_t>::max() -
                     ta4::kRecordOffset8Distance ||
             *record.record_offset8_future_index !=
                 record.root_index +
                     ta4::kRecordOffset8Distance ||
             *record.record_offset8_future_index <=
                 record.root_index ||
             *record.record_offset8_turn_distance >
                 ta4::kRecordOffset8Distance)) {
            throw std::invalid_argument(
                "CT8 RO8 future is not record offset eight");
        }
        if (ct8 &&
            (*record.calendar_turn8_future_index <=
                 record.root_index ||
             record.calendar_turn8_turn_distance !=
                 std::optional<std::size_t>(
                     ta4::kCalendarTurn8Advances))) {
            throw std::invalid_argument(
                "CT8 calendar future is not eight turns ahead");
        }
        if (ro8 && !record.control_future_index.has_value()) {
            throw std::invalid_argument(
                "CT8 RO8 future exists without RO4 future");
        }
        if (ct8 && !record.treatment_future_index.has_value()) {
            throw std::invalid_argument(
                "CT8 CT8 future exists without CT4 future");
        }
        if (!ro8 &&
            !bit_identical(
                record.record_offset8_target,
                record.terminal_target)) {
            throw std::invalid_argument(
                "CT8 RO8 tail is not bit-identical to z");
        }
        if (!ct8 &&
            !bit_identical(
                record.calendar_turn8_target,
                record.terminal_target)) {
            throw std::invalid_argument(
                "CT8 CT8 tail is not bit-identical to z");
        }
        if (!identities
                 .emplace(
                     record.physical_game,
                     record.perspective,
                     record.root_index)
                 .second) {
            throw std::invalid_argument(
                "CT8 record identity is duplicated");
        }
        pooled.push_back(&record);
        by_deck[deck_index(record.deck)].push_back(&record);
    }

    report.pooled = score_scope(pooled);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        report.by_deck[deck] =
            score_scope(by_deck[deck]);
    }
    return report;
}

ScientificReport make_scientific_report(
    const Capture& capture, std::uint64_t seed,
    std::size_t generation, std::size_t balanced_blocks) {
    if (balanced_blocks == 0 ||
        capture.physical_games == 0 ||
        capture.perspectives !=
            2 * capture.physical_games ||
        capture.records.empty()) {
        throw std::invalid_argument(
            "CT8 capture accounting is incomplete");
    }
    for (const std::string* hash : {
             &capture.schedule_hash,
             &capture.trace_hash,
             &capture.outcome_hash,
             &capture.control_target_hash,
             &capture.record_offset8_target_hash,
             &capture.treatment_target_hash,
             &capture.calendar_turn8_target_hash,
             &capture.scoring_hash}) {
        if (!is_hex_digest(*hash)) {
            throw std::invalid_argument(
                "CT8 capture contains a malformed digest");
        }
    }

    ScientificReport report;
    report.seed = seed;
    report.generation = generation;
    report.balanced_blocks = balanced_blocks;
    report.physical_games = capture.physical_games;
    report.perspectives = capture.perspectives;
    report.deck_seat_started_counts =
        capture.deck_seat_started_counts;
    report.metrics = score_four_arm_records(capture.records);
    report.schedule_balanced =
        capture.physical_games ==
            balanced_blocks *
                learned_iteration::kBalancedScheduleGames &&
        capture.perspectives ==
            2 * balanced_blocks *
                learned_iteration::kBalancedScheduleGames;
    const std::size_t expected_cell = 4 * balanced_blocks;
    for (const auto& deck :
         capture.deck_seat_started_counts) {
        for (const auto& seat : deck) {
            for (const std::size_t count : seat) {
                report.schedule_balanced =
                    report.schedule_balanced &&
                    count == expected_cell;
            }
        }
    }

    const auto& common =
        report.metrics.pooled.four_arm_common;
    report.common_coverage_passed =
        common.available &&
        common.actor_games >= kMinimumCommonActorGames &&
        common.physical_games >=
            kMinimumCommonPhysicalGames;
    for (const ScopeMetrics& deck :
         report.metrics.by_deck) {
        report.common_coverage_passed =
            report.common_coverage_passed &&
            deck.four_arm_common.actor_games >=
                kMinimumCommonActorGamesPerDeck;
    }
    const auto& early_green =
        report.metrics.by_deck[
            static_cast<std::size_t>(DeckId::Green)]
            .four_arm_common_by_root_turn[
                static_cast<std::size_t>(
                    RootTurnStratum::Early)];
    report.common_coverage_passed =
        report.common_coverage_passed &&
        early_green.available &&
        early_green.records >=
            kMinimumEarlyGreenRecords &&
        early_green.actor_games >=
            kMinimumEarlyGreenActorGames;
    if (early_green.available) {
        const auto& control =
            early_green.record_weighted.arms[
                arm_index(TargetArm::RecordOffset4)]
                .signed_bias;
        report.early_green_control_qualified =
            control.mean > 0.0 &&
            control.confidence_lower_95 > 0.0;
    }

    report.trace_invariants_passed =
        capture.trace_invariants_passed;
    report.record_offset4_identity_passed =
        capture.control_identity_passed;
    report.calendar_turn4_distance_passed =
        capture.treatment_distance_passed;
    report.calendar_turn4_earliest_passed =
        capture.treatment_earliest_passed;
    report.record_offset8_identity_passed =
        capture.record_offset8_identity_passed;
    report.calendar_turn8_distance_passed =
        capture.calendar_turn8_distance_passed;
    report.calendar_turn8_earliest_passed =
        capture.calendar_turn8_earliest_passed;
    report.tail_identity_passed =
        capture.tail_identity_passed &&
        capture.eight_tail_identity_passed;
    report.hidden_repartition_passed =
        capture.hidden_repartition_passed;
    report.hidden_repartition_states =
        capture.hidden_repartition_states;
    report.schedule_hash = capture.schedule_hash;
    report.trace_hash = capture.trace_hash;
    report.outcome_hash = capture.outcome_hash;
    report.record_offset4_target_hash =
        capture.control_target_hash;
    report.record_offset8_target_hash =
        capture.record_offset8_target_hash;
    report.calendar_turn4_target_hash =
        capture.treatment_target_hash;
    report.calendar_turn8_target_hash =
        capture.calendar_turn8_target_hash;
    report.scoring_hash = capture.scoring_hash;
    return report;
}

GateReport evaluate_gate(const ScientificReport& report) {
    GateReport gate;
    constexpr std::size_t ro4 =
        arm_index(TargetArm::RecordOffset4);
    constexpr std::size_t ro8 =
        arm_index(TargetArm::RecordOffset8);
    constexpr std::size_t ct4 =
        arm_index(TargetArm::CalendarTurn4);
    constexpr std::size_t ct8 =
        arm_index(TargetArm::CalendarTurn8);
    constexpr std::size_t ct8_ro4 =
        contrast_index(
            Contrast::CalendarTurn8MinusRecordOffset4);
    constexpr std::size_t ct8_ct4 =
        contrast_index(
            Contrast::CalendarTurn8MinusCalendarTurn4);
    constexpr std::size_t ct8_ro8 =
        contrast_index(
            Contrast::CalendarTurn8MinusRecordOffset8);

    const auto& pooled = report.metrics.pooled;
    const auto& green =
        report.metrics.by_deck[
            static_cast<std::size_t>(DeckId::Green)];
    const auto& blue =
        report.metrics.by_deck[
            static_cast<std::size_t>(DeckId::Blue)];
    const auto& ru =
        report.metrics.by_deck[
            static_cast<std::size_t>(DeckId::RUAggro)];
    const auto& early_green =
        green.four_arm_common_by_root_turn[
            static_cast<std::size_t>(
                RootTurnStratum::Early)];
    const auto& early_record =
        early_green.record_weighted;
    const auto& early_actor =
        early_green.equal_actor_game;

    gate.early_green_primary =
        early_green.available &&
        early_record.contrasts[ct8_ro4]
                .signed_bias_delta.mean <=
            -0.010 &&
        early_record.contrasts[ct8_ro4]
                .signed_bias_delta.confidence_upper_95 <
            0.0 &&
        early_actor.contrasts[ct8_ro4]
                .signed_bias_delta.mean <
            0.0;
    gate.early_green_constituent_advantage =
        early_green.available &&
        std::abs(
            early_record.arms[ct8].signed_bias.mean) +
                0.005 <=
            std::abs(
                early_record.arms[ct4].signed_bias.mean) &&
        std::abs(
            early_record.arms[ct8].signed_bias.mean) +
                0.005 <=
            std::abs(
                early_record.arms[ro8].signed_bias.mean) &&
        std::abs(
            early_actor.arms[ct8].signed_bias.mean) <
            std::abs(
                early_actor.arms[ct4].signed_bias.mean) &&
        std::abs(
            early_actor.arms[ct8].signed_bias.mean) <
            std::abs(
                early_actor.arms[ro8].signed_bias.mean);
    gate.early_green_interaction =
        early_green.available &&
        early_record.interaction.signed_bias_delta.mean <
            0.0 &&
        early_record.interaction.signed_bias_delta
                .confidence_upper_95 <
            0.0;

    const auto& green_record =
        green.four_arm_common.record_weighted;
    const auto& green_actor =
        green.four_arm_common.equal_actor_game;
    gate.whole_green_bias_shrank =
        green.four_arm_common.available &&
        std::abs(
            green_record.arms[ct8].signed_bias.mean) <
            std::abs(
                green_record.arms[ro4].signed_bias.mean) &&
        std::abs(
            green_actor.arms[ct8].signed_bias.mean) <
            std::abs(
                green_actor.arms[ro4].signed_bias.mean);

    const auto& pooled_common =
        pooled.four_arm_common.record_weighted;
    gate.pooled_loss_vs_control =
        pooled.four_arm_common.available &&
        pooled_common.contrasts[ct8_ro4]
                .brier_delta.confidence_upper_95 <
            0.0 &&
        pooled_common.contrasts[ct8_ro4]
                .soft_log_loss_delta
                .confidence_upper_95 <
            0.0;
    gate.pooled_loss_vs_constituents =
        pooled.four_arm_common.available &&
        pooled_common.contrasts[ct8_ct4]
                .brier_delta.mean <=
            0.0 &&
        pooled_common.contrasts[ct8_ct4]
                .brier_delta.confidence_upper_95 <
            0.001 &&
        pooled_common.contrasts[ct8_ct4]
                .soft_log_loss_delta.mean <=
            0.0 &&
        pooled_common.contrasts[ct8_ct4]
                .soft_log_loss_delta
                .confidence_upper_95 <
            0.001 &&
        pooled_common.contrasts[ct8_ro8]
                .brier_delta.mean <=
            0.0 &&
        pooled_common.contrasts[ct8_ro8]
                .brier_delta.confidence_upper_95 <
            0.001 &&
        pooled_common.contrasts[ct8_ro8]
                .soft_log_loss_delta.mean <=
            0.0 &&
        pooled_common.contrasts[ct8_ro8]
                .soft_log_loss_delta
                .confidence_upper_95 <
            0.001;

    const auto& pooled_all =
        pooled.all_records.record_weighted;
    gate.pooled_all_brier_best = true;
    for (const std::size_t arm : {ro4, ro8, ct4}) {
        gate.pooled_all_brier_best =
            gate.pooled_all_brier_best &&
            pooled_all.arms[ct8].brier.mean <=
                pooled_all.arms[arm].brier.mean;
    }

    gate.per_deck_all_brier_guard = true;
    gate.no_new_material_bias = true;
    for (const ScopeMetrics& deck : report.metrics.by_deck) {
        const auto& all = deck.all_records.record_weighted;
        for (const std::size_t arm : {ro4, ro8, ct4}) {
            gate.per_deck_all_brier_guard =
                gate.per_deck_all_brier_guard &&
                all.arms[ct8].brier.mean -
                        all.arms[arm].brier.mean <=
                    0.005;
        }
        gate.no_new_material_bias =
            gate.no_new_material_bias &&
            no_new_material_bias_in_scope(deck);
    }
    gate.blue_direction_and_shrink =
        direction_and_shrink(blue, true);
    gate.ru_direction_and_shrink =
        direction_and_shrink(ru, false);
    gate.red_white_no_new_material_bias = true;
    for (const DeckId deck : {DeckId::Red, DeckId::White}) {
        gate.red_white_no_new_material_bias =
            gate.red_white_no_new_material_bias &&
            no_new_material_bias_in_scope(
                report.metrics.by_deck[deck_index(deck)]);
    }

    gate.evidence_complete =
        report.common_coverage_passed &&
        report.early_green_control_qualified;
    gate.mechanical_invariants =
        report.schedule_balanced &&
        report.trace_invariants_passed &&
        report.record_offset4_identity_passed &&
        report.calendar_turn4_distance_passed &&
        report.calendar_turn4_earliest_passed &&
        report.record_offset8_identity_passed &&
        report.calendar_turn8_distance_passed &&
        report.calendar_turn8_earliest_passed &&
        report.tail_identity_passed &&
        report.hidden_repartition_passed;
    gate.passed =
        gate.early_green_primary &&
        gate.early_green_constituent_advantage &&
        gate.early_green_interaction &&
        gate.whole_green_bias_shrank &&
        gate.pooled_loss_vs_control &&
        gate.pooled_loss_vs_constituents &&
        gate.pooled_all_brier_best &&
        gate.per_deck_all_brier_guard &&
        gate.no_new_material_bias &&
        gate.blue_direction_and_shrink &&
        gate.ru_direction_and_shrink &&
        gate.red_white_no_new_material_bias &&
        gate.evidence_complete &&
        gate.mechanical_invariants;

    const auto failure =
        [&](bool condition, std::string message) {
            if (!condition) {
                gate.failures.push_back(std::move(message));
            }
        };
    failure(
        gate.early_green_primary,
        "early-Green CT8-RO4 primary direction, MDE, or "
        "precision failed");
    failure(
        gate.early_green_constituent_advantage,
        "early-Green CT8 absolute bias did not beat both "
        "constituents");
    failure(
        gate.early_green_interaction,
        "early-Green factorial interaction was not precisely "
        "negative");
    failure(
        gate.whole_green_bias_shrank,
        "whole-Green CT8 absolute bias did not shrink under "
        "both weights");
    failure(
        gate.pooled_loss_vs_control,
        "pooled common CT8 losses did not improve precisely "
        "versus RO4");
    failure(
        gate.pooled_loss_vs_constituents,
        "pooled common CT8 losses failed constituent guards");
    failure(
        gate.pooled_all_brier_best,
        "pooled all-row CT8 Brier was not best or tied");
    failure(
        gate.per_deck_all_brier_guard,
        "a deck all-row CT8 Brier exceeded an arm by >0.005");
    failure(
        gate.no_new_material_bias,
        "CT8 created a new material signed bias");
    failure(
        gate.blue_direction_and_shrink,
        "Blue CT8-RO4 bias did not move upward and shrink "
        "under both weights");
    failure(
        gate.ru_direction_and_shrink,
        "RU CT8-RO4 bias did not move downward and shrink "
        "under both weights");
    failure(
        gate.red_white_no_new_material_bias,
        "CT8 created a new material Red or White bias");
    failure(
        gate.evidence_complete,
        "CT8 coverage or positive early-Green RO4 control "
        "qualification was incomplete");
    failure(
        gate.mechanical_invariants,
        "CT8 mechanical invariants did not all pass");
    return gate;
}

ArtifactSnapshot require_artifact_snapshot(
    const std::string& artifact_path) {
    return terminal_weight_eval::snapshot_artifact(
        artifact_path);
}

bool infrastructure_complete(const AuditReport& report) {
    return report.artifact_unchanged &&
           report.repeated_report_bit_identical &&
           report.parent_fingerprint == kParentFingerprint &&
           report.scientific.common_coverage_passed &&
           report.scientific.early_green_control_qualified &&
           report.scientific.schedule_balanced &&
           report.scientific.trace_invariants_passed &&
           report.scientific
               .record_offset4_identity_passed &&
           report.scientific
               .calendar_turn4_distance_passed &&
           report.scientific
               .calendar_turn4_earliest_passed &&
           report.scientific
               .record_offset8_identity_passed &&
           report.scientific
               .calendar_turn8_distance_passed &&
           report.scientific
               .calendar_turn8_earliest_passed &&
           report.scientific.tail_identity_passed &&
           report.scientific.hidden_repartition_passed &&
           report.scientific.hidden_repartition_states > 0 &&
           report.gate.evidence_complete &&
           report.gate.mechanical_invariants;
}

AuditReport run_canonical_ct8_audit(std::ostream& progress) {
    constexpr std::size_t training_games = 800;
    constexpr std::size_t generations = 16;
    const std::string artifact_path =
        learned_value_challenger_cache_path(
            training_games, kDefaultLearnedTrainingSeed,
            generations);

    AuditReport report;
    report.artifact_before =
        require_artifact_snapshot(artifact_path);
    bool snapshot_verified = false;
    const auto verify_snapshot = [&] {
        report.artifact_after =
            require_artifact_snapshot(artifact_path);
        report.artifact_unchanged =
            report.artifact_before ==
            report.artifact_after;
        if (!report.artifact_unchanged) {
            throw std::runtime_error(
                "CT8 frozen C16 artifact content, size, or "
                "timestamp changed during the audit");
        }
        snapshot_verified = true;
    };

    try {
        progress
            << "Loading exact frozen Environment-v3 C16..."
            << std::flush;
        const auto artifact =
            load_learned_value_challenger_artifact(
                artifact_path, training_games,
                kDefaultLearnedTrainingSeed, generations);
        const auto parent = artifact.model();
        report.parent_fingerprint =
            learned_model_fingerprint(parent);
        if (report.parent_fingerprint !=
            kParentFingerprint) {
            throw std::runtime_error(
                "CT8 frozen C16 fingerprint mismatch: "
                "expected " +
                std::string(kParentFingerprint) +
                ", got " + report.parent_fingerprint);
        }
        report.parent_components =
            learned_model_component_fingerprints(parent);
        if (learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
            throw std::runtime_error(
                "CT8 parent uses the wrong critic schema");
        }
        if (require_artifact_snapshot(artifact_path) !=
            report.artifact_before) {
            throw std::runtime_error(
                "CT8 frozen C16 artifact changed while loading");
        }
        progress << " done\n";

        const auto tasks = ta4::audit_schedule(
            kAuditSeed, kAuditGeneration,
            kAuditBalancedBlocks);
        const std::size_t workers =
            std::min<std::size_t>(
                tasks.size(),
                std::max(
                    1U,
                    std::thread::hardware_concurrency()));
        const CaptureConfig config{
            .max_game_turns = 500,
            .worker_count = workers,
            .verify_hidden_repartition = true,
        };
        progress
            << "Constructing CT8-0 four-arm audit corpus twice "
               "(2,000 games each, K=1/H=4)..."
            << std::flush;
        const Capture first =
            ta4::collect(tasks, parent, config);
        if (require_artifact_snapshot(artifact_path) !=
            report.artifact_before) {
            throw std::runtime_error(
                "CT8 frozen C16 artifact changed after first "
                "collection");
        }
        const Capture second =
            ta4::collect(tasks, parent, config);
        report.scientific =
            target_factorial_audit::make_scientific_report(
            first, kAuditSeed, kAuditGeneration,
            kAuditBalancedBlocks);
        const ScientificReport second_report =
            target_factorial_audit::make_scientific_report(
                second, kAuditSeed, kAuditGeneration,
                kAuditBalancedBlocks);
        report.repeated_report_bit_identical =
            first == second &&
            report.scientific == second_report;
        progress << " done ("
                 << first.records.size()
                 << " trace-perspective records per capture)\n";

        verify_snapshot();
        report.gate = evaluate_gate(report.scientific);
        if (!infrastructure_complete(report)) {
            throw std::runtime_error(
                "CT8 audit evidence, control qualification, "
                "mechanics, hidden-zone, artifact, or "
                "repeatability checks were incomplete");
        }
        report.passed = report.gate.passed;
        return report;
    } catch (...) {
        if (!snapshot_verified) {
            verify_snapshot();
        }
        throw;
    }
}

void write_human_report(
    const AuditReport& report, std::ostream& output) {
    output
        << "\nCT8-0 record/calendar x four/eight target audit\n"
        << "  Parent: " << report.parent_fingerprint << '\n'
        << "  Artifact: "
        << (report.artifact_unchanged
                ? "UNCHANGED"
                : "CHANGED")
        << " (" << report.artifact_before.content_hash
        << ")\n"
        << "  Corpus: "
        << report.scientific.physical_games
        << " physical games, "
        << report.scientific.perspectives
        << " actor-games, "
        << report.scientific.metrics.pooled
               .all_records.records
        << " trace-perspective records\n"
        << "  Four-arm common: "
        << report.scientific.metrics.pooled
               .four_arm_common.records
        << " records, "
        << report.scientific.metrics.pooled
               .four_arm_common.actor_games
        << " actor-games, "
        << report.scientific.metrics.pooled
               .four_arm_common.physical_games
        << " physical games\n"
        << "  Repeated construction: "
        << (report.repeated_report_bit_identical
                ? "BIT-IDENTICAL"
                : "MISMATCH")
        << '\n'
        << "  Hidden repartitions: "
        << report.scientific.hidden_repartition_states
        << " changed states, "
        << (report.scientific.hidden_repartition_passed
                ? "PASS"
                : "FAIL")
        << '\n'
        << "  Hashes: schedule="
        << report.scientific.schedule_hash
        << "\n          trace="
        << report.scientific.trace_hash
        << "\n          outcome="
        << report.scientific.outcome_hash
        << "\n          RO4="
        << report.scientific.record_offset4_target_hash
        << "\n          RO8="
        << report.scientific.record_offset8_target_hash
        << "\n          CT4="
        << report.scientific.calendar_turn4_target_hash
        << "\n          CT8="
        << report.scientific.calendar_turn8_target_hash
        << "\n          scoring="
        << report.scientific.scoring_hash << '\n';

    const auto write_weighting =
        [&output](
            std::string_view label,
            const WeightingMetrics& metrics) {
            output << "    " << label << '\n';
            for (std::size_t arm = 0;
                 arm < kTargetArmCount; ++arm) {
                const auto& scored = metrics.arms[arm];
                output
                    << "      "
                    << target_arm_name(
                           static_cast<TargetArm>(arm))
                    << ": bias="
                    << format_real(scored.signed_bias.mean)
                    << ", Brier="
                    << format_real(scored.brier.mean)
                    << ", log="
                    << format_real(
                           scored.soft_log_loss.mean)
                    << ", target mean="
                    << format_real(scored.target_mean)
                    << ", variance="
                    << format_real(scored.target_variance)
                    << ", saturated="
                    << scored.saturated_targets << '/'
                    << scored.brier.records << '\n';
            }
            for (std::size_t contrast = 0;
                 contrast < kContrastCount; ++contrast) {
                const auto& delta =
                    metrics.contrasts[contrast];
                output
                    << "      "
                    << contrast_name(
                           static_cast<Contrast>(contrast))
                    << ": bias delta="
                    << format_real(
                           delta.signed_bias_delta.mean)
                    << " ["
                    << format_real(
                           delta.signed_bias_delta
                               .confidence_lower_95)
                    << ", "
                    << format_real(
                           delta.signed_bias_delta
                               .confidence_upper_95)
                    << "], Brier delta="
                    << format_real(delta.brier_delta.mean)
                    << " ["
                    << format_real(
                           delta.brier_delta
                               .confidence_lower_95)
                    << ", "
                    << format_real(
                           delta.brier_delta
                               .confidence_upper_95)
                    << "], log delta="
                    << format_real(
                           delta.soft_log_loss_delta.mean)
                    << " ["
                    << format_real(
                           delta.soft_log_loss_delta
                               .confidence_lower_95)
                    << ", "
                    << format_real(
                           delta.soft_log_loss_delta
                               .confidence_upper_95)
                    << "]\n";
            }
            output
                << "      interaction: bias="
                << format_real(
                       metrics.interaction
                           .signed_bias_delta.mean)
                << " ["
                << format_real(
                       metrics.interaction
                           .signed_bias_delta
                           .confidence_lower_95)
                << ", "
                << format_real(
                       metrics.interaction
                           .signed_bias_delta
                           .confidence_upper_95)
                << "], Brier="
                << format_real(
                       metrics.interaction.brier_delta.mean)
                << ", log="
                << format_real(
                       metrics.interaction
                           .soft_log_loss_delta.mean)
                << '\n';
        };
    const auto write_scope =
        [&](std::string_view name,
            const ScopeMetrics& scope) {
            output
                << "\n  " << name << ": all="
                << scope.all_records.records
                << ", four-common="
                << scope.four_arm_common.records
                << ", common actor-games="
                << scope.four_arm_common.actor_games
                << ", common physical-games="
                << scope.four_arm_common.physical_games
                << ", excluded actor-games="
                << scope.four_arm_common
                       .excluded_actor_games
                << '\n';
            write_weighting(
                "all / record-weighted",
                scope.all_records.record_weighted);
            write_weighting(
                "all / equal-actor-game",
                scope.all_records.equal_actor_game);
            if (scope.four_arm_common.available) {
                write_weighting(
                    "four-common / record-weighted",
                    scope.four_arm_common.record_weighted);
                write_weighting(
                    "four-common / equal-actor-game",
                    scope.four_arm_common.equal_actor_game);
            }
            for (std::size_t contrast = 0;
                 contrast < kContrastCount; ++contrast) {
                const auto& rows =
                    scope.pair_common[contrast];
                output
                    << "    pair-common "
                    << contrast_name(
                           static_cast<Contrast>(contrast))
                    << ": records=" << rows.records
                    << ", actor-games=" << rows.actor_games
                    << ", physical-games="
                    << rows.physical_games;
                if (rows.available) {
                    output
                        << ", record bias delta="
                        << format_real(
                               rows.record_weighted
                                   .contrasts[contrast]
                                   .signed_bias_delta.mean)
                        << ", equal-actor bias delta="
                        << format_real(
                               rows.equal_actor_game
                                   .contrasts[contrast]
                                   .signed_bias_delta.mean);
                }
                output << '\n';
            }
            constexpr std::array<
                std::string_view,
                kRootTurnStratumCount>
                stratum_names = {"<=3", "4-7", ">=8"};
            for (std::size_t stratum = 0;
                 stratum < stratum_names.size(); ++stratum) {
                const auto& rows =
                    scope.four_arm_common_by_root_turn[
                        stratum];
                output
                    << "    four-common root turn "
                    << stratum_names[stratum]
                    << ": records=" << rows.records
                    << ", actor-games=" << rows.actor_games;
                if (rows.available) {
                    const auto& record =
                        rows.record_weighted;
                    output
                        << ", RO4 bias="
                        << format_real(
                               record.arms[arm_index(
                                   TargetArm::RecordOffset4)]
                                   .signed_bias.mean)
                        << ", RO8 bias="
                        << format_real(
                               record.arms[arm_index(
                                   TargetArm::RecordOffset8)]
                                   .signed_bias.mean)
                        << ", CT4 bias="
                        << format_real(
                               record.arms[arm_index(
                                   TargetArm::CalendarTurn4)]
                                   .signed_bias.mean)
                        << ", CT8 bias="
                        << format_real(
                               record.arms[arm_index(
                                   TargetArm::CalendarTurn8)]
                                   .signed_bias.mean)
                        << ", interaction="
                        << format_real(
                               record.interaction
                                   .signed_bias_delta.mean)
                        << " ["
                        << format_real(
                               record.interaction
                                   .signed_bias_delta
                                   .confidence_lower_95)
                        << ", "
                        << format_real(
                               record.interaction
                                   .signed_bias_delta
                                   .confidence_upper_95)
                        << ']';
                }
                output << '\n';
            }
            output << "    bootstrapped/tails:";
            for (std::size_t arm = 0;
                 arm < kTargetArmCount; ++arm) {
                output
                    << ' '
                    << target_arm_name(
                           static_cast<TargetArm>(arm))
                    << '='
                    << scope.bootstrapped_records[arm]
                    << '/'
                    << scope.terminal_tail_records[arm];
            }
            output << '\n';
            output << "    RO4 physical-turn distances:";
            for (std::size_t distance = 0;
                 distance <
                 scope.record_offset4_turn_distance_histogram
                     .size();
                 ++distance) {
                output
                    << ' ' << distance << '='
                    << scope
                           .record_offset4_turn_distance_histogram
                               [distance];
            }
            output << '\n';
            output << "    RO8 physical-turn distances:";
            for (std::size_t distance = 0;
                 distance <
                 scope.record_offset8_turn_distance_histogram
                     .size();
                 ++distance) {
                output
                    << ' ' << distance << '='
                    << scope
                           .record_offset8_turn_distance_histogram
                               [distance];
            }
            output << '\n';
        };

    write_scope("Pooled", report.scientific.metrics.pooled);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        write_scope(
            audit_deck_name(static_cast<DeckId>(deck)),
            report.scientific.metrics.by_deck[deck]);
    }
    output
        << "\nScientific verdict: "
        << (report.passed ? "PASS" : "REJECT")
        << '\n';
    for (const std::string& failure :
         report.gate.failures) {
        output << "  - " << failure << '\n';
    }
}

void write_tsv_report(
    const AuditReport& report, std::ostream& output) {
    constexpr std::string_view schema = "ct8-audit-v1";
    output
        << "\nCT8_TSV_BEGIN\n"
        << "schema\trow\tscope\tsubject\tmetric\testimate\tse"
           "\tci_low\tci_high\trecords\tclusters\tstatus\tdetail\n";
    const auto row =
        [&output, schema](
            std::string_view type,
            std::string_view scope,
            std::string_view subject,
            std::string_view metric,
            std::string_view estimate_value,
            std::string_view standard_error,
            std::string_view low,
            std::string_view high,
            std::string_view records,
            std::string_view clusters,
            std::string_view status,
            std::string_view detail) {
            output
                << schema << '\t'
                << sanitize_tsv(type) << '\t'
                << sanitize_tsv(scope) << '\t'
                << sanitize_tsv(subject) << '\t'
                << sanitize_tsv(metric) << '\t'
                << sanitize_tsv(estimate_value) << '\t'
                << sanitize_tsv(standard_error) << '\t'
                << sanitize_tsv(low) << '\t'
                << sanitize_tsv(high) << '\t'
                << sanitize_tsv(records) << '\t'
                << sanitize_tsv(clusters) << '\t'
                << sanitize_tsv(status) << '\t'
                << sanitize_tsv(detail) << '\n';
        };
    const auto write_estimate =
        [&row](
            std::string_view scope,
            std::string_view subject,
            std::string_view metric,
            const ClusteredEstimate& estimate_value) {
            row(
                "metric", scope, subject, metric,
                format_real(estimate_value.mean),
                format_real(estimate_value.standard_error),
                format_real(
                    estimate_value.confidence_lower_95),
                format_real(
                    estimate_value.confidence_upper_95),
                std::to_string(estimate_value.records),
                std::to_string(estimate_value.clusters),
                "", "");
        };
    const auto write_weighting =
        [&row, &write_estimate](
            std::string_view scope,
            std::string_view row_set,
            std::string_view weighting,
            const RowSetMetrics& rows,
            const WeightingMetrics& metrics) {
            const std::string prefix =
                std::string(row_set) + "/" +
                std::string(weighting);
            row(
                "accounting", scope, prefix, "coverage",
                "", "", "", "",
                std::to_string(rows.records),
                std::to_string(rows.physical_games),
                rows.available ? "AVAILABLE" : "UNAVAILABLE",
                "actor_games=" +
                    std::to_string(rows.actor_games) +
                    ";excluded_actor_games=" +
                    std::to_string(
                        rows.excluded_actor_games));
            if (!rows.available) {
                return;
            }
            for (std::size_t arm = 0;
                 arm < kTargetArmCount; ++arm) {
                const auto& scored = metrics.arms[arm];
                const std::string subject =
                    prefix + "/" +
                    std::string(target_arm_name(
                        static_cast<TargetArm>(arm)));
                write_estimate(
                    scope, subject, "brier", scored.brier);
                write_estimate(
                    scope, subject, "soft_log_loss",
                    scored.soft_log_loss);
                write_estimate(
                    scope, subject, "signed_bias",
                    scored.signed_bias);
                row(
                    "diagnostic", scope, subject,
                    "target_distribution",
                    format_real(scored.target_mean), "", "",
                    format_real(scored.target_variance),
                    std::to_string(scored.brier.records),
                    std::to_string(scored.brier.clusters), "",
                    "saturated=" +
                        std::to_string(
                            scored.saturated_targets) +
                        ";fraction=" +
                        format_real(
                            scored.saturation_fraction));
            }
            for (std::size_t contrast = 0;
                 contrast < kContrastCount; ++contrast) {
                const auto& delta =
                    metrics.contrasts[contrast];
                const std::string subject =
                    prefix + "/" +
                    std::string(contrast_name(
                        static_cast<Contrast>(contrast)));
                write_estimate(
                    scope, subject, "brier_delta",
                    delta.brier_delta);
                write_estimate(
                    scope, subject, "soft_log_loss_delta",
                    delta.soft_log_loss_delta);
                write_estimate(
                    scope, subject, "signed_bias_delta",
                    delta.signed_bias_delta);
            }
            write_estimate(
                scope, prefix + "/interaction",
                "brier_delta",
                metrics.interaction.brier_delta);
            write_estimate(
                scope, prefix + "/interaction",
                "soft_log_loss_delta",
                metrics.interaction.soft_log_loss_delta);
            write_estimate(
                scope, prefix + "/interaction",
                "signed_bias_delta",
                metrics.interaction.signed_bias_delta);
        };
    const auto write_scope =
        [&row, &write_weighting, &write_estimate](
            std::string_view name,
            const ScopeMetrics& scope) {
            write_weighting(
                name, "all", "record",
                scope.all_records,
                scope.all_records.record_weighted);
            write_weighting(
                name, "all", "equal_actor_game",
                scope.all_records,
                scope.all_records.equal_actor_game);
            write_weighting(
                name, "four_common", "record",
                scope.four_arm_common,
                scope.four_arm_common.record_weighted);
            write_weighting(
                name, "four_common", "equal_actor_game",
                scope.four_arm_common,
                scope.four_arm_common.equal_actor_game);
            for (std::size_t contrast = 0;
                 contrast < kContrastCount; ++contrast) {
                const auto& rows =
                    scope.pair_common[contrast];
                const std::string pair_name =
                    "pair_common/" +
                    std::string(contrast_name(
                        static_cast<Contrast>(contrast)));
                row(
                    "accounting", name, pair_name,
                    "coverage", "", "", "", "",
                    std::to_string(rows.records),
                    std::to_string(rows.physical_games),
                    rows.available
                        ? "AVAILABLE"
                        : "UNAVAILABLE",
                    "actor_games=" +
                        std::to_string(rows.actor_games) +
                        ";excluded_actor_games=" +
                        std::to_string(
                            rows.excluded_actor_games));
                if (rows.available) {
                    const auto& record =
                        rows.record_weighted
                            .contrasts[contrast];
                    const auto& actor =
                        rows.equal_actor_game
                            .contrasts[contrast];
                    write_estimate(
                        name, pair_name + "/record",
                        "brier_delta",
                        record.brier_delta);
                    write_estimate(
                        name, pair_name + "/record",
                        "soft_log_loss_delta",
                        record.soft_log_loss_delta);
                    write_estimate(
                        name, pair_name + "/record",
                        "signed_bias_delta",
                        record.signed_bias_delta);
                    write_estimate(
                        name,
                        pair_name + "/equal_actor_game",
                        "brier_delta",
                        actor.brier_delta);
                    write_estimate(
                        name,
                        pair_name + "/equal_actor_game",
                        "soft_log_loss_delta",
                        actor.soft_log_loss_delta);
                    write_estimate(
                        name,
                        pair_name + "/equal_actor_game",
                        "signed_bias_delta",
                        actor.signed_bias_delta);
                }
            }
            constexpr std::array<
                std::string_view,
                kRootTurnStratumCount>
                stratum_names = {"<=3", "4-7", ">=8"};
            for (std::size_t stratum = 0;
                 stratum < stratum_names.size(); ++stratum) {
                const auto& rows =
                    scope.four_arm_common_by_root_turn[
                        stratum];
                const std::string stratum_scope =
                    std::string(name) + "/turn-" +
                    std::string(stratum_names[stratum]);
                write_weighting(
                    stratum_scope, "four_common", "record",
                    rows, rows.record_weighted);
                write_weighting(
                    stratum_scope, "four_common",
                    "equal_actor_game", rows,
                    rows.equal_actor_game);
            }
            std::string tails;
            for (std::size_t arm = 0;
                 arm < kTargetArmCount; ++arm) {
                if (!tails.empty()) {
                    tails += ';';
                }
                tails +=
                    std::string(target_arm_name(
                        static_cast<TargetArm>(arm))) +
                    "_bootstrap=" +
                    std::to_string(
                        scope.bootstrapped_records[arm]) +
                    ";" +
                    std::string(target_arm_name(
                        static_cast<TargetArm>(arm))) +
                    "_tail=" +
                    std::to_string(
                        scope.terminal_tail_records[arm]);
            }
            row(
                "diagnostic", name, "targets", "tails",
                "", "", "", "",
                std::to_string(scope.all_records.records),
                "", "", tails);
            const auto write_turn_distances =
                [&](std::string_view arm,
                    const auto& histogram) {
                    for (std::size_t distance = 0;
                         distance < histogram.size();
                         ++distance) {
                        row(
                            "diagnostic", name, arm,
                            "physical_turn_distance_count",
                            std::to_string(
                                histogram[distance]),
                            "", "", "", "", "", "",
                            "turns=" +
                                std::to_string(distance));
                    }
                };
            write_turn_distances(
                "RO4",
                scope
                    .record_offset4_turn_distance_histogram);
            write_turn_distances(
                "RO8",
                scope
                    .record_offset8_turn_distance_histogram);
        };

    row(
        "artifact", "global", "C16", "content_hash",
        "", "", "", "", "", "",
        report.artifact_unchanged ? "UNCHANGED" : "CHANGED",
        report.artifact_before.path + "|" +
            std::to_string(report.artifact_before.size) + "|" +
            std::to_string(
                report.artifact_before
                    .modification_time_ticks) +
            "|" + report.artifact_before.content_hash);
    row(
        "config", "global", "CT8-0", "recipe",
        "", "", "", "",
        std::to_string(report.scientific.physical_games),
        "", "FIXED",
        "seed=" + std::to_string(report.scientific.seed) +
            ";generation=" +
            std::to_string(report.scientific.generation) +
            ";blocks=" +
            std::to_string(
                report.scientific.balanced_blocks) +
            ";K=1;H=4;epsilon=0.05;terminal_weight=0.5");

    write_scope("Pooled", report.scientific.metrics.pooled);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        write_scope(
            audit_deck_name(static_cast<DeckId>(deck)),
            report.scientific.metrics.by_deck[deck]);
    }
    row(
        "qualification", "Green/turn-<=3", "RO4",
        "positive_control", "", "", "", "", "",
        "",
        report.scientific.early_green_control_qualified
            ? "PASS"
            : "FAIL",
        "lower95>0");
    row(
        "mechanical", "global", "repeat",
        "bit_identity", "", "", "", "", "", "",
        report.repeated_report_bit_identical
            ? "PASS"
            : "FAIL",
        "schedule=" + report.scientific.schedule_hash +
            ";trace=" + report.scientific.trace_hash +
            ";outcome=" +
            report.scientific.outcome_hash +
            ";RO4=" +
            report.scientific
                .record_offset4_target_hash +
            ";RO8=" +
            report.scientific
                .record_offset8_target_hash +
            ";CT4=" +
            report.scientific
                .calendar_turn4_target_hash +
            ";CT8=" +
            report.scientific
                .calendar_turn8_target_hash +
            ";scoring=" + report.scientific.scoring_hash);
    row(
        "mechanical", "global", "hidden_repartition",
        "bit_identity", "", "", "", "",
        std::to_string(
            report.scientific.hidden_repartition_states),
        "",
        report.scientific.hidden_repartition_passed
            ? "PASS"
            : "FAIL",
        "opponent-hand/library only;all four target arrays");
    row(
        "gate", "offline", "CT8", "verdict",
        "", "", "", "", "", "",
        report.passed ? "PASS" : "REJECT",
        report.gate.failures.empty()
            ? ""
            : report.gate.failures.front());
    output << "CT8_TSV_END\n";
}

} // namespace old_school::target_factorial_audit
