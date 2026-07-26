#include "old_school/replay_weight_audit.hpp"

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
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::replay_weight_audit {
namespace {

constexpr double kLogClamp = 1.0e-12;
constexpr double kMaterialBias = 0.05;
constexpr double kBiasSafetyBand = 0.010;
constexpr double kEarlyGreenEffect = -0.005;
constexpr double kPooledLossUpper = 0.001;
constexpr double kDeckLossPoint = 0.002;
constexpr double kDeckLossUpper = 0.003;
constexpr double kMdeMultiplier = 2.802;

using ActorKey = std::pair<std::size_t, std::size_t>;
using ActorTurnKey =
    std::tuple<std::size_t, std::size_t, std::size_t>;

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
            "RB0 record contains an invalid deck");
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
    throw std::invalid_argument("RB0 deck is out of range");
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
    throw std::invalid_argument("RB0 deck is out of range");
}

void hash_task(ContentHash& hash, const AuditTask& task) {
    hash.add_size(task.physical_game);
    hash.add_size(task.block);
    hash.add_size(task.scheduled.schedule_index);
    hash.add_size(task.scheduled.pairing_index);
    hash.add_size(
        static_cast<std::size_t>(
            task.scheduled.seat_decks[0]));
    hash.add_size(
        static_cast<std::size_t>(
            task.scheduled.seat_decks[1]));
    hash.add_size(task.scheduled.starting_player);
    hash.add_u64(task.scheduled.seed);
}

std::string schedule_digest(
    std::span<const AuditTask> tasks) {
    ContentHash hash;
    hash.add_text("rb0-schedule-v1");
    for (const AuditTask& task : tasks) {
        hash_task(hash, task);
    }
    return hash.finish();
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
    hash.add_double(record.ro4_target);
    hash_optional_index(hash, record.ro4_future_index);
    hash.add_double(record.treatment_weight);
}

DistributionSummary summarize_values(
    std::vector<double> values) {
    DistributionSummary result;
    if (values.empty()) {
        return result;
    }
    for (const double value : values) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "RB0 distribution contains a nonfinite value");
        }
    }
    std::sort(values.begin(), values.end());
    const auto quantile =
        [&](double probability) {
            const std::size_t index =
                static_cast<std::size_t>(
                    probability *
                    static_cast<double>(values.size() - 1));
            return values[index];
        };
    const long double sum = std::accumulate(
        values.begin(), values.end(), 0.0L);
    result.count = values.size();
    result.minimum = values.front();
    result.q25 = quantile(0.25);
    result.median = quantile(0.50);
    result.q75 = quantile(0.75);
    result.maximum = values.back();
    result.mean = static_cast<double>(
        sum / static_cast<long double>(values.size()));
    return result;
}

double mass_tolerance(double expected) {
    return 64.0 * std::numeric_limits<double>::epsilon() *
           std::max(1.0, std::abs(expected));
}

