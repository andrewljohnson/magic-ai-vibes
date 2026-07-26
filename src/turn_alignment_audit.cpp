#include "old_school/turn_alignment_audit.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <memory>
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

namespace old_school::turn_alignment_audit {
namespace {

constexpr double kLogClamp = 1.0e-12;
constexpr double kMaterialBias = 0.05;

using ActorKey = std::pair<std::size_t, std::size_t>;

class ContentHash {
  public:
    void add_byte(std::uint8_t byte) {
        static constexpr std::array<std::uint64_t, 4> salt = {
            0x9e3779b97f4a7c15ULL,
            0xd1b54a32d192ed03ULL,
            0x94d049bb133111ebULL,
            0xbf58476d1ce4e5b9ULL,
        };
        for (std::size_t index = 0; index < state_.size();
             ++index) {
            state_[index] = std::rotl(
                state_[index] ^
                    (static_cast<std::uint64_t>(byte) +
                     salt[index]),
                static_cast<int>(11 + 8 * index));
            state_[index] =
                state_[index] *
                    (salt[(index + 1) % salt.size()] | 1ULL) +
                count_;
        }
        ++count_;
    }

    void add_u64(std::uint64_t value) {
        for (std::size_t byte = 0; byte < 8; ++byte) {
            add_byte(static_cast<std::uint8_t>(
                value >> (8 * byte)));
        }
    }

    void add_size(std::size_t value) {
        add_u64(static_cast<std::uint64_t>(value));
    }

    void add_double(double value) {
        add_u64(std::bit_cast<std::uint64_t>(value));
    }

    void add_text(std::string_view value) {
        add_size(value.size());
        for (const unsigned char byte : value) {
            add_byte(byte);
        }
    }

    std::string finish() const {
        static constexpr char hex[] = "0123456789abcdef";
        std::array<std::uint64_t, 4> digest = state_;
        for (std::size_t index = 0; index < digest.size();
             ++index) {
            digest[index] ^=
                count_ +
                0x9e3779b97f4a7c15ULL *
                    static_cast<std::uint64_t>(index + 1);
            digest[index] ^= digest[index] >> 30;
            digest[index] *= 0xbf58476d1ce4e5b9ULL;
            digest[index] ^= digest[index] >> 27;
            digest[index] *= 0x94d049bb133111ebULL;
            digest[index] ^= digest[index] >> 31;
        }
        std::string output(64, '0');
        for (std::size_t word = 0; word < digest.size();
             ++word) {
            for (std::size_t nibble = 0; nibble < 16;
                 ++nibble) {
                const auto shift =
                    static_cast<unsigned int>(60 - 4 * nibble);
                output[word * 16 + nibble] =
                    hex[(digest[word] >> shift) & 0xfULL];
            }
        }
        return output;
    }

  private:
    std::array<std::uint64_t, 4> state_ = {
        0x6a09e667f3bcc909ULL,
        0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL,
        0xa54ff53a5f1d36f1ULL,
    };
    std::uint64_t count_ = 0;
};

void hash_task(ContentHash& hash, const AuditTask& task) {
    hash.add_size(task.physical_game);
    hash.add_size(task.block);
    hash.add_size(task.scheduled.schedule_index);
    hash.add_size(task.scheduled.pairing_index);
    hash.add_size(static_cast<std::size_t>(
        task.scheduled.seat_decks[0]));
    hash.add_size(static_cast<std::size_t>(
        task.scheduled.seat_decks[1]));
    hash.add_size(task.scheduled.starting_player);
    hash.add_u64(task.scheduled.seed);
}

void hash_optional_index(
    ContentHash& hash,
    const std::optional<std::size_t>& value) {
    hash.add_u64(value.has_value() ? 1U : 0U);
    if (value.has_value()) {
        hash.add_size(*value);
    }
}

void hash_observation(
    ContentHash& hash,
    std::span<const double> observation) {
    hash.add_size(observation.size());
    for (const double value : observation) {
        hash.add_double(value);
    }
}

void hash_record(ContentHash& hash, const AuditRecord& record) {
    hash.add_size(record.physical_game);
    hash.add_size(record.block);
    hash.add_size(record.schedule_index);
    hash.add_size(record.perspective);
    hash.add_size(static_cast<std::size_t>(record.deck));
    hash.add_size(record.root_index);
    hash.add_size(record.root_turn);
    hash.add_double(record.terminal_target);
    hash.add_double(record.control_target);
    hash.add_double(record.treatment_target);
    hash_optional_index(hash, record.control_future_index);
    hash_optional_index(hash, record.treatment_future_index);
    hash_optional_index(hash, record.control_turn_distance);
    hash_optional_index(hash, record.treatment_turn_distance);
}

bool bit_identical(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

bool bit_identical(
    std::span<const double> left,
    std::span<const double> right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!bit_identical(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

struct HiddenClone {
    GameState state;
    bool changed = false;
};

HiddenClone hidden_repartition_clone(
    const GameState& source, std::size_t perspective) {
    if (perspective >= source.players.size()) {
        throw std::out_of_range(
            "TA4 hidden-clone perspective is out of range");
    }
    HiddenClone result{.state = source};
    PlayerState& hidden =
        result.state.players[1 - perspective];

    for (std::size_t hand = 0;
         hand < hidden.hand.size(); ++hand) {
        const auto different = std::find_if(
            hidden.library.begin(), hidden.library.end(),
            [&](CardId card) {
                return card != hidden.hand[hand];
            });
        if (different != hidden.library.end()) {
            std::iter_swap(
                hidden.hand.begin() +
                    static_cast<std::ptrdiff_t>(hand),
                different);
            result.changed = true;
            return result;
        }
    }
    const auto swap_different =
        [&](std::vector<CardId>& cards) {
            for (std::size_t first = 0;
                 first < cards.size(); ++first) {
                const auto different = std::find_if(
                    cards.begin() +
                        static_cast<std::ptrdiff_t>(first + 1),
                    cards.end(),
                    [&](CardId card) {
                        return card != cards[first];
                    });
                if (different != cards.end()) {
                    std::iter_swap(
                        cards.begin() +
                            static_cast<std::ptrdiff_t>(first),
                        different);
                    return true;
                }
            }
            return false;
        };
    result.changed =
        swap_different(hidden.library) ||
        swap_different(hidden.hand);
    return result;
}

std::size_t deck_index(DeckId deck) {
    const std::size_t index = static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::invalid_argument(
            "TA4 record contains an invalid deck");
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
    throw std::invalid_argument("TA4 deck is out of range");
}

std::vector<CardId> cards_for_deck(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return green_deck();
    case DeckId::Red:
        return red_deck();
    case DeckId::Blue:
        return blue_deck();
    case DeckId::White:
        return white_control_deck();
    case DeckId::RUAggro:
        return ru_aggro_deck();
    }
    throw std::invalid_argument("TA4 deck is out of range");
}

void require_probability(double value, std::string_view field) {
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
        throw std::invalid_argument(
            std::string(field) +
            " must be finite and in [0, 1]");
    }
}

double soft_log_loss(double prediction, double target) {
    prediction =
        std::clamp(prediction, kLogClamp, 1.0 - kLogClamp);
    return -target * std::log(prediction) -
           (1.0 - target) * std::log(1.0 - prediction);
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
                "TA4 actor-game metric has zero rows");
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

TargetMetrics score_target(
    const std::vector<const AuditRecord*>& rows,
    bool treatment, bool equal_actor_games) {
    const auto prediction =
        [treatment](const AuditRecord& row) {
            return treatment ? row.treatment_target
                             : row.control_target;
        };
    const auto estimate =
        [&](const auto& metric) {
            return equal_actor_games
                       ? estimate_actor_games(rows, metric)
                       : estimate_rows(rows, metric);
        };

    TargetMetrics result;
    result.brier = estimate(
        [&](const AuditRecord& row) {
            const double error =
                prediction(row) - row.terminal_target;
            return error * error;
        });
    result.soft_log_loss = estimate(
        [&](const AuditRecord& row) {
            return soft_log_loss(
                prediction(row), row.terminal_target);
        });
    result.signed_bias = estimate(
        [&](const AuditRecord& row) {
            return prediction(row) - row.terminal_target;
        });

    std::map<ActorKey, std::pair<long double, std::size_t>>
        actor_predictions;
    long double sum = 0.0L;
    long double squared_sum = 0.0L;
    std::size_t sample_count = 0;
    for (const AuditRecord* row : rows) {
        const double value = prediction(*row);
        if (equal_actor_games) {
            auto& actor = actor_predictions[
                {row->physical_game, row->perspective}];
            actor.first += value;
            ++actor.second;
        } else {
            sum += value;
            squared_sum +=
                static_cast<long double>(value) * value;
            ++sample_count;
            if (value <= 0.01 || value >= 0.99) {
                ++result.saturated_targets;
            }
        }
    }
    if (equal_actor_games) {
        for (const auto& [key, actor] : actor_predictions) {
            static_cast<void>(key);
            const double value = static_cast<double>(
                actor.first /
                static_cast<long double>(actor.second));
            sum += value;
            squared_sum +=
                static_cast<long double>(value) * value;
            ++sample_count;
            if (value <= 0.01 || value >= 0.99) {
                ++result.saturated_targets;
            }
        }
    }
    if (sample_count == 0) {
        throw std::invalid_argument(
            "TA4 target metric scope is empty");
    }
    result.target_mean = static_cast<double>(
        sum / static_cast<long double>(sample_count));
    result.target_variance = std::max(
        0.0,
        static_cast<double>(
            squared_sum /
                static_cast<long double>(sample_count) -
            static_cast<long double>(result.target_mean) *
                result.target_mean));
    result.saturation_fraction =
        static_cast<double>(result.saturated_targets) /
        static_cast<double>(sample_count);
    return result;
}

PairedMetrics score_pair(
    const std::vector<const AuditRecord*>& rows,
    bool equal_actor_games) {
    const auto estimate =
        [&](const auto& metric) {
            return equal_actor_games
                       ? estimate_actor_games(rows, metric)
                       : estimate_rows(rows, metric);
        };
    PairedMetrics result;
    result.brier_delta = estimate(
        [](const AuditRecord& row) {
            const double control_error =
                row.control_target - row.terminal_target;
            const double treatment_error =
                row.treatment_target - row.terminal_target;
            return treatment_error * treatment_error -
                   control_error * control_error;
        });
    result.soft_log_loss_delta = estimate(
        [](const AuditRecord& row) {
            return soft_log_loss(
                       row.treatment_target,
                       row.terminal_target) -
                   soft_log_loss(
                       row.control_target,
                       row.terminal_target);
        });
    result.signed_bias_delta = estimate(
        [](const AuditRecord& row) {
            return row.treatment_target -
                   row.control_target;
        });
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
    std::set<std::size_t> games;
    for (const AuditRecord* row : rows) {
        actors.emplace(row->physical_game, row->perspective);
        games.insert(row->physical_game);
    }
    result.actor_games = actors.size();
    result.excluded_actor_games =
        actor_universe.size() - actors.size();
    result.physical_games = games.size();
    result.record_weighted = {
        .control = score_target(rows, false, false),
        .treatment = score_target(rows, true, false),
        .treatment_minus_control =
            score_pair(rows, false),
    };
    result.equal_actor_game = {
        .control = score_target(rows, false, true),
        .treatment = score_target(rows, true, true),
        .treatment_minus_control =
            score_pair(rows, true),
    };
    return result;
}

ScopeMetrics score_scope(
    const std::vector<const AuditRecord*>& rows) {
    if (rows.empty()) {
        throw std::invalid_argument(
            "TA4 metric scope contains no records");
    }
    std::set<ActorKey> actors;
    std::vector<const AuditRecord*> common;
    std::array<std::vector<const AuditRecord*>,
               kRootTurnStratumCount>
        strata;
    ScopeMetrics result;
    for (const AuditRecord* row : rows) {
        actors.emplace(row->physical_game, row->perspective);
        const bool control =
            row->control_future_index.has_value();
        const bool treatment =
            row->treatment_future_index.has_value();
        if (treatment && !control) {
            throw std::invalid_argument(
                "TA4 treatment future exists without control "
                "future");
        }
        if (control) {
            ++result.control_bootstrapped_records;
            if (!row->control_turn_distance.has_value() ||
                *row->control_turn_distance >
                    kRecordBootstrapDistance) {
                throw std::invalid_argument(
                    "TA4 control turn distance is invalid");
            }
            ++result.control_turn_distance_histogram[
                *row->control_turn_distance];
        } else {
            ++result.control_terminal_tail_records;
        }
        if (treatment) {
            ++result.treatment_bootstrapped_records;
            if (row->treatment_turn_distance !=
                std::optional<std::size_t>(
                    kTurnBootstrapAdvances)) {
                throw std::invalid_argument(
                    "TA4 treatment turn distance is invalid");
            }
            common.push_back(row);
            strata[stratum_index(
                root_stratum(row->root_turn))]
                .push_back(row);
        } else {
            ++result.treatment_terminal_tail_records;
        }
    }
    result.all_records = score_row_set(rows, actors);
    result.common_records = score_row_set(common, actors);
    for (std::size_t index = 0;
         index < strata.size(); ++index) {
        result.common_by_root_turn[index] =
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

std::string format_real(double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(
                  std::numeric_limits<double>::max_digits10)
           << value;
    return output.str();
}

} // namespace

std::vector<AuditTask> audit_schedule(
    std::uint64_t seed, std::size_t generation,
    std::size_t balanced_blocks) {
    if (balanced_blocks == 0) {
        throw std::invalid_argument(
            "TA4 audit requires at least one balanced block");
    }
    std::vector<AuditTask> result;
    result.reserve(
        balanced_blocks *
        learned_iteration::kBalancedScheduleGames);
    for (std::size_t block = 0;
         block < balanced_blocks; ++block) {
        const auto schedule =
            learned_iteration::balanced_schedule(
                seed, generation, block);
        for (const auto& game : schedule) {
            result.push_back({
                .physical_game = result.size(),
                .block = block,
                .scheduled = game,
            });
        }
    }
    return result;
}

MetricsReport score_records(
    std::span<const AuditRecord> records) {
    if (records.empty()) {
        throw std::invalid_argument(
            "TA4 scoring requires records");
    }
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
                "TA4 perspective is out of range");
        }
        require_probability(
            record.terminal_target, "TA4 terminal target");
        require_probability(
            record.control_target, "TA4 control target");
        require_probability(
            record.treatment_target, "TA4 treatment target");
        const bool control_future =
            record.control_future_index.has_value();
        const bool treatment_future =
            record.treatment_future_index.has_value();
        if (control_future !=
            record.control_turn_distance.has_value() ||
            treatment_future !=
                record.treatment_turn_distance.has_value()) {
            throw std::invalid_argument(
                "TA4 future-index and turn-distance presence "
                "must match");
        }
        if (control_future &&
            (record.root_index >
                 std::numeric_limits<std::size_t>::max() -
                     kRecordBootstrapDistance ||
             *record.control_future_index !=
                 record.root_index +
                     kRecordBootstrapDistance ||
             *record.control_future_index <=
                 record.root_index)) {
            throw std::invalid_argument(
                "TA4 control future is not record offset four");
        }
        if (treatment_future &&
            *record.treatment_future_index <=
                record.root_index) {
            throw std::invalid_argument(
                "TA4 treatment future is not after its root");
        }
        if (!control_future &&
            !bit_identical(
                record.control_target,
                record.terminal_target)) {
            throw std::invalid_argument(
                "TA4 control tail is not bit-identical to z");
        }
        if (!treatment_future &&
            !bit_identical(
                record.treatment_target,
                record.terminal_target)) {
            throw std::invalid_argument(
                "TA4 treatment tail is not bit-identical to z");
        }
        if (!identities
                 .emplace(
                     record.physical_game,
                     record.perspective,
                     record.root_index)
                 .second) {
            throw std::invalid_argument(
                "TA4 record identity is duplicated");
        }
        pooled.push_back(&record);
        by_deck[deck_index(record.deck)].push_back(&record);
    }
    MetricsReport report;
    report.pooled = score_scope(pooled);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        report.by_deck[deck] =
            score_scope(by_deck[deck]);
    }
    return report;
}