WeightDiagnostics describe_weights(
    std::span<const AuditRecord* const> records,
    bool verify_hierarchy) {
    WeightDiagnostics result;
    if (records.empty()) {
        return result;
    }

    std::map<ActorKey, std::size_t> actor_counts;
    std::map<ActorKey, std::set<std::size_t>> actor_turns;
    std::map<ActorTurnKey, std::size_t> turn_counts;
    std::map<ActorKey, long double> actor_masses;
    std::map<ActorTurnKey, long double> turn_masses;
    std::vector<double> weights;
    weights.reserve(records.size());
    long double total = 0.0L;
    long double squared = 0.0L;
    bool finite_positive = true;
    for (const AuditRecord* record : records) {
        const ActorKey actor = {
            record->physical_game, record->perspective};
        const ActorTurnKey actor_turn = {
            record->physical_game, record->perspective,
            record->root_turn};
        ++actor_counts[actor];
        actor_turns[actor].insert(record->root_turn);
        ++turn_counts[actor_turn];
        actor_masses[actor] += record->treatment_weight;
        turn_masses[actor_turn] += record->treatment_weight;
        weights.push_back(record->treatment_weight);
        finite_positive =
            finite_positive &&
            std::isfinite(record->treatment_weight) &&
            record->treatment_weight > 0.0;
        total += record->treatment_weight;
        squared +=
            static_cast<long double>(record->treatment_weight) *
            record->treatment_weight;
    }

    std::vector<double> actor_record_counts;
    std::vector<double> actor_turn_counts;
    std::vector<double> turn_record_counts;
    actor_record_counts.reserve(actor_counts.size());
    actor_turn_counts.reserve(actor_turns.size());
    turn_record_counts.reserve(turn_counts.size());
    for (const auto& [actor, count] : actor_counts) {
        static_cast<void>(actor);
        actor_record_counts.push_back(
            static_cast<double>(count));
    }
    for (const auto& [actor, turns] : actor_turns) {
        static_cast<void>(actor);
        actor_turn_counts.push_back(
            static_cast<double>(turns.size()));
    }
    for (const auto& [actor_turn, count] : turn_counts) {
        static_cast<void>(actor_turn);
        turn_record_counts.push_back(
            static_cast<double>(count));
    }

    result.records = records.size();
    result.actor_games = actor_counts.size();
    result.actor_turns = turn_counts.size();
    result.total_weight = static_cast<double>(total);
    result.kish_effective_sample_size =
        squared == 0.0L
            ? 0.0
            : static_cast<double>(total * total / squared);
    result.weights = summarize_values(std::move(weights));
    result.actor_record_counts =
        summarize_values(std::move(actor_record_counts));
    result.actor_turn_counts =
        summarize_values(std::move(actor_turn_counts));
    result.turn_record_multiplicities =
        summarize_values(std::move(turn_record_counts));
    result.finite_positive = finite_positive;

    if (!verify_hierarchy) {
        return result;
    }
    const double expected_total =
        static_cast<double>(records.size());
    const double expected_actor =
        expected_total /
        static_cast<double>(actor_counts.size());
    result.expected_total_weight = expected_total;
    result.expected_actor_weight = expected_actor;
    result.maximum_global_mass_error =
        std::abs(result.total_weight - expected_total);
    result.global_mass_identity =
        result.maximum_global_mass_error <=
        mass_tolerance(expected_total);
    result.actor_mass_identity = true;
    result.turn_mass_identity = true;
    for (const auto& [actor, mass] : actor_masses) {
        const double error = std::abs(
            static_cast<double>(mass) - expected_actor);
        result.maximum_actor_mass_error =
            std::max(result.maximum_actor_mass_error, error);
        result.actor_mass_identity =
            result.actor_mass_identity &&
            error <= mass_tolerance(expected_actor);
        const std::size_t turn_count =
            actor_turns.at(actor).size();
        const double expected_turn =
            expected_actor /
            static_cast<double>(turn_count);
        for (const std::size_t turn : actor_turns.at(actor)) {
            const double turn_error = std::abs(
                static_cast<double>(
                    turn_masses.at({
                        actor.first, actor.second, turn})) -
                expected_turn);
            result.maximum_turn_mass_error =
                std::max(
                    result.maximum_turn_mass_error,
                    turn_error);
            result.turn_mass_identity =
                result.turn_mass_identity &&
                turn_error <= mass_tolerance(expected_turn);
        }
    }
    return result;
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

std::size_t stratum_index(RootTurnStratum value) {
    return static_cast<std::size_t>(value);
}

double soft_log_loss(double prediction, double target) {
    prediction =
        std::clamp(prediction, kLogClamp, 1.0 - kLogClamp);
    return -target * std::log(prediction) -
           (1.0 - target) * std::log(1.0 - prediction);
}

ClusteredEstimate estimate_from_cluster_scores(
    std::size_t records, double estimate,
    const std::map<std::size_t, long double>& scores) {
    if (records == 0 || scores.size() < 2) {
        throw std::invalid_argument(
            "RB0 clustered ratio estimate requires records and "
            "at least two physical games");
    }
    long double square_sum = 0.0L;
    for (const auto& [cluster, score] : scores) {
        static_cast<void>(cluster);
        square_sum += score * score;
    }
    const long double clusters =
        static_cast<long double>(scores.size());
    const double standard_error = std::sqrt(
        std::max(
            0.0,
            static_cast<double>(
                clusters / (clusters - 1.0L) *
                square_sum)));
    return {
        .records = records,
        .clusters = scores.size(),
        .mean = estimate,
        .standard_error = standard_error,
        .confidence_lower_95 =
            estimate -
            terminal_weight_eval::kNormal95CriticalValue *
                standard_error,
        .confidence_upper_95 =
            estimate +
            terminal_weight_eval::kNormal95CriticalValue *
                standard_error,
    };
}

template <typename Metric>
MetricComparison compare_metric(
    const std::vector<const AuditRecord*>& records,
    Metric metric) {
    if (records.empty()) {
        throw std::invalid_argument(
            "RB0 metric comparison requires records");
    }
    std::vector<terminal_weight_eval::ClusteredValue>
        control_values;
    control_values.reserve(records.size());
    long double control_sum = 0.0L;
    long double treatment_sum = 0.0L;
    long double weight_sum = 0.0L;
    for (const AuditRecord* record : records) {
        const double value = metric(*record);
        if (!std::isfinite(value) ||
            !std::isfinite(record->treatment_weight) ||
            record->treatment_weight <= 0.0) {
            throw std::invalid_argument(
                "RB0 metric row is nonfinite or nonpositive");
        }
        control_values.push_back({
            .cluster = record->physical_game,
            .value = value,
        });
        control_sum += value;
        treatment_sum +=
            static_cast<long double>(
                record->treatment_weight) *
            value;
        weight_sum += record->treatment_weight;
    }
    if (weight_sum <= 0.0L) {
        throw std::invalid_argument(
            "RB0 scoped weight sum must be positive");
    }
    const long double count =
        static_cast<long double>(records.size());
    const double control_mean =
        static_cast<double>(control_sum / count);
    const double treatment_mean =
        static_cast<double>(
            treatment_sum / weight_sum);
    const double contrast_mean =
        treatment_mean - control_mean;

    const bool all_unit_weights =
        std::all_of(
            records.begin(), records.end(),
            [](const AuditRecord* record) {
                return bit_identical(
                    record->treatment_weight, 1.0);
            });
    const ClusteredEstimate control =
        terminal_weight_eval::cr1_clustered_estimate(
            control_values);
    if (all_unit_weights) {
        return {
            .control = control,
            .treatment = control,
            .treatment_minus_control = {
                .records = records.size(),
                .clusters = control.clusters,
                .mean = 0.0,
                .standard_error = 0.0,
                .confidence_lower_95 = 0.0,
                .confidence_upper_95 = 0.0,
            },
        };
    }

    std::map<std::size_t, long double> treatment_scores;
    std::map<std::size_t, long double> contrast_scores;
    for (const AuditRecord* record : records) {
        const long double value = metric(*record);
        const long double weighted_score =
            static_cast<long double>(
                record->treatment_weight) /
            weight_sum *
            (value - treatment_mean);
        const long double control_score =
            1.0L / count * (value - control_mean);
        treatment_scores[record->physical_game] +=
            weighted_score;
        contrast_scores[record->physical_game] +=
            weighted_score - control_score;
    }

    MetricComparison result;
    result.control = control;
    result.treatment = estimate_from_cluster_scores(
        records.size(), treatment_mean, treatment_scores);
    result.treatment_minus_control =
        estimate_from_cluster_scores(
            records.size(), contrast_mean, contrast_scores);
    return result;
}

RowSetMetrics score_scope(
    const std::vector<const AuditRecord*>& records) {
    RowSetMetrics result;
    if (records.empty()) {
        return result;
    }
    std::set<ActorKey> actors;
    std::set<std::size_t> games;
    std::vector<double> terminal_targets;
    std::vector<double> ro4_targets;
    terminal_targets.reserve(records.size());
    ro4_targets.reserve(records.size());
    for (const AuditRecord* record : records) {
        actors.emplace(
            record->physical_game, record->perspective);
        games.insert(record->physical_game);
        terminal_targets.push_back(record->terminal_target);
        ro4_targets.push_back(record->ro4_target);
        if (record->ro4_future_index.has_value()) {
            ++result.bootstrapped_records;
        } else {
            ++result.terminal_tail_records;
        }
    }
    result.records = records.size();
    result.actor_games = actors.size();
    result.physical_games = games.size();
    result.weights = describe_weights(records, false);
    result.terminal_targets =
        summarize_values(terminal_targets);
    result.ro4_targets =
        summarize_values(ro4_targets);
    if (games.size() < 2) {
        return result;
    }
    result.available = true;
    result.metrics.signed_bias =
        compare_metric(
            records, [](const AuditRecord& record) {
                return record.ro4_target -
                       record.terminal_target;
            });
    result.metrics.brier =
        compare_metric(
            records, [](const AuditRecord& record) {
                const double error =
                    record.ro4_target -
                    record.terminal_target;
                return error * error;
            });
    result.metrics.soft_log_loss =
        compare_metric(
            records, [](const AuditRecord& record) {
                return soft_log_loss(
                    record.ro4_target,
                    record.terminal_target);
            });
    result.achieved_mde_95_80 =
        kMdeMultiplier *
        result.metrics.signed_bias
            .treatment_minus_control.standard_error;
    return result;
}

ScopeMetrics score_deck_scope(
    const std::vector<const AuditRecord*>& records) {
    ScopeMetrics result;
    std::vector<const AuditRecord*> eligible;
    std::array<std::vector<const AuditRecord*>,
               kRootTurnStratumCount>
        all_by_turn;
    std::array<std::vector<const AuditRecord*>,
               kRootTurnStratumCount>
        eligible_by_turn;
    for (const AuditRecord* record : records) {
        const std::size_t stratum =
            stratum_index(root_stratum(record->root_turn));
        all_by_turn[stratum].push_back(record);
        if (record->ro4_future_index.has_value()) {
            eligible.push_back(record);
            eligible_by_turn[stratum].push_back(record);
        }
    }
    result.all_records = score_scope(records);
    result.ro4_eligible = score_scope(eligible);
    for (std::size_t stratum = 0;
         stratum < kRootTurnStratumCount; ++stratum) {
        result.all_by_root_turn[stratum] =
            score_scope(all_by_turn[stratum]);
        result.eligible_by_root_turn[stratum] =
            score_scope(eligible_by_turn[stratum]);
    }
    return result;
}

struct HiddenClone {
    GameState state;
    bool changed = false;
};

HiddenClone hidden_repartition_clone(
    const GameState& source, std::size_t perspective) {
    if (perspective >= source.players.size()) {
        throw std::out_of_range(
            "RB0 hidden-clone perspective is out of range");
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
    // A library reorder is not a hidden-zone repartition. If no distinct
    // hand/library identities can be exchanged, this state contributes to
    // the invariance check but not to the changed-repartition count.
    return result;
}

void validate_task(
    const AuditTask& task,
    const CaptureConfig& config) {
    if (task.scheduled.schedule_index >=
            learned_iteration::kBalancedScheduleGames ||
        task.scheduled.pairing_index >=
            learned_iteration::kBalancedPairings ||
        task.scheduled.starting_player >= 2 ||
        task.scheduled.seat_decks[0] ==
            task.scheduled.seat_decks[1]) {
        throw std::invalid_argument(
            "RB0 task has invalid schedule metadata");
    }
    static_cast<void>(
        deck_index(task.scheduled.seat_decks[0]));
    static_cast<void>(
        deck_index(task.scheduled.seat_decks[1]));
    if (config.schedule_generation == 0) {
        throw std::invalid_argument(
            "RB0 collection schedule generation must be positive");
    }
    if (task.block >
        (std::numeric_limits<std::size_t>::max() -
         task.scheduled.schedule_index) /
            learned_iteration::kBalancedScheduleGames) {
        throw std::invalid_argument(
            "RB0 task physical-game identity overflows");
    }
    const std::size_t expected_physical_game =
        task.block *
            learned_iteration::kBalancedScheduleGames +
        task.scheduled.schedule_index;
    const auto expected_block =
        learned_iteration::balanced_schedule(
            config.schedule_seed,
            config.schedule_generation,
            task.block);
    if (task.physical_game != expected_physical_game ||
        task.scheduled !=
            expected_block[
                task.scheduled.schedule_index]) {
        throw std::invalid_argument(
            "RB0 task is not bound to the exact "
            "seed/generation/block schedule");
    }
}

void require_trace_turns(
    std::span<const std::size_t> turns) {
    if (turns.empty()) {
        throw std::runtime_error(
            "RB0 game produced an empty trace");
    }
    if (turns.front() == 0) {
        throw std::runtime_error(
            "RB0 trace starts before turn one");
    }
    for (std::size_t index = 1; index < turns.size();
         ++index) {
        if (turns[index] < turns[index - 1]) {
            throw std::runtime_error(
                "RB0 trace turn number regressed");
        }
        if (turns[index] - turns[index - 1] > 1) {
            throw std::runtime_error(
                "RB0 trace skipped a calendar turn");
        }
    }
}

struct TaskCapture {
    std::vector<AuditRecord> records;
    std::vector<AuditRecord> hidden_records;
    std::size_t hidden_repartition_states = 0;
    bool trace_invariants_passed = true;
    bool ro4_identity_passed = true;
    bool terminal_tail_identity_passed = true;
    bool hidden_repartition_passed = true;
    std::string trace_hash;
    std::string outcome_hash;
    std::string feature_hash;
    std::string ro4_target_hash;
    std::string hidden_ro4_target_hash;
};

TaskCapture run_task(
    const AuditTask& task,
    std::shared_ptr<const LearnedModel> parent,
    const CaptureConfig& capture_config) {
    validate_task(task, capture_config);
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
            "RB0 starting-player schedule mismatch");
    }

    std::vector<std::size_t> turns;
    turns.reserve(trace.size());
    for (const GameState& state : trace) {
        turns.push_back(state.turn_number);
    }
    require_trace_turns(turns);

    TaskCapture output;
    std::array<double, 2> terminal_targets{};
    std::array<std::vector<double>, 2> parent_values;
    std::array<std::vector<double>, 2> clone_values;
    std::array<std::vector<double>, 2> targets;
    std::array<std::vector<double>, 2> clone_targets;
    std::array<std::vector<std::size_t>, 2> clone_turns;
    ContentHash feature_hash;
    ContentHash hidden_feature_hash;
    feature_hash.add_text("rb0-features-v1");
    hidden_feature_hash.add_text("rb0-features-v1");
    for (std::size_t perspective = 0;
         perspective < 2; ++perspective) {
        terminal_targets[perspective] =
            learned_discounted_terminal_target(
                result, perspective);
        require_probability(
            terminal_targets[perspective],
            "RB0 discounted terminal target");
        parent_values[perspective].reserve(trace.size());
        clone_values[perspective].reserve(trace.size());
        clone_turns[perspective].reserve(trace.size());
        for (const GameState& state : trace) {
            const std::vector<double> features =
                learned_observation(state, perspective);
            const double value =
                learned_critic_value(
                    state, perspective, parent);
            require_probability(value, "RB0 parent value");
            hash_observation(feature_hash, features);
            parent_values[perspective].push_back(value);

            if (capture_config.verify_hidden_repartition) {
                const HiddenClone clone =
                    hidden_repartition_clone(
                        state, perspective);
                if (clone.changed) {
                    ++output.hidden_repartition_states;
                }
                if (observe_game_state(state, perspective) !=
                    observe_game_state(
                        clone.state, perspective)) {
                    throw std::runtime_error(
                        "RB0 hidden repartition changed the "
                        "public information set");
                }
                const std::vector<double> clone_features =
                    learned_observation(
                        clone.state, perspective);
                if (!bit_identical(
                        features, clone_features)) {
                    throw std::runtime_error(
                        "RB0 hidden repartition changed critic "
                        "features");
                }
                hash_observation(
                    hidden_feature_hash, clone_features);
                const double clone_value =
                    learned_critic_value(
                        clone.state, perspective, parent);
                if (!bit_identical(value, clone_value)) {
                    throw std::runtime_error(
                        "RB0 hidden repartition changed critic "
                        "value");
                }
                clone_values[perspective].push_back(
                    clone_value);
                clone_turns[perspective].push_back(
                    clone.state.turn_number);
            } else {
                hash_observation(
                    hidden_feature_hash, features);
                clone_values[perspective].push_back(value);
                clone_turns[perspective].push_back(
                    state.turn_number);
            }
        }
        targets[perspective] =
            learned_iteration::n_state_bootstrap_targets(
                parent_values[perspective],
                terminal_targets[perspective],
                kRecordBootstrapDistance);
        clone_targets[perspective] =
            learned_iteration::n_state_bootstrap_targets(
                clone_values[perspective],
                terminal_targets[perspective],
                kRecordBootstrapDistance);
        if (!bit_identical(
                targets[perspective],
                clone_targets[perspective])) {
            throw std::runtime_error(
                "RB0 hidden repartition changed RO4 targets");
        }
    }
    output.feature_hash = feature_hash.finish();
    if (output.feature_hash != hidden_feature_hash.finish()) {
        throw std::runtime_error(
            "RB0 hidden repartition changed feature hash");
    }

    ContentHash trace_hash;
    ContentHash outcome_hash;
    ContentHash target_hash;
    ContentHash hidden_target_hash;
    trace_hash.add_text("rb0-trace-v1");
    outcome_hash.add_text("rb0-outcome-v1");
    target_hash.add_text("rb0-ro4-target-v1");
    hidden_target_hash.add_text("rb0-ro4-target-v1");
    hash_task(trace_hash, task);
    hash_task(outcome_hash, task);
    trace_hash.add_size(trace.size());
    outcome_hash.add_u64(
        static_cast<std::uint64_t>(result.winner + 1));
    outcome_hash.add_size(
        static_cast<std::size_t>(result.reason));
    outcome_hash.add_size(result.turns);
    outcome_hash.add_size(result.starting_player);
    outcome_hash.add_u64(static_cast<std::uint64_t>(
        static_cast<std::int64_t>(
            result.ending_life[0])));
    outcome_hash.add_u64(static_cast<std::uint64_t>(
        static_cast<std::int64_t>(
            result.ending_life[1])));
    outcome_hash.add_double(terminal_targets[0]);
    outcome_hash.add_double(terminal_targets[1]);
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
            const bool has_future =
                index <=
                    std::numeric_limits<std::size_t>::max() -
                        kRecordBootstrapDistance &&
                index + kRecordBootstrapDistance <
                    trace.size();
            const std::optional<std::size_t> future =
                has_future
                    ? std::optional<std::size_t>(
                          index +
                          kRecordBootstrapDistance)
                    : std::nullopt;
            const double direct =
                has_future
                    ? 0.5 * terminal_targets[perspective] +
                          0.5 *
                              parent_values[perspective]
                                           [*future]
                    : terminal_targets[perspective];
            if (!bit_identical(
                    targets[perspective][index], direct)) {
                throw std::logic_error(
                    "RB0 RO4 helper does not match canonical "
                    "0.50*z + 0.50*V(i+4)");
            }
            if (!has_future &&
                !bit_identical(
                    targets[perspective][index],
                    terminal_targets[perspective])) {
                throw std::logic_error(
                    "RB0 terminal tail is not bit-identical "
                    "to z");
            }
            const AuditRecord record = {
                .physical_game = task.physical_game,
                .block = task.block,
                .schedule_index =
                    task.scheduled.schedule_index,
                .perspective = perspective,
                .deck =
                    task.scheduled.seat_decks[perspective],
                .root_index = index,
                .root_turn = trace[index].turn_number,
                .terminal_target =
                    terminal_targets[perspective],
                .ro4_target =
                    targets[perspective][index],
                .ro4_future_index = future,
                .treatment_weight = 1.0,
            };
            const AuditRecord hidden_record = {
                .physical_game = task.physical_game,
                .block = task.block,
                .schedule_index =
                    task.scheduled.schedule_index,
                .perspective = perspective,
                .deck =
                    task.scheduled.seat_decks[perspective],
                .root_index = index,
                .root_turn =
                    clone_turns[perspective][index],
                .terminal_target =
                    terminal_targets[perspective],
                .ro4_target =
                    clone_targets[perspective][index],
                .ro4_future_index = future,
                .treatment_weight = 1.0,
            };
            output.records.push_back(record);
            output.hidden_records.push_back(hidden_record);
            target_hash.add_size(task.physical_game);
            target_hash.add_size(index);
            target_hash.add_size(perspective);
            target_hash.add_double(record.ro4_target);
            hidden_target_hash.add_size(task.physical_game);
            hidden_target_hash.add_size(index);
            hidden_target_hash.add_size(perspective);
            hidden_target_hash.add_double(
                hidden_record.ro4_target);
        }
    }
    output.trace_hash = trace_hash.finish();
    output.outcome_hash = outcome_hash.finish();
    output.ro4_target_hash = target_hash.finish();
    output.hidden_ro4_target_hash =
        hidden_target_hash.finish();
    if (output.records.size() !=
            output.hidden_records.size() ||
        output.ro4_target_hash !=
            output.hidden_ro4_target_hash) {
        throw std::runtime_error(
            "RB0 hidden repartition changed target records or "
            "hash");
    }
    return output;
}