GateReport evaluate_gate(const ScientificReport& report) {
    GateReport gate;
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
    const auto& green_record =
        green.common_records.record_weighted;
    const auto& green_actor =
        green.common_records.equal_actor_game;

    gate.green_bias_moved =
        green_record.treatment_minus_control
                .signed_bias_delta.mean <=
            -0.010;
    gate.green_bias_precise =
        green_record.treatment_minus_control
                .signed_bias_delta.confidence_upper_95 <
            0.0;
    gate.green_equal_actor_direction =
        green_actor.treatment_minus_control
                .signed_bias_delta.mean <
            0.0;
    gate.green_absolute_bias_shrank =
        std::abs(
            green_record.treatment.signed_bias.mean) <
            std::abs(
                green_record.control.signed_bias.mean) &&
        std::abs(
            green_actor.treatment.signed_bias.mean) <
            std::abs(
                green_actor.control.signed_bias.mean);

    gate.pooled_common_brier_noninferior =
        pooled.common_records.record_weighted
            .treatment_minus_control.brier_delta
            .confidence_upper_95 < 0.001;
    gate.pooled_all_brier_nonpositive =
        pooled.all_records.record_weighted
            .treatment_minus_control.brier_delta.mean <= 0.0;

    const auto direction_and_shrink =
        [](const ScopeMetrics& scope, bool upward) {
            const auto& record =
                scope.common_records.record_weighted;
            const auto& actor =
                scope.common_records.equal_actor_game;
            const double record_delta =
                record.treatment_minus_control
                    .signed_bias_delta.mean;
            const double actor_delta =
                actor.treatment_minus_control
                    .signed_bias_delta.mean;
            const bool directions =
                upward
                    ? record_delta > 0.0 &&
                          actor_delta > 0.0
                    : record_delta < 0.0 &&
                          actor_delta < 0.0;
            return directions &&
                   std::abs(
                       record.treatment.signed_bias.mean) <
                       std::abs(
                           record.control.signed_bias.mean) &&
                   std::abs(
                       actor.treatment.signed_bias.mean) <
                       std::abs(
                           actor.control.signed_bias.mean);
        };
    gate.blue_direction_and_shrink =
        direction_and_shrink(blue, true);
    gate.ru_direction_and_shrink =
        direction_and_shrink(ru, false);
    gate.additional_deck_moved =
        gate.blue_direction_and_shrink ||
        gate.ru_direction_and_shrink;

    gate.per_deck_all_brier_guard = true;
    for (const ScopeMetrics& deck : report.metrics.by_deck) {
        gate.per_deck_all_brier_guard =
            gate.per_deck_all_brier_guard &&
            deck.all_records.record_weighted
                    .treatment_minus_control.brier_delta.mean <=
                0.005;
    }

    gate.red_white_no_new_material_bias = true;
    for (const DeckId deck : {DeckId::Red, DeckId::White}) {
        const auto& metrics =
            report.metrics.by_deck[deck_index(deck)]
                .all_records.record_weighted;
        const bool treatment_material =
            has_material_bias(metrics.treatment);
        const bool inherited =
            has_material_bias(metrics.control) &&
            same_sign(
                metrics.treatment.signed_bias.mean,
                metrics.control.signed_bias.mean);
        if (treatment_material && !inherited) {
            gate.red_white_no_new_material_bias = false;
        }
    }

    gate.evidence_complete =
        pooled.common_records.available &&
        pooled.common_records.actor_games >= 3800 &&
        pooled.common_records.physical_games >= 1900;
    for (const ScopeMetrics& deck : report.metrics.by_deck) {
        gate.evidence_complete =
            gate.evidence_complete &&
            deck.common_records.actor_games >= 760;
    }
    gate.mechanical_invariants =
        report.schedule_balanced &&
        report.trace_invariants_passed &&
        report.control_identity_passed &&
        report.tail_identity_passed &&
        report.treatment_distance_passed &&
        report.treatment_earliest_passed &&
        report.hidden_repartition_passed;
    gate.passed =
        gate.green_bias_moved &&
        gate.green_bias_precise &&
        gate.green_equal_actor_direction &&
        gate.green_absolute_bias_shrank &&
        gate.pooled_common_brier_noninferior &&
        gate.pooled_all_brier_nonpositive &&
        gate.additional_deck_moved &&
        gate.per_deck_all_brier_guard &&
        gate.red_white_no_new_material_bias &&
        gate.evidence_complete &&
        gate.mechanical_invariants;

    const auto failure =
        [&](bool condition, std::string message) {
            if (!condition) {
                gate.failures.push_back(std::move(message));
            }
        };
    failure(gate.green_bias_moved,
            "Green common-row bias delta was not <= -0.010");
    failure(gate.green_bias_precise,
            "Green common-row bias upper95 was not below zero");
    failure(gate.green_equal_actor_direction,
            "Green equal-actor-game bias did not move downward");
    failure(gate.green_absolute_bias_shrank,
            "Green absolute bias did not shrink under both weights");
    failure(gate.pooled_common_brier_noninferior,
            "pooled common-row Brier upper95 was not below +0.001");
    failure(gate.pooled_all_brier_nonpositive,
            "pooled all-row Brier point delta was positive");
    failure(gate.additional_deck_moved,
            "neither Blue nor RU corroborated under both weights");
    failure(gate.per_deck_all_brier_guard,
            "a deck all-row Brier delta exceeded +0.005");
    failure(gate.red_white_no_new_material_bias,
            "TA4 created a new material Red or White bias");
    failure(gate.evidence_complete,
            "TA4 common-row evidence coverage was incomplete");
    failure(gate.mechanical_invariants,
            "TA4 mechanical invariants did not all pass");
    return gate;
}