bool same_sign(double left, double right) {
    return (left > 0.0 && right > 0.0) ||
           (left < 0.0 && right < 0.0);
}

bool has_material_bias(const ClusteredEstimate& estimate) {
    return std::abs(estimate.mean) >= kMaterialBias &&
           (estimate.confidence_lower_95 > 0.0 ||
            estimate.confidence_upper_95 < 0.0);
}

std::string bool_text(bool value) {
    return value ? "PASS" : "FAIL";
}

void require_hash(std::string_view hash, std::string_view field) {
    if (hash.size() != 64 ||
        !std::all_of(
            hash.begin(), hash.end(),
            [](char character) {
                return (character >= '0' &&
                        character <= '9') ||
                       (character >= 'a' &&
                        character <= 'f');
            })) {
        throw std::invalid_argument(
            std::string("RB0 malformed ") +
            std::string(field));
    }
}

} // namespace

std::vector<AuditTask> audit_schedule(
    std::uint64_t seed, std::size_t generation,
    std::size_t balanced_blocks) {
    if (generation == 0 || balanced_blocks == 0) {
        throw std::invalid_argument(
            "RB0 schedule generation and block count must be "
            "positive");
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
        for (const auto& scheduled : schedule) {
            result.push_back({
                .physical_game =
                    block *
                        learned_iteration::
                            kBalancedScheduleGames +
                    scheduled.schedule_index,
                .block = block,
                .scheduled = scheduled,
            });
        }
    }
    return result;
}

std::vector<double> hierarchical_weights(
    std::span<const ReplayCoordinate> coordinates) {
    if (coordinates.empty()) {
        throw std::invalid_argument(
            "RB0 hierarchical weighting requires rows");
    }
    std::map<ActorKey, std::set<std::size_t>> actor_turns;
    std::map<ActorTurnKey, std::size_t> multiplicities;
    for (const ReplayCoordinate& coordinate : coordinates) {
        if (coordinate.perspective >= 2 ||
            coordinate.calendar_turn == 0) {
            throw std::invalid_argument(
                "RB0 hierarchical coordinate is malformed");
        }
        const ActorKey actor = {
            coordinate.physical_game,
            coordinate.perspective};
        actor_turns[actor].insert(
            coordinate.calendar_turn);
        ++multiplicities[{
            coordinate.physical_game,
            coordinate.perspective,
            coordinate.calendar_turn,
        }];
    }
    if (actor_turns.empty()) {
        throw std::invalid_argument(
            "RB0 hierarchical weighting found no actor-games");
    }

    const long double records =
        static_cast<long double>(coordinates.size());
    const long double actors =
        static_cast<long double>(actor_turns.size());
    std::vector<double> result;
    result.reserve(coordinates.size());
    for (const ReplayCoordinate& coordinate : coordinates) {
        const ActorKey actor = {
            coordinate.physical_game,
            coordinate.perspective};
        const std::size_t turns =
            actor_turns.at(actor).size();
        const std::size_t multiplicity =
            multiplicities.at({
                coordinate.physical_game,
                coordinate.perspective,
                coordinate.calendar_turn,
            });
        if (turns == 0 || multiplicity == 0) {
            throw std::logic_error(
                "RB0 hierarchical group is empty");
        }
        const long double weight =
            records /
            (actors * static_cast<long double>(turns) *
             static_cast<long double>(multiplicity));
        const double converted = static_cast<double>(weight);
        if (!std::isfinite(converted) || converted <= 0.0) {
            throw std::runtime_error(
                "RB0 hierarchical weight is invalid");
        }
        result.push_back(converted);
    }
    return result;
}

Capture collect(
    std::span<const AuditTask> tasks,
    std::shared_ptr<const LearnedModel> parent,
    CaptureConfig config) {
    if (tasks.empty()) {
        throw std::invalid_argument(
            "RB0 collection requires tasks");
    }
    if (!parent) {
        throw std::invalid_argument(
            "RB0 collection requires a frozen parent");
    }
    if (config.max_game_turns == 0 ||
        config.worker_count == 0 ||
        config.schedule_generation == 0) {
        throw std::invalid_argument(
            "RB0 collection bounds must be positive");
    }

    std::vector<AuditTask> canonical_tasks(
        tasks.begin(), tasks.end());
    std::sort(
        canonical_tasks.begin(), canonical_tasks.end(),
        [](const AuditTask& left, const AuditTask& right) {
            return std::tie(
                       left.physical_game, left.block,
                       left.scheduled.schedule_index) <
                   std::tie(
                       right.physical_game, right.block,
                       right.scheduled.schedule_index);
        });
    std::set<std::size_t> physical_games;
    for (const AuditTask& task : canonical_tasks) {
        validate_task(task, config);
        if (!physical_games.insert(task.physical_game).second) {
            throw std::invalid_argument(
                "RB0 physical-game IDs must be unique");
        }
    }

    std::vector<TaskCapture> slots(canonical_tasks.size());
    std::vector<std::exception_ptr> errors(
        canonical_tasks.size());
    std::atomic_size_t next_task = 0;
    const std::size_t worker_count =
        std::min(
            config.worker_count, canonical_tasks.size());
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count;
         ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t index =
                    next_task.fetch_add(
                        1, std::memory_order_relaxed);
                if (index >= canonical_tasks.size()) {
                    return;
                }
                try {
                    slots[index] =
                        run_task(
                            canonical_tasks[index],
                            parent, config);
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
    output.physical_games = canonical_tasks.size();
    output.actor_games = 2 * canonical_tasks.size();
    output.trace_invariants_passed = true;
    output.ro4_identity_passed = true;
    output.terminal_tail_identity_passed = true;
    output.hidden_repartition_passed = true;
    ContentHash trace_hash;
    ContentHash outcome_hash;
    ContentHash feature_hash;
    ContentHash target_hash;
    ContentHash hidden_target_hash;
    trace_hash.add_text("rb0-traces-v1");
    outcome_hash.add_text("rb0-outcomes-v1");
    feature_hash.add_text("rb0-features-v1");
    target_hash.add_text("rb0-ro4-targets-v1");
    hidden_target_hash.add_text("rb0-ro4-targets-v1");
    std::size_t total_records = 0;
    for (const TaskCapture& slot : slots) {
        total_records += slot.records.size();
    }
    output.records.reserve(total_records);
    std::vector<AuditRecord> hidden_records;
    hidden_records.reserve(total_records);
    for (std::size_t index = 0;
         index < canonical_tasks.size(); ++index) {
        const AuditTask& task = canonical_tasks[index];
        const TaskCapture& slot = slots[index];
        trace_hash.add_text(slot.trace_hash);
        outcome_hash.add_text(slot.outcome_hash);
        feature_hash.add_text(slot.feature_hash);
        target_hash.add_text(slot.ro4_target_hash);
        hidden_target_hash.add_text(
            slot.hidden_ro4_target_hash);
        output.trace_invariants_passed =
            output.trace_invariants_passed &&
            slot.trace_invariants_passed;
        output.ro4_identity_passed =
            output.ro4_identity_passed &&
            slot.ro4_identity_passed;
        output.terminal_tail_identity_passed =
            output.terminal_tail_identity_passed &&
            slot.terminal_tail_identity_passed;
        output.hidden_repartition_passed =
            output.hidden_repartition_passed &&
            slot.hidden_repartition_passed;
        output.hidden_repartition_states +=
            slot.hidden_repartition_states;
        ++output.ordered_pair_counts
              [deck_index(task.scheduled.seat_decks[0])]
              [deck_index(task.scheduled.seat_decks[1])];
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
        hidden_records.insert(
            hidden_records.end(),
            slot.hidden_records.begin(),
            slot.hidden_records.end());
    }

    std::vector<ReplayCoordinate> coordinates;
    std::vector<ReplayCoordinate> hidden_coordinates;
    coordinates.reserve(output.records.size());
    hidden_coordinates.reserve(hidden_records.size());
    for (const AuditRecord& record : output.records) {
        coordinates.push_back({
            .physical_game = record.physical_game,
            .perspective = record.perspective,
            .calendar_turn = record.root_turn,
        });
    }
    for (const AuditRecord& record : hidden_records) {
        hidden_coordinates.push_back({
            .physical_game = record.physical_game,
            .perspective = record.perspective,
            .calendar_turn = record.root_turn,
        });
    }
    const std::vector<double> weights =
        hierarchical_weights(coordinates);
    const std::vector<double> hidden_weights =
        hierarchical_weights(hidden_coordinates);
    if (weights.size() != output.records.size() ||
        hidden_weights.size() != hidden_records.size() ||
        weights.size() != hidden_weights.size()) {
        throw std::logic_error(
            "RB0 weight count does not match record count");
    }
    for (std::size_t index = 0;
         index < output.records.size(); ++index) {
        output.records[index].treatment_weight =
            weights[index];
        hidden_records[index].treatment_weight =
            hidden_weights[index];
    }
    std::vector<const AuditRecord*> record_pointers;
    record_pointers.reserve(output.records.size());
    for (const AuditRecord& record : output.records) {
        record_pointers.push_back(&record);
    }
    output.weights =
        describe_weights(record_pointers, true);
    output.weight_identity_passed =
        output.weights.finite_positive &&
        output.weights.global_mass_identity &&
        output.weights.actor_mass_identity &&
        output.weights.turn_mass_identity &&
        output.weights.actor_games == output.actor_games;

    ContentHash weight_hash;
    ContentHash hidden_weight_hash;
    ContentHash grouping_hash;
    ContentHash hidden_grouping_hash;
    ContentHash scoring_hash;
    ContentHash hidden_scoring_hash;
    weight_hash.add_text("rb0-weights-v1");
    hidden_weight_hash.add_text("rb0-weights-v1");
    grouping_hash.add_text("rb0-grouping-v1");
    hidden_grouping_hash.add_text("rb0-grouping-v1");
    scoring_hash.add_text("rb0-scoring-v1");
    hidden_scoring_hash.add_text("rb0-scoring-v1");
    for (std::size_t index = 0;
         index < output.records.size(); ++index) {
        const AuditRecord& record = output.records[index];
        const AuditRecord& hidden_record =
            hidden_records[index];
        weight_hash.add_size(record.physical_game);
        weight_hash.add_size(record.perspective);
        weight_hash.add_size(record.root_turn);
        weight_hash.add_double(record.treatment_weight);
        hidden_weight_hash.add_size(
            hidden_record.physical_game);
        hidden_weight_hash.add_size(
            hidden_record.perspective);
        hidden_weight_hash.add_size(
            hidden_record.root_turn);
        hidden_weight_hash.add_double(
            hidden_record.treatment_weight);
        grouping_hash.add_size(record.physical_game);
        grouping_hash.add_size(record.block);
        grouping_hash.add_size(record.schedule_index);
        grouping_hash.add_size(record.perspective);
        grouping_hash.add_size(record.root_index);
        grouping_hash.add_size(record.root_turn);
        hidden_grouping_hash.add_size(
            hidden_record.physical_game);
        hidden_grouping_hash.add_size(hidden_record.block);
        hidden_grouping_hash.add_size(
            hidden_record.schedule_index);
        hidden_grouping_hash.add_size(
            hidden_record.perspective);
        hidden_grouping_hash.add_size(
            hidden_record.root_index);
        hidden_grouping_hash.add_size(
            hidden_record.root_turn);
        hash_record(scoring_hash, record);
        hash_record(hidden_scoring_hash, hidden_record);
    }
    output.schedule_hash =
        schedule_digest(canonical_tasks);
    output.trace_hash = trace_hash.finish();
    output.outcome_hash = outcome_hash.finish();
    output.feature_hash = feature_hash.finish();
    output.grouping_hash = grouping_hash.finish();
    output.ro4_target_hash = target_hash.finish();
    output.weight_hash = weight_hash.finish();
    output.scoring_hash = scoring_hash.finish();
    output.hidden_grouping_identity_passed =
        output.grouping_hash ==
        hidden_grouping_hash.finish();
    output.hidden_target_hash_identity_passed =
        output.ro4_target_hash ==
        hidden_target_hash.finish();
    output.hidden_weight_identity_passed =
        output.weight_hash ==
            hidden_weight_hash.finish() &&
        bit_identical(weights, hidden_weights);
    output.hidden_scoring_hash_identity_passed =
        output.scoring_hash ==
        hidden_scoring_hash.finish();
    output.hidden_repartition_passed =
        output.hidden_repartition_passed &&
        output.hidden_grouping_identity_passed &&
        output.hidden_target_hash_identity_passed &&
        output.hidden_weight_identity_passed &&
        output.hidden_scoring_hash_identity_passed;
    return output;
}

MetricsReport score_records(
    std::span<const AuditRecord> records) {
    if (records.empty()) {
        throw std::invalid_argument(
            "RB0 scoring requires records");
    }
    std::set<std::tuple<std::size_t, std::size_t, std::size_t>>
        identities;
    std::vector<const AuditRecord*> pooled;
    std::array<std::vector<const AuditRecord*>, kDeckCount>
        by_deck;
    pooled.reserve(records.size());
    for (const AuditRecord& record : records) {
        if (record.perspective >= 2 ||
            record.root_turn == 0 ||
            !std::isfinite(record.treatment_weight) ||
            record.treatment_weight <= 0.0) {
            throw std::invalid_argument(
                "RB0 scoring record metadata is malformed");
        }
        require_probability(
            record.terminal_target,
            "RB0 terminal target");
        require_probability(record.ro4_target, "RB0 RO4 target");
        if (record.ro4_future_index.has_value()) {
            if (record.root_index >
                    std::numeric_limits<std::size_t>::max() -
                        kRecordBootstrapDistance ||
                *record.ro4_future_index !=
                    record.root_index +
                        kRecordBootstrapDistance) {
                throw std::invalid_argument(
                    "RB0 future is not record offset four");
            }
        } else if (!bit_identical(
                       record.ro4_target,
                       record.terminal_target)) {
            throw std::invalid_argument(
                "RB0 terminal tail is not bit-identical to z");
        }
        if (!identities
                 .emplace(
                     record.physical_game,
                     record.perspective,
                     record.root_index)
                 .second) {
            throw std::invalid_argument(
                "RB0 record identity is duplicated");
        }
        pooled.push_back(&record);
        by_deck[deck_index(record.deck)].push_back(&record);
    }

    MetricsReport report;
    report.pooled = score_deck_scope(pooled);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        report.by_deck[deck] =
            score_deck_scope(by_deck[deck]);
    }
    return report;
}