namespace {

struct TaskCapture {
    std::vector<AuditRecord> records;
    bool trace_invariants_passed = true;
    bool control_identity_passed = true;
    bool tail_identity_passed = true;
    bool treatment_distance_passed = true;
    bool treatment_earliest_passed = true;
    bool hidden_repartition_passed = true;
    std::size_t hidden_repartition_states = 0;
    std::string trace_hash;
    std::string outcome_hash;
    std::string control_target_hash;
    std::string treatment_target_hash;
    std::string scoring_hash;
};

void validate_task(const AuditTask& task) {
    if (task.scheduled.schedule_index >=
            learned_iteration::kBalancedScheduleGames ||
        task.scheduled.pairing_index >=
            learned_iteration::kBalancedPairings ||
        task.scheduled.starting_player >= 2) {
        throw std::invalid_argument(
            "TA4 task has invalid schedule coordinates");
    }
    deck_index(task.scheduled.seat_decks[0]);
    deck_index(task.scheduled.seat_decks[1]);
    if (task.scheduled.seat_decks[0] ==
        task.scheduled.seat_decks[1]) {
        throw std::invalid_argument(
            "TA4 task must pair distinct decks");
    }
}

void require_trace_turns(
    std::span<const std::size_t> turns) {
    if (turns.empty()) {
        throw std::runtime_error(
            "TA4 game produced an empty trace");
    }
    if (turns.front() == 0) {
        throw std::runtime_error(
            "TA4 trace starts before turn one");
    }
    for (std::size_t index = 1; index < turns.size();
         ++index) {
        if (turns[index] < turns[index - 1]) {
            throw std::runtime_error(
                "TA4 trace turn number regressed");
        }
        if (turns[index] - turns[index - 1] > 1) {
            throw std::runtime_error(
                "TA4 trace skipped a calendar turn");
        }
    }
}

void require_hidden_clone_identity(
    const GameState& state,
    std::size_t perspective,
    std::shared_ptr<const LearnedModel> parent,
    double expected_value,
    std::vector<double>& clone_values,
    std::size_t index,
    std::size_t& changed_states) {
    const HiddenClone clone =
        hidden_repartition_clone(state, perspective);
    if (clone.changed) {
        ++changed_states;
    }
    if (observe_game_state(state, perspective) !=
        observe_game_state(clone.state, perspective)) {
        throw std::runtime_error(
            "TA4 hidden repartition changed the public "
            "information set");
    }
    const std::vector<double> original_features =
        learned_observation(state, perspective);
    const std::vector<double> clone_features =
        learned_observation(clone.state, perspective);
    if (!bit_identical(original_features, clone_features)) {
        throw std::runtime_error(
            "TA4 hidden repartition changed encoded critic "
            "features");
    }
    const double clone_value =
        learned_critic_value(
            clone.state, perspective, parent);
    if (!bit_identical(expected_value, clone_value)) {
        throw std::runtime_error(
            "TA4 hidden repartition changed the frozen critic "
            "value");
    }
    clone_values[index] = clone_value;
}

TaskCapture run_task(
    const AuditTask& task,
    std::shared_ptr<const LearnedModel> parent,
    const CaptureConfig& capture_config) {
    validate_task(task);

    GameConfig game_config;
    game_config.max_turns = capture_config.max_game_turns;
    game_config.starting_player =
        task.scheduled.starting_player;
    game_config.learned_model = parent;
    game_config.learned_search_depth = 1;
    const BotConfig pilot = {
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 1,
        .exploration_rate = 0.05,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .training_games = 800,
        .learned_model = parent,
    };
    game_config.bots = {pilot, pilot};
    Game game(
        cards_for_deck(task.scheduled.seat_decks[0]),
        cards_for_deck(task.scheduled.seat_decks[1]),
        task.scheduled.seed, game_config);
    std::vector<GameState> trace;
    const GameResult result = game.run_with_trace(trace);
    if (result.starting_player !=
        task.scheduled.starting_player) {
        throw std::runtime_error(
            "TA4 starting-player schedule mismatch");
    }

    std::vector<std::size_t> turns;
    turns.reserve(trace.size());
    for (const GameState& state : trace) {
        turns.push_back(state.turn_number);
    }
    require_trace_turns(turns);
    const auto treatment_indices =
        learned_iteration::turn_aligned_bootstrap_indices(
            turns, kTurnBootstrapAdvances);
    if (treatment_indices.size() != trace.size()) {
        throw std::logic_error(
            "TA4 turn-index helper returned the wrong size");
    }

    std::array<double, 2> terminal_targets{};
    std::array<std::vector<double>, 2> parent_values;
    std::array<std::vector<double>, 2> clone_values;
    std::array<std::vector<double>, 2> control_targets;
    std::array<std::vector<double>, 2> treatment_targets;
    std::array<std::vector<double>, 2>
        clone_control_targets;
    std::array<std::vector<double>, 2>
        clone_treatment_targets;
    std::size_t hidden_repartition_states = 0;
    for (std::size_t perspective = 0;
         perspective < 2; ++perspective) {
        terminal_targets[perspective] =
            learned_discounted_terminal_target(
                result, perspective);
        require_probability(
            terminal_targets[perspective],
            "TA4 discounted terminal target");
        parent_values[perspective].reserve(trace.size());
        clone_values[perspective].resize(trace.size());
        for (const GameState& state : trace) {
            const double value =
                learned_critic_value(
                    state, perspective, parent);
            require_probability(value, "TA4 parent value");
            parent_values[perspective].push_back(value);
        }
        if (capture_config.verify_hidden_repartition) {
            for (std::size_t index = 0;
                 index < trace.size(); ++index) {
                require_hidden_clone_identity(
                    trace[index], perspective, parent,
                    parent_values[perspective][index],
                    clone_values[perspective], index,
                    hidden_repartition_states);
            }
        } else {
            clone_values[perspective] =
                parent_values[perspective];
        }
        control_targets[perspective] =
            learned_iteration::n_state_bootstrap_targets(
                parent_values[perspective],
                terminal_targets[perspective],
                kRecordBootstrapDistance);
        treatment_targets[perspective] =
            learned_iteration::
                weighted_turn_aligned_bootstrap_targets(
                    parent_values[perspective], turns,
                    terminal_targets[perspective],
                    kTurnBootstrapAdvances, kTerminalWeight);
    }

    TaskCapture output;
    if (capture_config.verify_hidden_repartition) {
        output.hidden_repartition_states =
            hidden_repartition_states;
        for (std::size_t perspective = 0;
             perspective < 2; ++perspective) {
            clone_control_targets[perspective] =
                learned_iteration::n_state_bootstrap_targets(
                    clone_values[perspective],
                    terminal_targets[perspective],
                    kRecordBootstrapDistance);
            clone_treatment_targets[perspective] =
                learned_iteration::
                    weighted_turn_aligned_bootstrap_targets(
                        clone_values[perspective], turns,
                        terminal_targets[perspective],
                        kTurnBootstrapAdvances,
                        kTerminalWeight);
            if (!bit_identical(
                    control_targets[perspective],
                    clone_control_targets[perspective]) ||
                !bit_identical(
                    treatment_targets[perspective],
                    clone_treatment_targets[perspective])) {
                throw std::runtime_error(
                    "TA4 hidden repartition changed target arrays");
            }
        }
    } else {
        clone_control_targets = control_targets;
        clone_treatment_targets = treatment_targets;
    }

    ContentHash trace_hash;
    trace_hash.add_text("ta4-trace-v1");
    hash_task(trace_hash, task);
    trace_hash.add_size(trace.size());
    for (std::size_t index = 0; index < trace.size();
         ++index) {
        trace_hash.add_size(index);
        trace_hash.add_size(trace[index].turn_number);
        for (std::size_t perspective = 0;
             perspective < 2; ++perspective) {
            hash_observation(
                trace_hash,
                learned_observation(
                    trace[index], perspective));
        }
    }
    output.trace_hash = trace_hash.finish();

    ContentHash outcome_hash;
    outcome_hash.add_text("ta4-outcome-v1");
    hash_task(outcome_hash, task);
    outcome_hash.add_u64(
        static_cast<std::uint64_t>(result.winner + 1));
    outcome_hash.add_size(
        static_cast<std::size_t>(result.reason));
    outcome_hash.add_size(result.turns);
    outcome_hash.add_size(result.starting_player);
    outcome_hash.add_u64(static_cast<std::uint64_t>(
        static_cast<std::int64_t>(result.ending_life[0])));
    outcome_hash.add_u64(static_cast<std::uint64_t>(
        static_cast<std::int64_t>(result.ending_life[1])));
    outcome_hash.add_double(terminal_targets[0]);
    outcome_hash.add_double(terminal_targets[1]);
    output.outcome_hash = outcome_hash.finish();

    ContentHash control_hash;
    ContentHash treatment_hash;
    ContentHash scoring_hash;
    ContentHash hidden_scoring_hash;
    control_hash.add_text("ta4-control-v1");
    treatment_hash.add_text("ta4-treatment-v1");
    scoring_hash.add_text("ta4-scoring-v1");
    hidden_scoring_hash.add_text("ta4-scoring-v1");
    output.records.reserve(2 * trace.size());
    for (std::size_t root = 0; root < trace.size();
         ++root) {
        std::optional<std::size_t> control_future;
        std::optional<std::size_t> control_distance;
        if (root + kRecordBootstrapDistance <
            trace.size()) {
            control_future =
                root + kRecordBootstrapDistance;
            if (turns[*control_future] < turns[root]) {
                throw std::runtime_error(
                    "TA4 control future regressed");
            }
            control_distance =
                turns[*control_future] - turns[root];
            if (*control_distance >
                kRecordBootstrapDistance) {
                throw std::runtime_error(
                    "TA4 control future advanced too many turns");
            }
        }
        const auto treatment_future =
            treatment_indices[root];
        std::optional<std::size_t> treatment_distance;
        if (treatment_future.has_value()) {
            if (!control_future.has_value() ||
                *treatment_future <= root ||
                turns[*treatment_future] < turns[root]) {
                throw std::runtime_error(
                    "TA4 treatment future is structurally "
                    "invalid");
            }
            treatment_distance =
                turns[*treatment_future] - turns[root];
            if (*treatment_distance !=
                kTurnBootstrapAdvances) {
                throw std::runtime_error(
                    "TA4 treatment future is not four turns "
                    "ahead");
            }
            for (std::size_t between = root + 1;
                 between < *treatment_future; ++between) {
                if (turns[between] >=
                    turns[*treatment_future]) {
                    throw std::runtime_error(
                        "TA4 treatment future is not the earliest "
                        "aligned record");
                }
            }
        }

        for (std::size_t perspective = 0;
             perspective < 2; ++perspective) {
            const double terminal =
                terminal_targets[perspective];
            const double control =
                control_targets[perspective][root];
            const double treatment =
                treatment_targets[perspective][root];
            const double direct_control =
                control_future.has_value()
                    ? kTerminalWeight * terminal +
                          (1.0 - kTerminalWeight) *
                              parent_values[perspective]
                                           [*control_future]
                    : terminal;
            const double direct_treatment =
                treatment_future.has_value()
                    ? kTerminalWeight * terminal +
                          (1.0 - kTerminalWeight) *
                              parent_values[perspective]
                                           [*treatment_future]
                    : terminal;
            if (!bit_identical(control, direct_control)) {
                throw std::runtime_error(
                    "TA4 control target differs from canonical "
                    "record-offset helper");
            }
            if (!bit_identical(treatment, direct_treatment)) {
                throw std::runtime_error(
                    "TA4 treatment target differs from declared "
                    "calendar-turn formula");
            }
            if ((!control_future.has_value() &&
                 !bit_identical(control, terminal)) ||
                (!treatment_future.has_value() &&
                 !bit_identical(treatment, terminal))) {
                throw std::runtime_error(
                    "TA4 terminal tail differs from z");
            }

            AuditRecord record{
                .physical_game = task.physical_game,
                .block = task.block,
                .schedule_index =
                    task.scheduled.schedule_index,
                .perspective = perspective,
                .deck =
                    task.scheduled.seat_decks[perspective],
                .root_index = root,
                .root_turn = turns[root],
                .terminal_target = terminal,
                .control_target = control,
                .treatment_target = treatment,
                .control_future_index = control_future,
                .treatment_future_index = treatment_future,
                .control_turn_distance = control_distance,
                .treatment_turn_distance =
                    treatment_distance,
            };
            require_probability(
                record.control_target,
                "TA4 control target");
            require_probability(
                record.treatment_target,
                "TA4 treatment target");

            hash_task(control_hash, task);
            control_hash.add_size(perspective);
            control_hash.add_size(root);
            hash_optional_index(
                control_hash, control_future);
            control_hash.add_double(control);
            hash_task(treatment_hash, task);
            treatment_hash.add_size(perspective);
            treatment_hash.add_size(root);
            hash_optional_index(
                treatment_hash, treatment_future);
            treatment_hash.add_double(treatment);
            hash_record(scoring_hash, record);
            AuditRecord hidden_record = record;
            hidden_record.control_target =
                clone_control_targets[perspective][root];
            hidden_record.treatment_target =
                clone_treatment_targets[perspective][root];
            hash_record(hidden_scoring_hash, hidden_record);
            output.records.push_back(std::move(record));
        }
    }
    output.control_target_hash = control_hash.finish();
    output.treatment_target_hash =
        treatment_hash.finish();
    output.scoring_hash = scoring_hash.finish();
    if (capture_config.verify_hidden_repartition &&
        output.scoring_hash != hidden_scoring_hash.finish()) {
        throw std::runtime_error(
            "TA4 hidden repartition changed the scoring hash");
    }
    return output;
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

} // namespace

Capture collect(
    std::span<const AuditTask> tasks,
    std::shared_ptr<const LearnedModel> parent,
    CaptureConfig config) {
    if (tasks.empty()) {
        throw std::invalid_argument(
            "TA4 collection requires tasks");
    }
    if (!parent) {
        throw std::invalid_argument(
            "TA4 collection requires a frozen parent");
    }
    if (config.max_game_turns == 0 ||
        config.worker_count == 0) {
        throw std::invalid_argument(
            "TA4 collection bounds must be positive");
    }
    std::set<std::size_t> physical_games;
    for (const AuditTask& task : tasks) {
        validate_task(task);
        if (!physical_games.insert(task.physical_game).second) {
            throw std::invalid_argument(
                "TA4 physical-game IDs must be unique");
        }
    }

    std::vector<TaskCapture> slots(tasks.size());
    std::vector<std::exception_ptr> errors(tasks.size());
    std::atomic_size_t next_task = 0;
    const std::size_t worker_count =
        std::min(config.worker_count, tasks.size());
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count;
         ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t index =
                    next_task.fetch_add(
                        1, std::memory_order_relaxed);
                if (index >= tasks.size()) {
                    return;
                }
                try {
                    slots[index] =
                        run_task(
                            tasks[index], parent, config);
                } catch (...) {
                    errors[index] =
                        std::current_exception();
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    for (const auto& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }

    Capture output;
    output.physical_games = tasks.size();
    output.perspectives = 2 * tasks.size();
    output.trace_invariants_passed = true;
    output.control_identity_passed = true;
    output.tail_identity_passed = true;
    output.treatment_distance_passed = true;
    output.treatment_earliest_passed = true;
    output.hidden_repartition_passed = true;
    ContentHash schedule_hash;
    ContentHash trace_hash;
    ContentHash outcome_hash;
    ContentHash control_hash;
    ContentHash treatment_hash;
    ContentHash scoring_hash;
    schedule_hash.add_text("ta4-schedule-v1");
    trace_hash.add_text("ta4-traces-v1");
    outcome_hash.add_text("ta4-outcomes-v1");
    control_hash.add_text("ta4-controls-v1");
    treatment_hash.add_text("ta4-treatments-v1");
    scoring_hash.add_text("ta4-scores-v1");

    std::size_t total_records = 0;
    for (const TaskCapture& slot : slots) {
        total_records += slot.records.size();
    }
    output.records.reserve(total_records);
    for (std::size_t index = 0; index < tasks.size();
         ++index) {
        const AuditTask& task = tasks[index];
        const TaskCapture& slot = slots[index];
        hash_task(schedule_hash, task);
        trace_hash.add_text(slot.trace_hash);
        outcome_hash.add_text(slot.outcome_hash);
        control_hash.add_text(slot.control_target_hash);
        treatment_hash.add_text(
            slot.treatment_target_hash);
        scoring_hash.add_text(slot.scoring_hash);
        output.trace_invariants_passed =
            output.trace_invariants_passed &&
            slot.trace_invariants_passed;
        output.control_identity_passed =
            output.control_identity_passed &&
            slot.control_identity_passed;
        output.tail_identity_passed =
            output.tail_identity_passed &&
            slot.tail_identity_passed;
        output.treatment_distance_passed =
            output.treatment_distance_passed &&
            slot.treatment_distance_passed;
        output.treatment_earliest_passed =
            output.treatment_earliest_passed &&
            slot.treatment_earliest_passed;
        output.hidden_repartition_passed =
            output.hidden_repartition_passed &&
            slot.hidden_repartition_passed;
        output.hidden_repartition_states +=
            slot.hidden_repartition_states;
        for (std::size_t perspective = 0;
             perspective < 2; ++perspective) {
            const std::size_t deck =
                deck_index(
                    task.scheduled
                        .seat_decks[perspective]);
            const std::size_t started =
                task.scheduled.starting_player ==
                        perspective
                    ? 1
                    : 0;
            ++output.deck_seat_started_counts
                  [deck][perspective][started];
        }
        output.records.insert(
            output.records.end(),
            slot.records.begin(), slot.records.end());
    }
    output.schedule_hash = schedule_hash.finish();
    output.trace_hash = trace_hash.finish();
    output.outcome_hash = outcome_hash.finish();
    output.control_target_hash = control_hash.finish();
    output.treatment_target_hash =
        treatment_hash.finish();
    output.scoring_hash = scoring_hash.finish();
    return output;
}

ScientificReport make_scientific_report(
    const Capture& capture, std::uint64_t seed,
    std::size_t generation,
    std::size_t balanced_blocks) {
    if (balanced_blocks == 0 ||
        capture.physical_games == 0 ||
        capture.perspectives !=
            2 * capture.physical_games ||
        capture.records.empty()) {
        throw std::invalid_argument(
            "TA4 capture accounting is incomplete");
    }
    for (const std::string* hash : {
             &capture.schedule_hash,
             &capture.trace_hash,
             &capture.outcome_hash,
             &capture.control_target_hash,
             &capture.treatment_target_hash,
             &capture.scoring_hash}) {
        if (!is_hex_digest(*hash)) {
            throw std::invalid_argument(
                "TA4 capture contains a malformed digest");
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
    report.metrics = score_records(capture.records);
    report.schedule_balanced =
        capture.physical_games ==
            balanced_blocks *
                learned_iteration::kBalancedScheduleGames &&
        capture.perspectives ==
            2 * balanced_blocks *
                learned_iteration::kBalancedScheduleGames;
    const std::size_t expected_cell =
        4 * balanced_blocks;
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
    report.common_coverage_passed =
        report.metrics.pooled.common_records.actor_games >=
            3800 &&
        report.metrics.pooled.common_records.physical_games >=
            1900;
    for (const ScopeMetrics& deck :
         report.metrics.by_deck) {
        report.common_coverage_passed =
            report.common_coverage_passed &&
            deck.common_records.actor_games >= 760;
    }
    report.trace_invariants_passed =
        capture.trace_invariants_passed;
    report.control_identity_passed =
        capture.control_identity_passed;
    report.tail_identity_passed =
        capture.tail_identity_passed;
    report.treatment_distance_passed =
        capture.treatment_distance_passed;
    report.treatment_earliest_passed =
        capture.treatment_earliest_passed;
    report.hidden_repartition_passed =
        capture.hidden_repartition_passed;
    report.hidden_repartition_states =
        capture.hidden_repartition_states;
    report.schedule_hash = capture.schedule_hash;
    report.trace_hash = capture.trace_hash;
    report.outcome_hash = capture.outcome_hash;
    report.control_target_hash =
        capture.control_target_hash;
    report.treatment_target_hash =
        capture.treatment_target_hash;
    report.scoring_hash = capture.scoring_hash;
    return report;
}

AuditReport run_canonical_ta4_audit(
    std::ostream& progress) {
    constexpr std::size_t training_games = 800;
    constexpr std::size_t generations = 16;
    const std::string artifact_path =
        learned_value_challenger_cache_path(
            training_games,
            kDefaultLearnedTrainingSeed,
            generations);

    AuditReport report;
    report.artifact_before =
        terminal_weight_eval::snapshot_artifact(
            artifact_path);
    bool snapshot_verified = false;
    const auto verify_snapshot = [&] {
        report.artifact_after =
            terminal_weight_eval::snapshot_artifact(
                artifact_path);
        report.artifact_unchanged =
            report.artifact_before ==
            report.artifact_after;
        if (!report.artifact_unchanged) {
            throw std::runtime_error(
                "TA4 frozen C16 artifact content, size, or "
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
                kDefaultLearnedTrainingSeed,
                generations);
        const auto parent = artifact.model();
        report.parent_fingerprint =
            learned_model_fingerprint(parent);
        if (report.parent_fingerprint !=
            kParentFingerprint) {
            throw std::runtime_error(
                "TA4 frozen C16 fingerprint mismatch: "
                "expected " +
                std::string(kParentFingerprint) +
                ", got " + report.parent_fingerprint);
        }
        report.parent_components =
            learned_model_component_fingerprints(parent);
        if (learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
            throw std::runtime_error(
                "TA4 parent uses the wrong critic schema");
        }
        const auto after_load =
            terminal_weight_eval::snapshot_artifact(
                artifact_path);
        if (after_load != report.artifact_before) {
            throw std::runtime_error(
                "TA4 frozen C16 artifact changed while loading");
        }
        progress << " done\n";

        const auto tasks =
            audit_schedule(
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
            << "Constructing TA4-0 audit corpus twice "
               "(2,000 games each, K=1/H=4)..."
            << std::flush;
        const Capture first =
            collect(tasks, parent, config);
        const auto after_first =
            terminal_weight_eval::snapshot_artifact(
                artifact_path);
        if (after_first != report.artifact_before) {
            throw std::runtime_error(
                "TA4 frozen C16 artifact changed after first "
                "collection");
        }
        const Capture second =
            collect(tasks, parent, config);
        report.scientific =
            make_scientific_report(
                first, kAuditSeed, kAuditGeneration,
                kAuditBalancedBlocks);
        const ScientificReport second_report =
            make_scientific_report(
                second, kAuditSeed, kAuditGeneration,
                kAuditBalancedBlocks);
        report.repeated_report_bit_identical =
            first == second &&
            report.scientific == second_report;
        progress << " done ("
                 << first.records.size()
                 << " trace-perspective records per capture)\n";

        verify_snapshot();
        const bool infrastructure_complete =
            report.repeated_report_bit_identical &&
            report.scientific.schedule_balanced &&
            report.scientific.common_coverage_passed &&
            report.scientific.trace_invariants_passed &&
            report.scientific.control_identity_passed &&
            report.scientific.tail_identity_passed &&
            report.scientific
                .treatment_distance_passed &&
            report.scientific
                .treatment_earliest_passed &&
            report.scientific
                .hidden_repartition_passed &&
            report.scientific
                .hidden_repartition_states > 0;
        if (!infrastructure_complete) {
            throw std::runtime_error(
                "TA4 audit evidence, mechanical, hidden-zone, "
                "or repeatability checks were incomplete");
        }
        report.gate = evaluate_gate(report.scientific);
        if (!report.gate.evidence_complete ||
            !report.gate.mechanical_invariants) {
            throw std::runtime_error(
                "TA4 gate received incomplete evidence or failed "
                "mechanical invariants");
        }
        report.passed =
            report.gate.passed &&
            report.repeated_report_bit_identical &&
            report.artifact_unchanged;
        return report;
    } catch (...) {
        if (!snapshot_verified) {
            verify_snapshot();
        }
        throw;
    }
}

void write_human_report(
    const AuditReport& report,
    std::ostream& output) {
    output
        << "\nTA4-0 calendar-turn target audit\n"
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
        << "\n          control="
        << report.scientific.control_target_hash
        << "\n          treatment="
        << report.scientific.treatment_target_hash
        << "\n          scoring="
        << report.scientific.scoring_hash << '\n';

    const auto write_weight =
        [&output](
            std::string_view label,
            const WeightingMetrics& metrics) {
            output
                << "    " << label
                << ": bias "
                << format_real(
                       metrics.control.signed_bias.mean)
                << " -> "
                << format_real(
                       metrics.treatment.signed_bias.mean)
                << " (delta "
                << format_real(
                       metrics.treatment_minus_control
                           .signed_bias_delta.mean)
                << ", 95% ["
                << format_real(
                       metrics.treatment_minus_control
                           .signed_bias_delta
                           .confidence_lower_95)
                << ", "
                << format_real(
                       metrics.treatment_minus_control
                           .signed_bias_delta
                           .confidence_upper_95)
                << "]); Brier "
                << format_real(metrics.control.brier.mean)
                << " -> "
                << format_real(metrics.treatment.brier.mean)
                << " (delta "
                << format_real(
                       metrics.treatment_minus_control
                           .brier_delta.mean)
                << "); log loss "
                << format_real(
                       metrics.control.soft_log_loss.mean)
                << " -> "
                << format_real(
                       metrics.treatment.soft_log_loss.mean)
                << " (delta "
                << format_real(
                       metrics.treatment_minus_control
                           .soft_log_loss_delta.mean)
                << ")\n";
        };
    const auto write_scope =
        [&](std::string_view name,
            const ScopeMetrics& scope) {
            output
                << "\n  " << name << ": all="
                << scope.all_records.records
                << ", common="
                << scope.common_records.records
                << ", actor-games="
                << scope.common_records.actor_games
                << ", physical-games="
                << scope.common_records.physical_games
                << ", excluded actor-games="
                << scope.common_records
                       .excluded_actor_games
                << '\n';
            write_weight(
                "all / record-weighted",
                scope.all_records.record_weighted);
            write_weight(
                "all / equal-actor-game",
                scope.all_records.equal_actor_game);
            if (scope.common_records.available) {
                write_weight(
                    "common / record-weighted",
                    scope.common_records.record_weighted);
                write_weight(
                    "common / equal-actor-game",
                    scope.common_records.equal_actor_game);
            }
            output
                << "    control distance histogram 0..4:";
            for (const std::size_t count :
                 scope.control_turn_distance_histogram) {
                output << ' ' << count;
            }
            output
                << "; tails control="
                << scope.control_terminal_tail_records
                << ", treatment="
                << scope.treatment_terminal_tail_records
                << '\n';
            constexpr std::array<
                std::string_view,
                kRootTurnStratumCount>
                stratum_names = {"<=3", "4-7", ">=8"};
            for (std::size_t stratum = 0;
                 stratum < stratum_names.size(); ++stratum) {
                const auto& rows =
                    scope.common_by_root_turn[stratum];
                output
                    << "    common bias delta, root turn "
                    << stratum_names[stratum] << ": ";
                if (!rows.available) {
                    output << "unavailable\n";
                    continue;
                }
                output
                    << format_real(
                           rows.record_weighted
                               .treatment_minus_control
                               .signed_bias_delta.mean)
                    << " record-weighted; "
                    << format_real(
                           rows.equal_actor_game
                               .treatment_minus_control
                               .signed_bias_delta.mean)
                    << " equal-actor-game (n="
                    << rows.records << ")\n";
            }
        };
    write_scope(
        "Pooled", report.scientific.metrics.pooled);
    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
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
    const AuditReport& report,
    std::ostream& output) {
    constexpr std::string_view schema = "ta4-audit-v1";
    output
        << "\nTA4_TSV_BEGIN\n"
        << "schema\trow\tscope\tsubject\tmetric\testimate\tse"
           "\tci_low\tci_high\trecords\tclusters\tstatus\tdetail\n";
    const auto row =
        [&output, schema](
            std::string_view type,
            std::string_view scope,
            std::string_view subject,
            std::string_view metric,
            std::string_view estimate,
            std::string_view se,
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
                << sanitize_tsv(estimate) << '\t'
                << sanitize_tsv(se) << '\t'
                << sanitize_tsv(low) << '\t'
                << sanitize_tsv(high) << '\t'
                << sanitize_tsv(records) << '\t'
                << sanitize_tsv(clusters) << '\t'
                << sanitize_tsv(status) << '\t'
                << sanitize_tsv(detail) << '\n';
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
        "config", "global", "TA4-0", "recipe",
        "", "", "", "",
        std::to_string(
            report.scientific.physical_games),
        "", "FIXED",
        "seed=" +
            std::to_string(report.scientific.seed) +
            ";generation=" +
            std::to_string(
                report.scientific.generation) +
            ";blocks=" +
            std::to_string(
                report.scientific.balanced_blocks) +
            ";K=1;H=4;epsilon=0.05;terminal_weight=0.5");

    const auto write_estimate =
        [&row](
            std::string_view scope,
            std::string_view subject,
            std::string_view metric,
            const ClusteredEstimate& estimate) {
            row(
                "metric", scope, subject, metric,
                format_real(estimate.mean),
                format_real(estimate.standard_error),
                format_real(
                    estimate.confidence_lower_95),
                format_real(
                    estimate.confidence_upper_95),
                std::to_string(estimate.records),
                std::to_string(estimate.clusters),
                "", "");
        };
    const auto write_weighting =
        [&row, &write_estimate](
            std::string_view scope,
            std::string_view row_set,
            std::string_view weighting,
            const RowSetMetrics& rows,
            const WeightingMetrics& metrics) {
            const std::string subject =
                std::string(row_set) + "/" +
                std::string(weighting);
            row(
                "accounting", scope, subject, "coverage",
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
            write_estimate(
                scope, subject + "/control",
                "brier", metrics.control.brier);
            write_estimate(
                scope, subject + "/treatment",
                "brier", metrics.treatment.brier);
            write_estimate(
                scope, subject + "/delta",
                "brier",
                metrics.treatment_minus_control
                    .brier_delta);
            write_estimate(
                scope, subject + "/control",
                "soft_log_loss",
                metrics.control.soft_log_loss);
            write_estimate(
                scope, subject + "/treatment",
                "soft_log_loss",
                metrics.treatment.soft_log_loss);
            write_estimate(
                scope, subject + "/delta",
                "soft_log_loss",
                metrics.treatment_minus_control
                    .soft_log_loss_delta);
            write_estimate(
                scope, subject + "/control",
                "signed_bias",
                metrics.control.signed_bias);
            write_estimate(
                scope, subject + "/treatment",
                "signed_bias",
                metrics.treatment.signed_bias);
            write_estimate(
                scope, subject + "/delta",
                "signed_bias",
                metrics.treatment_minus_control
                    .signed_bias_delta);
            row(
                "diagnostic", scope,
                subject + "/control", "target_distribution",
                format_real(metrics.control.target_mean),
                "", "",
                format_real(
                    metrics.control.target_variance),
                std::to_string(rows.records), "", "",
                "saturated=" +
                    std::to_string(
                        metrics.control
                            .saturated_targets) +
                    ";fraction=" +
                    format_real(
                        metrics.control
                            .saturation_fraction));
            row(
                "diagnostic", scope,
                subject + "/treatment",
                "target_distribution",
                format_real(metrics.treatment.target_mean),
                "", "",
                format_real(
                    metrics.treatment.target_variance),
                std::to_string(rows.records), "", "",
                "saturated=" +
                    std::to_string(
                        metrics.treatment
                            .saturated_targets) +
                    ";fraction=" +
                    format_real(
                        metrics.treatment
                            .saturation_fraction));
        };
    const auto write_scope =
        [&](std::string_view name,
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
                name, "common", "record",
                scope.common_records,
                scope.common_records.record_weighted);
            write_weighting(
                name, "common", "equal_actor_game",
                scope.common_records,
                scope.common_records.equal_actor_game);
            std::string histogram;
            for (std::size_t distance = 0;
                 distance <
                 scope.control_turn_distance_histogram.size();
                 ++distance) {
                if (!histogram.empty()) {
                    histogram += ',';
                }
                histogram +=
                    std::to_string(distance) + ":" +
                    std::to_string(
                        scope
                            .control_turn_distance_histogram[
                                distance]);
            }
            row(
                "diagnostic", name, "targets", "tails",
                "", "", "", "",
                std::to_string(
                    scope.all_records.records),
                "", "",
                "control_bootstrap=" +
                    std::to_string(
                        scope.control_bootstrapped_records) +
                    ";control_tail=" +
                    std::to_string(
                        scope.control_terminal_tail_records) +
                    ";treatment_bootstrap=" +
                    std::to_string(
                        scope
                            .treatment_bootstrapped_records) +
                    ";treatment_tail=" +
                    std::to_string(
                        scope
                            .treatment_terminal_tail_records) +
                    ";control_distance_hist=" + histogram);
            constexpr std::array<
                std::string_view,
                kRootTurnStratumCount>
                stratum_names = {"<=3", "4-7", ">=8"};
            for (std::size_t stratum = 0;
                 stratum < stratum_names.size(); ++stratum) {
                const auto& rows =
                    scope.common_by_root_turn[stratum];
                if (!rows.available) {
                    continue;
                }
                const std::string stratum_scope =
                    std::string(name) + "/turn-" +
                    std::string(stratum_names[stratum]);
                write_estimate(
                    stratum_scope,
                    "common/record/delta",
                    "signed_bias",
                    rows.record_weighted
                        .treatment_minus_control
                        .signed_bias_delta);
                write_estimate(
                    stratum_scope,
                    "common/equal_actor_game/delta",
                    "signed_bias",
                    rows.equal_actor_game
                        .treatment_minus_control
                        .signed_bias_delta);
            }
        };
    write_scope(
        "Pooled", report.scientific.metrics.pooled);
    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        write_scope(
            audit_deck_name(static_cast<DeckId>(deck)),
            report.scientific.metrics.by_deck[deck]);
    }
    row(
        "mechanical", "global", "repeat",
        "bit_identity", "", "", "", "", "", "",
        report.repeated_report_bit_identical
            ? "PASS"
            : "FAIL",
        "schedule=" +
            report.scientific.schedule_hash +
            ";trace=" + report.scientific.trace_hash +
            ";outcome=" +
            report.scientific.outcome_hash +
            ";control=" +
            report.scientific.control_target_hash +
            ";treatment=" +
            report.scientific.treatment_target_hash +
            ";scoring=" +
            report.scientific.scoring_hash);
    row(
        "mechanical", "global", "hidden_repartition",
        "bit_identity", "", "", "", "",
        std::to_string(
            report.scientific
                .hidden_repartition_states),
        "",
        report.scientific.hidden_repartition_passed
            ? "PASS"
            : "FAIL",
        "opponent-hand/library only");
    row(
        "gate", "offline", "TA4", "verdict",
        "", "", "", "", "", "",
        report.passed ? "PASS" : "REJECT",
        report.gate.failures.empty()
            ? ""
            : report.gate.failures.front());
    output << "TA4_TSV_END\n";
}

} // namespace old_school::turn_alignment_audit