ScientificReport make_scientific_report(
    const Capture& capture, std::uint64_t seed,
    std::size_t generation, std::size_t balanced_blocks) {
    if (capture.records.empty() ||
        capture.physical_games == 0 ||
        capture.actor_games !=
            2 * capture.physical_games) {
        throw std::invalid_argument(
            "RB0 capture accounting is incomplete");
    }
    for (const std::string* hash : {
             &capture.schedule_hash,
             &capture.trace_hash,
             &capture.outcome_hash,
             &capture.feature_hash,
             &capture.grouping_hash,
             &capture.ro4_target_hash,
             &capture.weight_hash,
             &capture.scoring_hash}) {
        require_hash(*hash, "capture digest");
    }

    ScientificReport report;
    report.seed = seed;
    report.generation = generation;
    report.balanced_blocks = balanced_blocks;
    report.physical_games = capture.physical_games;
    report.actor_games = capture.actor_games;
    report.deck_seat_started_counts =
        capture.deck_seat_started_counts;
    report.ordered_pair_counts =
        capture.ordered_pair_counts;
    report.metrics = score_records(capture.records);
    report.weights = capture.weights;

    const std::size_t expected_games =
        balanced_blocks *
        learned_iteration::kBalancedScheduleGames;
    const std::size_t expected_cell =
        balanced_blocks *
        (kDeckCount - 1);
    report.schedule_balanced =
        capture.physical_games == expected_games &&
        capture.actor_games == 2 * expected_games &&
        capture.schedule_hash ==
            schedule_digest(
                audit_schedule(
                    seed, generation,
                    balanced_blocks));
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
    for (std::size_t first = 0;
         first < kDeckCount; ++first) {
        for (std::size_t second = 0;
             second < kDeckCount; ++second) {
            const std::size_t expected =
                first == second
                    ? 0
                    : 2 * balanced_blocks;
            report.schedule_balanced =
                report.schedule_balanced &&
                capture.ordered_pair_counts[first][second] ==
                    expected;
        }
    }

    const auto& pooled_eligible =
        report.metrics.pooled.ro4_eligible;
    report.eligible_coverage_passed =
        pooled_eligible.available &&
        pooled_eligible.actor_games >=
            kMinimumEligibleActorGames &&
        pooled_eligible.physical_games >=
            kMinimumEligiblePhysicalGames;
    for (const ScopeMetrics& deck : report.metrics.by_deck) {
        report.eligible_coverage_passed =
            report.eligible_coverage_passed &&
            deck.ro4_eligible.available &&
            deck.ro4_eligible.actor_games >=
                kMinimumEligibleActorGamesPerDeck;
    }
    const auto& early_green =
        report.metrics.by_deck[
            static_cast<std::size_t>(DeckId::Green)]
            .eligible_by_root_turn[
                stratum_index(RootTurnStratum::Early)];
    report.eligible_coverage_passed =
        report.eligible_coverage_passed &&
        early_green.available &&
        early_green.records >=
            kMinimumEarlyGreenEligibleRecords &&
        early_green.actor_games >=
            kMinimumEarlyGreenEligibleActorGames;
    report.early_green_control_qualified =
        early_green.available &&
        early_green.metrics.signed_bias.control.mean > 0.0 &&
        early_green.metrics.signed_bias.control
                .confidence_lower_95 >
            0.0;

    report.kish_qualification_passed =
        report.metrics.pooled.all_records.available &&
        report.metrics.pooled.all_records.weights
                .kish_effective_sample_size >=
            0.5 *
                static_cast<double>(
                    report.metrics.pooled.all_records.records);
    for (const ScopeMetrics& deck : report.metrics.by_deck) {
        report.kish_qualification_passed =
            report.kish_qualification_passed &&
            deck.all_records.available &&
            deck.all_records.weights
                    .kish_effective_sample_size >=
                0.4 *
                    static_cast<double>(
                        deck.all_records.records);
    }
    report.trace_invariants_passed =
        capture.trace_invariants_passed &&
        capture.rootless_actor_games == 0;
    report.ro4_identity_passed =
        capture.ro4_identity_passed;
    report.terminal_tail_identity_passed =
        capture.terminal_tail_identity_passed;
    report.hidden_repartition_passed =
        capture.hidden_repartition_passed;
    report.hidden_grouping_identity_passed =
        capture.hidden_grouping_identity_passed;
    report.hidden_target_hash_identity_passed =
        capture.hidden_target_hash_identity_passed;
    report.hidden_weight_identity_passed =
        capture.hidden_weight_identity_passed;
    report.hidden_scoring_hash_identity_passed =
        capture.hidden_scoring_hash_identity_passed;
    report.hidden_repartition_states =
        capture.hidden_repartition_states;
    report.weight_identity_passed =
        capture.weight_identity_passed;
    report.schedule_hash = capture.schedule_hash;
    report.trace_hash = capture.trace_hash;
    report.outcome_hash = capture.outcome_hash;
    report.feature_hash = capture.feature_hash;
    report.grouping_hash = capture.grouping_hash;
    report.ro4_target_hash = capture.ro4_target_hash;
    report.weight_hash = capture.weight_hash;
    report.scoring_hash = capture.scoring_hash;
    return report;
}

GateReport evaluate_gate(const ScientificReport& report) {
    GateReport gate;
    const auto& early_green =
        report.metrics.by_deck[
            static_cast<std::size_t>(DeckId::Green)]
            .eligible_by_root_turn[
                stratum_index(RootTurnStratum::Early)];
    if (early_green.available) {
        const auto& bias =
            early_green.metrics.signed_bias;
        gate.early_green_point_effect =
            bias.treatment_minus_control.mean <=
            kEarlyGreenEffect;
        gate.early_green_interval =
            bias.treatment_minus_control
                    .confidence_upper_95 <
                0.0;
        gate.early_green_absolute_bias_shrank =
            std::abs(bias.treatment.mean) <
            std::abs(bias.control.mean);
    }

    const auto& green =
        report.metrics.by_deck[
            static_cast<std::size_t>(DeckId::Green)];
    gate.whole_green_bias_shrank =
        green.all_records.available &&
        green.ro4_eligible.available &&
        std::abs(
            green.all_records.metrics.signed_bias
                .treatment.mean) <
            std::abs(
                green.all_records.metrics.signed_bias
                    .control.mean) &&
        std::abs(
            green.ro4_eligible.metrics.signed_bias
                .treatment.mean) <
            std::abs(
                green.ro4_eligible.metrics.signed_bias
                    .control.mean);

    const auto& pooled =
        report.metrics.pooled.all_records;
    gate.pooled_loss_noninferior =
        pooled.available &&
        pooled.metrics.brier.treatment_minus_control
                .confidence_upper_95 <
            kPooledLossUpper &&
        pooled.metrics.soft_log_loss
                .treatment_minus_control
                .confidence_upper_95 <
            kPooledLossUpper;

    gate.per_deck_bias_band = true;
    gate.per_deck_brier_guard = true;
    gate.per_deck_soft_log_guard = true;
    gate.no_new_material_bias = true;
    for (const ScopeMetrics& deck : report.metrics.by_deck) {
        for (const RowSetMetrics* rows : {
                 &deck.all_records, &deck.ro4_eligible}) {
            if (!rows->available) {
                gate.per_deck_bias_band = false;
                gate.per_deck_brier_guard = false;
                gate.per_deck_soft_log_guard = false;
                gate.no_new_material_bias = false;
                continue;
            }
            const auto& bias = rows->metrics.signed_bias;
            gate.per_deck_bias_band =
                gate.per_deck_bias_band &&
                std::abs(bias.treatment.mean) <=
                    std::max(
                        std::abs(bias.control.mean),
                        kBiasSafetyBand);
            const auto& brier =
                rows->metrics.brier
                    .treatment_minus_control;
            gate.per_deck_brier_guard =
                gate.per_deck_brier_guard &&
                brier.mean <= kDeckLossPoint &&
                brier.confidence_upper_95 <
                    kDeckLossUpper;
            const auto& log_loss =
                rows->metrics.soft_log_loss
                    .treatment_minus_control;
            gate.per_deck_soft_log_guard =
                gate.per_deck_soft_log_guard &&
                log_loss.mean <= kDeckLossPoint &&
                log_loss.confidence_upper_95 <
                    kDeckLossUpper;
            const bool treatment_material =
                has_material_bias(bias.treatment);
            const bool inherited =
                has_material_bias(bias.control) &&
                same_sign(
                    bias.treatment.mean,
                    bias.control.mean);
            if (treatment_material && !inherited) {
                gate.no_new_material_bias = false;
            }
        }
    }

    gate.evidence_complete =
        report.eligible_coverage_passed &&
        report.early_green_control_qualified &&
        report.kish_qualification_passed;
    gate.mechanical_invariants =
        report.schedule_balanced &&
        report.trace_invariants_passed &&
        report.ro4_identity_passed &&
        report.terminal_tail_identity_passed &&
        report.hidden_repartition_passed &&
        report.hidden_grouping_identity_passed &&
        report.hidden_target_hash_identity_passed &&
        report.hidden_weight_identity_passed &&
        report.hidden_scoring_hash_identity_passed &&
        report.hidden_repartition_states > 0 &&
        report.weight_identity_passed;
    gate.passed =
        gate.early_green_point_effect &&
        gate.early_green_interval &&
        gate.early_green_absolute_bias_shrank &&
        gate.whole_green_bias_shrank &&
        gate.pooled_loss_noninferior &&
        gate.per_deck_bias_band &&
        gate.per_deck_brier_guard &&
        gate.per_deck_soft_log_guard &&
        gate.no_new_material_bias &&
        gate.evidence_complete &&
        gate.mechanical_invariants;

    const auto failure =
        [&](bool condition, std::string message) {
            if (!condition) {
                gate.failures.push_back(std::move(message));
            }
        };
    failure(
        gate.early_green_point_effect,
        "early-Green eligible bias delta was not <= -0.005");
    failure(
        gate.early_green_interval,
        "early-Green eligible bias upper95 was not below zero");
    failure(
        gate.early_green_absolute_bias_shrank,
        "early-Green eligible absolute bias did not shrink");
    failure(
        gate.whole_green_bias_shrank,
        "whole-Green absolute bias did not shrink on all and "
        "eligible rows");
    failure(
        gate.pooled_loss_noninferior,
        "pooled all-row Brier or soft-log upper95 was not below "
        "+0.001");
    failure(
        gate.per_deck_bias_band,
        "a deck/scope treatment bias exceeded its safety band");
    failure(
        gate.per_deck_brier_guard,
        "a deck/scope Brier delta exceeded its point or interval "
        "guard");
    failure(
        gate.per_deck_soft_log_guard,
        "a deck/scope soft-log delta exceeded its point or "
        "interval guard");
    failure(
        gate.no_new_material_bias,
        "RB0 created a new material deck/scope bias");
    failure(
        gate.evidence_complete,
        "RB0 coverage, positive-control, or ESS qualification "
        "failed");
    failure(
        gate.mechanical_invariants,
        "RB0 mechanical invariants did not all pass");
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
           report.reversed_input_bit_identical &&
           report.worker_reduction_bit_identical &&
           report.parent_fingerprint == kParentFingerprint &&
           report.scientific.schedule_balanced &&
           report.scientific.trace_invariants_passed &&
           report.scientific.ro4_identity_passed &&
           report.scientific
               .terminal_tail_identity_passed &&
           report.scientific.hidden_repartition_passed &&
           report.scientific
               .hidden_grouping_identity_passed &&
           report.scientific
               .hidden_target_hash_identity_passed &&
           report.scientific
               .hidden_weight_identity_passed &&
           report.scientific
               .hidden_scoring_hash_identity_passed &&
           report.scientific.hidden_repartition_states > 0 &&
           report.scientific.weight_identity_passed &&
           report.gate.mechanical_invariants;
}

AuditReport run_canonical_rb0_audit(std::ostream& progress) {
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
                "RB0 frozen C16 artifact content, size, or "
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
                "RB0 frozen C16 fingerprint mismatch: expected " +
                std::string(kParentFingerprint) + ", got " +
                report.parent_fingerprint);
        }
        report.parent_components =
            learned_model_component_fingerprints(parent);
        if (learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
            throw std::runtime_error(
                "RB0 parent uses the wrong critic schema");
        }
        if (require_artifact_snapshot(artifact_path) !=
            report.artifact_before) {
            throw std::runtime_error(
                "RB0 frozen C16 artifact changed while loading");
        }
        progress << " done\n";

        const std::vector<AuditTask> tasks =
            audit_schedule(
                kAuditSeed, kAuditGeneration,
                kAuditBalancedBlocks);
        static_assert(kAuditWorkerCount >= 2);
        const CaptureConfig parallel_config = {
            .max_game_turns = 500,
            .worker_count = kAuditWorkerCount,
            .verify_hidden_repartition = true,
            .schedule_seed = kAuditSeed,
            .schedule_generation = kAuditGeneration,
        };
        progress
            << "Constructing RB0-0 audit corpus four times "
               "(2,400 games each: repeat, reverse, and fixed "
               "1-vs-4 workers; K=1/H=4)..."
            << std::flush;
        const Capture first =
            collect(tasks, parent, parallel_config);
        if (require_artifact_snapshot(artifact_path) !=
            report.artifact_before) {
            throw std::runtime_error(
                "RB0 frozen C16 artifact changed after first "
                "collection");
        }
        const Capture repeated =
            collect(tasks, parent, parallel_config);
        if (require_artifact_snapshot(artifact_path) !=
            report.artifact_before) {
            throw std::runtime_error(
                "RB0 frozen C16 artifact changed after repeated "
                "collection");
        }
        std::vector<AuditTask> reversed = tasks;
        std::reverse(reversed.begin(), reversed.end());
        const Capture reversed_capture =
            collect(
                reversed, parent, parallel_config);
        if (require_artifact_snapshot(artifact_path) !=
            report.artifact_before) {
            throw std::runtime_error(
                "RB0 frozen C16 artifact changed after reversed "
                "collection");
        }
        const Capture single_worker =
            collect(
                tasks, parent,
                {
                    .max_game_turns = 500,
                    .worker_count = 1,
                    .verify_hidden_repartition = true,
                    .schedule_seed = kAuditSeed,
                    .schedule_generation =
                        kAuditGeneration,
                });
        report.scientific =
            make_scientific_report(
                first, kAuditSeed, kAuditGeneration,
                kAuditBalancedBlocks);
        const ScientificReport repeated_report =
            make_scientific_report(
                repeated, kAuditSeed, kAuditGeneration,
                kAuditBalancedBlocks);
        const ScientificReport reversed_report =
            make_scientific_report(
                reversed_capture, kAuditSeed,
                kAuditGeneration,
                kAuditBalancedBlocks);
        const ScientificReport single_worker_report =
            make_scientific_report(
                single_worker, kAuditSeed,
                kAuditGeneration,
                kAuditBalancedBlocks);
        report.repeated_report_bit_identical =
            first == repeated &&
            report.scientific == repeated_report;
        report.reversed_input_bit_identical =
            first == reversed_capture &&
            report.scientific == reversed_report;
        report.worker_reduction_bit_identical =
            first == single_worker &&
            report.scientific ==
                single_worker_report;
        progress << " done ("
                 << first.records.size()
                 << " trace-perspective rows per capture)\n";

        verify_snapshot();
        report.gate = evaluate_gate(report.scientific);
        if (!infrastructure_complete(report)) {
            throw std::runtime_error(
                "RB0 audit mechanics, hidden-zone, artifact, "
                "or deterministic-reduction checks were "
                "incomplete");
        }
        // Coverage, positive-control, and ESS qualification are
        // scientific gates. Their failure is a complete rejection
        // (exit 1), not an infrastructure failure.
        report.passed = report.gate.passed;
        return report;
    } catch (...) {
        if (!snapshot_verified) {
            verify_snapshot();
        }
        throw;
    }
}

namespace {

void write_distribution(
    std::ostream& output, std::string_view label,
    const DistributionSummary& distribution) {
    output << "    " << label
           << ": n=" << distribution.count
           << ", min=" << distribution.minimum
           << ", q25=" << distribution.q25
           << ", median=" << distribution.median
           << ", q75=" << distribution.q75
           << ", max=" << distribution.maximum
           << ", mean=" << distribution.mean << '\n';
}

void write_estimate(
    std::ostream& output, std::string_view label,
    const ClusteredEstimate& estimate) {
    output << "      " << label
           << ": " << estimate.mean
           << " (SE " << estimate.standard_error
           << ", 95% [" << estimate.confidence_lower_95
           << ", " << estimate.confidence_upper_95 << "])\n";
}

void write_metric_comparison(
    std::ostream& output, std::string_view label,
    const MetricComparison& metric) {
    output << "    " << label << '\n';
    write_estimate(output, "control", metric.control);
    write_estimate(output, "treatment", metric.treatment);
    write_estimate(
        output, "treatment-control",
        metric.treatment_minus_control);
}

void write_row_set(
    std::ostream& output, std::string_view label,
    const RowSetMetrics& rows) {
    output << "  " << label;
    if (!rows.available) {
        output << ": unavailable\n";
        return;
    }
    output << ": rows=" << rows.records
           << ", actor-games=" << rows.actor_games
           << ", physical-games=" << rows.physical_games
           << ", bootstrapped=" << rows.bootstrapped_records
           << ", terminal-tail=" << rows.terminal_tail_records
           << ", weight-sum=" << rows.weights.total_weight
           << ", Kish-ESS="
           << rows.weights.kish_effective_sample_size
           << ", post-run approximate detectable effect "
              "(2.802*SE, normal)="
           << rows.achieved_mde_95_80 << '\n';
    write_metric_comparison(
        output, "signed bias",
        rows.metrics.signed_bias);
    write_metric_comparison(
        output, "Brier", rows.metrics.brier);
    write_metric_comparison(
        output, "soft log loss",
        rows.metrics.soft_log_loss);
    write_distribution(
        output, "weights", rows.weights.weights);
    write_distribution(
        output, "terminal targets",
        rows.terminal_targets);
    write_distribution(
        output, "RO4 targets", rows.ro4_targets);
}

void write_scope(
    std::ostream& output, std::string_view label,
    const ScopeMetrics& scope) {
    output << "\n" << label << '\n';
    write_row_set(output, "all", scope.all_records);
    write_row_set(
        output, "RO4 eligible", scope.ro4_eligible);
    static constexpr std::array<std::string_view, 3>
        strata = {"turn<=3", "turn4-7", "turn>=8"};
    for (std::size_t index = 0;
         index < strata.size(); ++index) {
        write_row_set(
            output,
            std::string("all/") +
                std::string(strata[index]),
            scope.all_by_root_turn[index]);
        write_row_set(
            output,
            std::string("eligible/") +
                std::string(strata[index]),
            scope.eligible_by_root_turn[index]);
    }
}

void write_tsv_estimate(
    std::ostream& output, std::string_view scope,
    std::string_view row_set, std::string_view metric,
    std::string_view arm,
    const ClusteredEstimate& estimate) {
    output << "metric\t" << scope << '\t' << row_set << '\t'
           << metric << '\t' << arm << '\t'
           << estimate.records << '\t' << estimate.clusters
           << '\t' << estimate.mean << '\t'
           << estimate.standard_error << '\t'
           << estimate.confidence_lower_95 << '\t'
           << estimate.confidence_upper_95 << '\n';
}

void write_tsv_rows(
    std::ostream& output, std::string_view scope,
    std::string_view row_set, const RowSetMetrics& rows) {
    if (!rows.available) {
        output << "scope\t" << scope << '\t' << row_set
               << "\tunavailable\n";
        return;
    }
    output << "scope\t" << scope << '\t' << row_set
           << "\trows=" << rows.records
           << ";actor_games=" << rows.actor_games
           << ";physical_games=" << rows.physical_games
           << ";bootstrapped=" << rows.bootstrapped_records
           << ";terminal_tail=" << rows.terminal_tail_records
           << ";weight_sum=" << rows.weights.total_weight
           << ";kish_ess="
           << rows.weights.kish_effective_sample_size
           << ";post_run_approx_detectable_effect_2.802se_normal="
           << rows.achieved_mde_95_80
           << '\n';
    const auto write_comparison =
        [&](std::string_view metric,
            const MetricComparison& comparison) {
            write_tsv_estimate(
                output, scope, row_set, metric, "control",
                comparison.control);
            write_tsv_estimate(
                output, scope, row_set, metric, "treatment",
                comparison.treatment);
            write_tsv_estimate(
                output, scope, row_set, metric, "delta",
                comparison.treatment_minus_control);
        };
    write_comparison("signed_bias", rows.metrics.signed_bias);
    write_comparison("brier", rows.metrics.brier);
    write_comparison(
        "soft_log_loss", rows.metrics.soft_log_loss);
}

void write_tsv_scope(
    std::ostream& output, std::string_view scope_name,
    const ScopeMetrics& scope) {
    write_tsv_rows(
        output, scope_name, "all", scope.all_records);
    write_tsv_rows(
        output, scope_name, "eligible",
        scope.ro4_eligible);
    static constexpr std::array<std::string_view, 3>
        strata = {"early", "middle", "late"};
    for (std::size_t index = 0;
         index < strata.size(); ++index) {
        write_tsv_rows(
            output, scope_name,
            std::string("all_") +
                std::string(strata[index]),
            scope.all_by_root_turn[index]);
        write_tsv_rows(
            output, scope_name,
            std::string("eligible_") +
                std::string(strata[index]),
            scope.eligible_by_root_turn[index]);
    }
}

} // namespace

void write_human_report(
    const AuditReport& report, std::ostream& output) {
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(6);
    output << "RB0-0 actor-game/calendar-turn replay-weight "
              "census\n"
           << "  parent: " << report.parent_fingerprint << '\n'
           << "  seed/generation/blocks: "
           << report.scientific.seed << '/'
           << report.scientific.generation << '/'
           << report.scientific.balanced_blocks << '\n'
           << "  games/actor-games/rows: "
           << report.scientific.physical_games << '/'
           << report.scientific.actor_games << '/'
           << report.scientific.weights.records << '\n'
           << "  weight mass: actual="
           << report.scientific.weights.total_weight
           << ", expected="
           << report.scientific.weights
                  .expected_total_weight
           << ", actor expected="
           << report.scientific.weights
                  .expected_actor_weight
           << ", max errors global/actor/turn="
           << report.scientific.weights
                  .maximum_global_mass_error
           << '/'
           << report.scientific.weights
                  .maximum_actor_mass_error
           << '/'
           << report.scientific.weights
                  .maximum_turn_mass_error
           << '\n'
           << "  hashes:\n"
           << "    schedule " << report.scientific.schedule_hash
           << "\n    trace " << report.scientific.trace_hash
           << "\n    outcome " << report.scientific.outcome_hash
           << "\n    feature " << report.scientific.feature_hash
           << "\n    grouping "
           << report.scientific.grouping_hash
           << "\n    RO4 " << report.scientific.ro4_target_hash
           << "\n    weight " << report.scientific.weight_hash
           << "\n    scoring " << report.scientific.scoring_hash
           << '\n';
    write_distribution(
        output, "global weights",
        report.scientific.weights.weights);
    write_distribution(
        output, "actor-game record counts",
        report.scientific.weights.actor_record_counts);
    write_distribution(
        output, "actor-game distinct-turn counts",
        report.scientific.weights.actor_turn_counts);
    write_distribution(
        output, "within-turn record multiplicities",
        report.scientific.weights
            .turn_record_multiplicities);

    write_scope(output, "Pooled", report.scientific.metrics.pooled);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        write_scope(
            output,
            audit_deck_name(static_cast<DeckId>(deck)),
            report.scientific.metrics.by_deck[deck]);
    }

    output << "\nQualifications\n"
           << "  schedule: "
           << bool_text(report.scientific.schedule_balanced)
           << "\n  eligible coverage: "
           << bool_text(
                  report.scientific
                      .eligible_coverage_passed)
           << "\n  early-Green positive control: "
           << bool_text(
                  report.scientific
                      .early_green_control_qualified)
           << "\n  Kish ESS: "
           << bool_text(
                  report.scientific
                      .kish_qualification_passed)
           << "\n  hidden repartition: "
           << bool_text(
                  report.scientific
                      .hidden_repartition_passed)
           << " (changed states "
           << report.scientific.hidden_repartition_states
           << ")\n  hidden grouping/target/weight/scoring hashes: "
           << bool_text(
                  report.scientific
                      .hidden_grouping_identity_passed)
           << '/'
           << bool_text(
                  report.scientific
                      .hidden_target_hash_identity_passed)
           << '/'
           << bool_text(
                  report.scientific
                      .hidden_weight_identity_passed)
           << '/'
           << bool_text(
                  report.scientific
                      .hidden_scoring_hash_identity_passed)
           << "\n  weight identities: "
           << bool_text(
                  report.scientific
                      .weight_identity_passed)
           << "\n  repeated/reversed/workers: "
           << bool_text(
                  report.repeated_report_bit_identical)
           << '/'
           << bool_text(
                  report.reversed_input_bit_identical)
           << '/'
           << bool_text(
                  report.worker_reduction_bit_identical)
           << "\n  artifact unchanged: "
           << bool_text(report.artifact_unchanged)
           << "\n\nGates\n"
           << "  early-Green point/interval/shrink: "
           << bool_text(
                  report.gate.early_green_point_effect)
           << '/'
           << bool_text(report.gate.early_green_interval)
           << '/'
           << bool_text(
                  report.gate
                      .early_green_absolute_bias_shrank)
           << "\n  whole-Green shrink: "
           << bool_text(
                  report.gate.whole_green_bias_shrank)
           << "\n  pooled loss: "
           << bool_text(
                  report.gate.pooled_loss_noninferior)
           << "\n  per-deck bias/Brier/log-loss: "
           << bool_text(report.gate.per_deck_bias_band)
           << '/'
           << bool_text(
                  report.gate.per_deck_brier_guard)
           << '/'
           << bool_text(
                  report.gate.per_deck_soft_log_guard)
           << "\n  no new material bias: "
           << bool_text(report.gate.no_new_material_bias)
           << "\n  evidence/mechanics: "
           << bool_text(report.gate.evidence_complete)
           << '/'
           << bool_text(report.gate.mechanical_invariants)
           << "\n\nRB0-0 verdict: "
           << (report.passed ? "PASS" : "REJECT") << '\n';
    for (const std::string& failure : report.gate.failures) {
        output << "  - " << failure << '\n';
    }
}

void write_tsv_report(
    const AuditReport& report, std::ostream& output) {
    output.imbue(std::locale::classic());
    output << std::setprecision(17);
    output << "RB0_TSV_BEGIN\n"
           << "meta\tseed\t" << report.scientific.seed << '\n'
           << "meta\tgeneration\t"
           << report.scientific.generation << '\n'
           << "meta\tbalanced_blocks\t"
           << report.scientific.balanced_blocks << '\n'
           << "meta\tphysical_games\t"
           << report.scientific.physical_games << '\n'
           << "meta\tactor_games\t"
           << report.scientific.actor_games << '\n'
           << "meta\tparent_fingerprint\t"
           << report.parent_fingerprint << '\n'
           << "hash\tschedule\t"
           << report.scientific.schedule_hash << '\n'
           << "hash\ttrace\t"
           << report.scientific.trace_hash << '\n'
           << "hash\toutcome\t"
           << report.scientific.outcome_hash << '\n'
           << "hash\tfeature\t"
           << report.scientific.feature_hash << '\n'
           << "hash\tgrouping\t"
           << report.scientific.grouping_hash << '\n'
           << "hash\tro4\t"
           << report.scientific.ro4_target_hash << '\n'
           << "hash\tweight\t"
           << report.scientific.weight_hash << '\n'
           << "hash\tscoring\t"
           << report.scientific.scoring_hash << '\n'
           << "weights\tglobal\trows="
           << report.scientific.weights.records
           << ";actors="
           << report.scientific.weights.actor_games
           << ";actor_turns="
           << report.scientific.weights.actor_turns
           << ";sum="
           << report.scientific.weights.total_weight
           << ";expected="
           << report.scientific.weights
                  .expected_total_weight
           << ";kish_ess="
           << report.scientific.weights
                  .kish_effective_sample_size
           << ";min="
           << report.scientific.weights.weights.minimum
           << ";q25=" << report.scientific.weights.weights.q25
           << ";median="
           << report.scientific.weights.weights.median
           << ";q75=" << report.scientific.weights.weights.q75
           << ";max="
           << report.scientific.weights.weights.maximum
           << '\n';
    for (std::size_t first = 0;
         first < kDeckCount; ++first) {
        for (std::size_t second = 0;
             second < kDeckCount; ++second) {
            output << "schedule\tordered_pair\t"
                   << audit_deck_name(
                          static_cast<DeckId>(first))
                   << '\t'
                   << audit_deck_name(
                          static_cast<DeckId>(second))
                   << '\t'
                   << report.scientific
                          .ordered_pair_counts[first][second]
                   << '\n';
        }
    }
    output << "mechanical\thidden_grouping\t"
           << bool_text(
                  report.scientific
                      .hidden_grouping_identity_passed)
           << "\nmechanical\thidden_target_hash\t"
           << bool_text(
                  report.scientific
                      .hidden_target_hash_identity_passed)
           << "\nmechanical\thidden_weight\t"
           << bool_text(
                  report.scientific
                      .hidden_weight_identity_passed)
           << "\nmechanical\thidden_scoring_hash\t"
           << bool_text(
                  report.scientific
                      .hidden_scoring_hash_identity_passed)
           << '\n';
    write_tsv_scope(
        output, "pooled", report.scientific.metrics.pooled);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        write_tsv_scope(
            output,
            audit_deck_name(static_cast<DeckId>(deck)),
            report.scientific.metrics.by_deck[deck]);
    }
    const auto gate =
        [&](std::string_view name, bool passed) {
            output << "gate\t" << name << '\t'
                   << (passed ? "PASS" : "FAIL") << '\n';
        };
    gate(
        "early_green_point_effect",
        report.gate.early_green_point_effect);
    gate(
        "early_green_interval",
        report.gate.early_green_interval);
    gate(
        "early_green_absolute_bias_shrank",
        report.gate.early_green_absolute_bias_shrank);
    gate(
        "whole_green_bias_shrank",
        report.gate.whole_green_bias_shrank);
    gate(
        "pooled_loss_noninferior",
        report.gate.pooled_loss_noninferior);
    gate(
        "per_deck_bias_band",
        report.gate.per_deck_bias_band);
    gate(
        "per_deck_brier_guard",
        report.gate.per_deck_brier_guard);
    gate(
        "per_deck_soft_log_guard",
        report.gate.per_deck_soft_log_guard);
    gate(
        "no_new_material_bias",
        report.gate.no_new_material_bias);
    gate("evidence_complete", report.gate.evidence_complete);
    gate(
        "mechanical_invariants",
        report.gate.mechanical_invariants);
    gate("verdict", report.passed);
    output << "RB0_TSV_END\n";
}

} // namespace old_school::replay_weight_audit
