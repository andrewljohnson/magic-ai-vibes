#include "old_school/terminal_weight_eval.hpp"
#include "old_school/audit_common.hpp"
#include "old_school/probe_runner.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace old_school::terminal_weight_eval {
namespace {

constexpr std::size_t kParentIndex =
    static_cast<std::size_t>(CriticModel::ParentC16);
constexpr std::size_t kControlIndex =
    static_cast<std::size_t>(CriticModel::TW50);
constexpr std::size_t kTreatmentIndex =
    static_cast<std::size_t>(CriticModel::TW75);
constexpr double kDeckLossGuard = 0.01;
constexpr double kMaterialBias = 0.05;

using audit_common::ContentHash;
using audit_common::format_real;
using audit_common::make_tsv_estimate_writer;
using audit_common::require_probability;
using audit_common::same_strict_sign;
using audit_common::sanitize_tsv;
using audit_common::soft_log_loss;

std::size_t deck_index(DeckId deck) {
    const std::size_t index = static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::invalid_argument(
            "terminal-weight evaluation deck is out of range");
    }
    return index;
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
    throw std::invalid_argument(
        "terminal-weight evaluation deck is out of range");
}

std::string_view critic_model_name(std::size_t model) {
    switch (static_cast<CriticModel>(model)) {
    case CriticModel::ParentC16:
        return "C16";
    case CriticModel::TW50:
        return "TW50-C17";
    case CriticModel::TW75:
        return "TW75-C17";
    }
    throw std::logic_error(
        "terminal-weight critic model index is invalid");
}

ArtifactSnapshot snapshot_metadata(const std::string& path) {
    std::error_code error;
    const std::uintmax_t size =
        std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error(
            "cannot inspect evaluation artifact '" + path +
            "': " + error.message());
    }
    const auto time =
        std::filesystem::last_write_time(path, error);
    if (error) {
        throw std::runtime_error(
            "cannot inspect evaluation artifact timestamp '" +
            path + "': " + error.message());
    }
    return {
        .path = path,
        .size = size,
        .modification_time_ticks =
            static_cast<std::int64_t>(
                time.time_since_epoch().count()),
    };
}

std::vector<const HoldoutRecord*> select_records(
    std::span<const HoldoutRecord> records,
    std::optional<DeckId> deck) {
    std::vector<const HoldoutRecord*> selected;
    selected.reserve(records.size());
    for (const HoldoutRecord& record : records) {
        deck_index(record.deck);
        require_probability(
            record.discounted_terminal_target,
            "holdout target");
        for (const double prediction : record.predictions) {
            require_probability(
                prediction, "holdout prediction");
        }
        if (!deck.has_value() || record.deck == *deck) {
            selected.push_back(&record);
        }
    }
    if (selected.empty()) {
        throw std::invalid_argument(
            "holdout metric scope must contain records");
    }
    return selected;
}

template <typename Function>
ClusteredEstimate estimate_records(
    const std::vector<const HoldoutRecord*>& records,
    Function metric) {
    std::vector<ClusteredValue> values;
    values.reserve(records.size());
    for (const HoldoutRecord* record : records) {
        values.push_back({
            .cluster = record->physical_game,
            .value = metric(*record),
        });
    }
    return cr1_clustered_estimate(values);
}

HoldoutScopeMetrics score_scope(
    const std::vector<const HoldoutRecord*>& records) {
    HoldoutScopeMetrics result;
    result.records = records.size();

    std::set<std::size_t> games;
    std::set<std::pair<std::size_t, std::size_t>>
        perspectives;
    long double target_sum = 0.0L;
    long double target_squared_sum = 0.0L;
    long double distance_sum = 0.0L;
    bool has_distance = false;
    for (const HoldoutRecord* record : records) {
        games.insert(record->physical_game);
        perspectives.emplace(
            record->physical_game, deck_index(record->deck));
        target_sum += record->discounted_terminal_target;
        target_squared_sum +=
            static_cast<long double>(
                record->discounted_terminal_target) *
            record->discounted_terminal_target;
        if (record->trace_turn_distance.has_value()) {
            const std::size_t distance =
                *record->trace_turn_distance;
            distance_sum += distance;
            result.minimum_trace_turn_distance =
                has_distance
                    ? std::min(
                          result.minimum_trace_turn_distance,
                          distance)
                    : distance;
            result.maximum_trace_turn_distance =
                has_distance
                    ? std::max(
                          result.maximum_trace_turn_distance,
                          distance)
                    : distance;
            has_distance = true;
            ++result.bootstrapped_records;
        } else {
            ++result.terminal_tail_records;
        }
    }
    result.physical_games = games.size();
    result.perspectives = perspectives.size();
    result.target_mean = static_cast<double>(
        target_sum /
        static_cast<long double>(result.records));
    result.target_variance = std::max(
        0.0,
        static_cast<double>(
            target_squared_sum /
            static_cast<long double>(result.records) -
            static_cast<long double>(result.target_mean) *
                result.target_mean));
    if (result.bootstrapped_records != 0) {
        result.mean_trace_turn_distance =
            static_cast<double>(
                distance_sum /
                static_cast<long double>(
                    result.bootstrapped_records));
    }

    for (std::size_t model = 0;
         model < kCriticModelCount; ++model) {
        auto& metrics = result.models[model];
        metrics.brier = estimate_records(
            records, [model](const HoldoutRecord& record) {
                const double error =
                    record.predictions[model] -
                    record.discounted_terminal_target;
                return error * error;
            });
        metrics.soft_log_loss = estimate_records(
            records, [model](const HoldoutRecord& record) {
                return soft_log_loss(
                    record.predictions[model],
                    record.discounted_terminal_target);
            });
        metrics.signed_bias = estimate_records(
            records, [model](const HoldoutRecord& record) {
                return record.predictions[model] -
                       record.discounted_terminal_target;
            });
        long double prediction_sum = 0.0L;
        for (const HoldoutRecord* record : records) {
            const double prediction =
                record->predictions[model];
            prediction_sum += prediction;
            if (prediction <= 0.01 || prediction >= 0.99) {
                ++metrics.saturated_predictions;
            }
        }
        metrics.prediction_mean = static_cast<double>(
            prediction_sum /
            static_cast<long double>(records.size()));
        metrics.saturation_fraction =
            static_cast<double>(
                metrics.saturated_predictions) /
            static_cast<double>(records.size());
    }

    const std::array<std::size_t, 2> controls = {
        kControlIndex,
        kParentIndex,
    };
    for (std::size_t comparison = 0;
         comparison < controls.size(); ++comparison) {
        const std::size_t control = controls[comparison];
        auto& metrics =
            result.treatment_comparisons[comparison];
        metrics.brier_delta = estimate_records(
            records,
            [control](const HoldoutRecord& record) {
                const double treatment_error =
                    record.predictions[kTreatmentIndex] -
                    record.discounted_terminal_target;
                const double control_error =
                    record.predictions[control] -
                    record.discounted_terminal_target;
                return treatment_error * treatment_error -
                       control_error * control_error;
            });
        metrics.soft_log_loss_delta = estimate_records(
            records,
            [control](const HoldoutRecord& record) {
                const double target =
                    record.discounted_terminal_target;
                const auto loss =
                    [target](double raw_prediction) {
                        return soft_log_loss(
                            raw_prediction, target);
                    };
                return loss(
                           record.predictions[
                               kTreatmentIndex]) -
                       loss(record.predictions[control]);
            });
    }
    return result;
}

bool material_bias(const ClusteredEstimate& bias) {
    return std::abs(bias.mean) >= kMaterialBias &&
           (bias.confidence_lower_95 > 0.0 ||
            bias.confidence_upper_95 < 0.0);
}

bool same_sign_material_bias(
    const ClusteredEstimate& treatment,
    const ClusteredEstimate& control) {
    return material_bias(control) &&
           same_strict_sign(treatment.mean, control.mean);
}

void append_failure(
    bool condition, std::string message,
    std::vector<std::string>& failures) {
    if (!condition) {
        failures.push_back(std::move(message));
    }
}

struct HoldoutGame {
    std::vector<HoldoutRecord> records;
};

std::vector<HoldoutRecord> collect_holdout_impl(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> control,
    std::shared_ptr<const LearnedModel> treatment,
    const HoldoutCollectionConfig& collection,
    std::ostream& progress) {
    const auto schedule = holdout_schedule(
        collection.seed, collection.generation,
        collection.balanced_blocks);
    std::vector<HoldoutGame> games(schedule.size());
    std::vector<std::exception_ptr> errors(schedule.size());
    std::atomic_size_t next_game = 0;
    const std::size_t worker_count =
        std::min<std::size_t>(
            schedule.size(),
            std::max(
                1U, std::thread::hardware_concurrency()));

    progress
        << "Running HOLD1: " << schedule.size()
        << " frozen-parent mirror games "
           "(K=1/H=4, epsilon=0.05)..."
        << std::flush;
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count;
         ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t index =
                    next_game.fetch_add(
                        1, std::memory_order_relaxed);
                if (index >= schedule.size()) {
                    return;
                }
                try {
                    const HoldoutTask& task = schedule[index];
                    GameConfig config;
                    config.max_turns =
                        collection.max_game_turns;
                    config.starting_player =
                        task.scheduled.starting_player;
                    config.learned_model = parent;
                    config.learned_search_depth = 1;
                    const BotConfig pilot = {
                        .kind = BotKind::Learned,
                        .learned_variant =
                            LearnedVariant::
                                ValueSearchChampion,
                        .rollouts_per_action = 1,
                        .exploration_rate = 0.05,
                        .value_continuation_epsilon = 0.0,
                        .value_priority_residual_weight = 0.0,
                        .training_games =
                            collection.pilot_training_games,
                        .learned_model = parent,
                    };
                    config.bots = {pilot, pilot};
                    Game game(
                        cards_for_deck(
                            task.scheduled.seat_decks[0]),
                        cards_for_deck(
                            task.scheduled.seat_decks[1]),
                        task.scheduled.seed, config);
                    std::vector<GameState> trace;
                    const GameResult result =
                        game.run_with_trace(trace);
                    if (trace.empty()) {
                        throw std::runtime_error(
                            "HOLD1 game produced an empty trace");
                    }
                    if (result.starting_player !=
                        task.scheduled.starting_player) {
                        throw std::runtime_error(
                            "HOLD1 starting-player schedule "
                            "mismatch");
                    }
                    auto& records = games[index].records;
                    records.reserve(2 * trace.size());
                    for (std::size_t trace_index = 0;
                         trace_index < trace.size();
                         ++trace_index) {
                        std::optional<std::size_t> distance;
                        constexpr std::size_t
                            kBootstrapDistance = 4;
                        if (trace_index + kBootstrapDistance <
                            trace.size()) {
                            const std::size_t first_turn =
                                trace[trace_index].turn_number;
                            const std::size_t future_turn =
                                trace[trace_index +
                                      kBootstrapDistance]
                                    .turn_number;
                            if (future_turn < first_turn) {
                                throw std::runtime_error(
                                    "HOLD1 trace turn order "
                                    "regressed");
                            }
                            distance =
                                future_turn - first_turn;
                        }
                        for (std::size_t perspective = 0;
                             perspective < 2;
                             ++perspective) {
                            records.push_back({
                                .physical_game =
                                    task.physical_game,
                                .deck =
                                    task.scheduled
                                        .seat_decks[perspective],
                                .discounted_terminal_target =
                                    learned_discounted_terminal_target(
                                        result, perspective),
                                .predictions = {
                                    learned_critic_value(
                                        trace[trace_index],
                                        perspective, parent),
                                    learned_critic_value(
                                        trace[trace_index],
                                        perspective, control),
                                    learned_critic_value(
                                        trace[trace_index],
                                        perspective, treatment),
                                },
                                .trace_turn_distance = distance,
                            });
                        }
                    }
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

    std::vector<HoldoutRecord> records;
    std::size_t total_records = 0;
    for (const HoldoutGame& game : games) {
        total_records += game.records.size();
    }
    records.reserve(total_records);
    for (auto& game : games) {
        records.insert(
            records.end(),
            std::make_move_iterator(game.records.begin()),
            std::make_move_iterator(game.records.end()));
    }
    progress << " done (" << records.size()
             << " trace-perspective records)\n";
    return records;
}

GameplayPanelReport run_gameplay_panel(
    std::shared_ptr<const LearnedModel> challenger,
    std::shared_ptr<const LearnedModel> baseline,
    std::string_view label, std::ostream& progress) {
    const auto schedule = same_deck_gameplay_schedule(
        kTerminalWeightC17GameplaySeed,
        kGameplayQuartetsPerDeck);
    std::vector<GameplayOutcome> outcomes(schedule.size());
    std::vector<std::exception_ptr> errors(schedule.size());
    std::atomic_size_t next_game = 0;
    const std::size_t worker_count =
        std::min<std::size_t>(
            schedule.size(),
            std::max(
                1U, std::thread::hardware_concurrency()));

    progress << "Running " << label
             << ": 1,000 same-deck K=8/H=4 games..."
             << std::flush;
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count;
         ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t index =
                    next_game.fetch_add(
                        1, std::memory_order_relaxed);
                if (index >= schedule.size()) {
                    return;
                }
                try {
                    const GameplayTask& task =
                        schedule[index];
                    GameConfig config;
                    config.max_turns = 500;
                    config.starting_player =
                        task.starting_player;
                    config.learned_search_depth = 1;
                    const BotConfig challenger_bot = {
                        .kind = BotKind::Learned,
                        .learned_variant =
                            LearnedVariant::
                                ValueSearchChampion,
                        .rollouts_per_action = 8,
                        .exploration_rate = 0.0,
                        .value_continuation_epsilon = 0.0,
                        .value_priority_residual_weight = 0.0,
                        .training_games = 800,
                        .learned_model = challenger,
                    };
                    const BotConfig baseline_bot = {
                        .kind = BotKind::Learned,
                        .learned_variant =
                            LearnedVariant::
                                ValueSearchChampion,
                        .rollouts_per_action = 8,
                        .exploration_rate = 0.0,
                        .value_continuation_epsilon = 0.0,
                        .value_priority_residual_weight = 0.0,
                        .training_games = 800,
                        .learned_model = baseline,
                    };
                    config.bots[task.challenger_player] =
                        challenger_bot;
                    config.bots[1 - task.challenger_player] =
                        baseline_bot;
                    const auto deck = cards_for_deck(task.deck);
                    Game game(
                        deck, deck, task.seed, config);
                    const GameResult result = game.run();
                    if (result.starting_player !=
                        task.starting_player) {
                        throw std::runtime_error(
                            "gameplay starting-player schedule "
                            "mismatch");
                    }
                    outcomes[index] = {
                        .deck = task.deck,
                        .quartet = task.quartet,
                        .challenger_won =
                            result.winner ==
                            static_cast<int>(
                                task.challenger_player),
                        .baseline_won =
                            result.winner >= 0 &&
                            result.winner !=
                                static_cast<int>(
                                    task.challenger_player),
                    };
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
    progress << " done\n";
    return score_gameplay_outcomes(outcomes);
}

void require_canonical_artifacts(
    const LearnedValueChallengerArtifact& parent_artifact,
    const LearnedTerminalWeightC17Artifact& bundle) {
    const auto parent = parent_artifact.model();
    const auto& report = bundle.report();
    const std::string parent_fingerprint =
        learned_model_fingerprint(parent);
    const auto parent_components =
        learned_model_component_fingerprints(parent);
    const auto control_components =
        learned_model_component_fingerprints(
            bundle.control_model());
    const auto treatment_components =
        learned_model_component_fingerprints(
            bundle.treatment_model());
    const auto critic_only_delta =
        [](const LearnedModelComponentFingerprints& source,
           const LearnedModelComponentFingerprints& candidate) {
            return candidate.critic != source.critic &&
                   candidate.priority == source.priority &&
                   candidate.attack == source.attack &&
                   candidate.block == source.block &&
                   candidate.damage_order ==
                       source.damage_order;
        };
    if (parent_artifact.training_games() != 800 ||
        parent_artifact.seed() !=
            kDefaultLearnedTrainingSeed ||
        parent_artifact.self_play_generations() != 16 ||
        parent_fingerprint !=
            kTerminalWeightC17ParentFingerprint ||
        report.training_games != 800 ||
        report.parent_training_seed !=
            kDefaultLearnedTrainingSeed ||
        report.parent_generations != 16 ||
        report.shard_seed !=
            kTerminalWeightC17ShardSeed ||
        report.balanced_blocks != 5 ||
        report.scheduled_games !=
            kHoldoutPhysicalGames ||
        report.bootstrap_distance != 4 ||
        report.collection_search_worlds != 1 ||
        report.collection_horizon_turns != 4 ||
        report.collection_max_game_turns != 500 ||
        report.collection_exploration_rate != 0.05 ||
        report.control_terminal_weight != 0.50 ||
        report.treatment_terminal_weight != 0.75 ||
        report.fit_epochs != 3 ||
        report.fit_learning_rate != 0.006 ||
        report.parent_fingerprint != parent_fingerprint ||
        report.parent_components != parent_components ||
        report.control_components != control_components ||
        report.treatment_components !=
            treatment_components ||
        !critic_only_delta(
            parent_components, control_components) ||
        !critic_only_delta(
            parent_components, treatment_components) ||
        control_components.critic ==
            treatment_components.critic ||
        report.control_fingerprint !=
            learned_model_fingerprint(
                bundle.control_model()) ||
        report.treatment_fingerprint !=
            learned_model_fingerprint(
                bundle.treatment_model())) {
        throw std::runtime_error(
            "sealed terminal-weight evaluator artifact identity "
            "or component provenance mismatch");
    }
}

} // namespace

std::vector<HoldoutRecord> collect_holdout_records(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> control,
    std::shared_ptr<const LearnedModel> treatment,
    HoldoutCollectionConfig config,
    std::ostream& progress) {
    if (!parent || !control || !treatment) {
        throw std::invalid_argument(
            "HOLD1 collection requires three non-null models");
    }
    if (config.balanced_blocks == 0 ||
        config.max_game_turns == 0 ||
        config.pilot_training_games == 0) {
        throw std::invalid_argument(
            "HOLD1 collection limits must be positive");
    }
    return collect_holdout_impl(
        std::move(parent), std::move(control),
        std::move(treatment), config, progress);
}

ArtifactSnapshot snapshot_artifact(const std::string& path) {
    const ArtifactSnapshot before = snapshot_metadata(path);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot open evaluation artifact '" + path + "'");
    }
    ContentHash hash;
    std::array<char, 64 * 1024> buffer{};
    std::uintmax_t bytes_read = 0;
    while (input) {
        input.read(
            buffer.data(),
            static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count < 0) {
            throw std::runtime_error(
                "cannot read evaluation artifact '" + path + "'");
        }
        bytes_read += static_cast<std::uintmax_t>(count);
        for (std::streamsize index = 0; index < count; ++index) {
            hash.add_byte(static_cast<std::uint8_t>(
                static_cast<unsigned char>(buffer[
                    static_cast<std::size_t>(index)])));
        }
    }
    if (!input.eof()) {
        throw std::runtime_error(
            "cannot read evaluation artifact '" + path + "'");
    }
    const ArtifactSnapshot after = snapshot_metadata(path);
    if (before.path != after.path ||
        before.size != after.size ||
        before.modification_time_ticks !=
            after.modification_time_ticks ||
        bytes_read != before.size) {
        throw std::runtime_error(
            "evaluation artifact changed while being hashed: '" +
            path + "'");
    }
    ArtifactSnapshot result = after;
    result.content_hash = hash.finish();
    return result;
}

ClusteredEstimate cr1_clustered_estimate(
    std::span<const ClusteredValue> values) {
    if (values.empty()) {
        throw std::invalid_argument(
            "CR1 estimate requires records");
    }
    long double sum = 0.0L;
    std::map<std::size_t, std::vector<double>> clusters;
    for (const ClusteredValue& value : values) {
        if (!std::isfinite(value.value)) {
            throw std::invalid_argument(
                "CR1 values must be finite");
        }
        sum += value.value;
        clusters[value.cluster].push_back(value.value);
    }
    if (clusters.size() < 2) {
        throw std::invalid_argument(
            "CR1 estimate requires at least two clusters");
    }
    const long double mean =
        sum / static_cast<long double>(values.size());
    long double cluster_square_sum = 0.0L;
    for (const auto& [cluster, observations] : clusters) {
        static_cast<void>(cluster);
        long double residual_sum = 0.0L;
        for (const double observation : observations) {
            residual_sum += observation - mean;
        }
        cluster_square_sum +=
            residual_sum * residual_sum;
    }
    const long double cluster_count =
        static_cast<long double>(clusters.size());
    const long double record_count =
        static_cast<long double>(values.size());
    const long double variance =
        cluster_count / (cluster_count - 1.0L) *
        cluster_square_sum /
        (record_count * record_count);
    const double standard_error =
        std::sqrt(std::max(
            0.0, static_cast<double>(variance)));
    const double estimate = static_cast<double>(mean);
    return {
        .records = values.size(),
        .clusters = clusters.size(),
        .mean = estimate,
        .standard_error = standard_error,
        .confidence_lower_95 =
            estimate -
            kNormal95CriticalValue * standard_error,
        .confidence_upper_95 =
            estimate +
            kNormal95CriticalValue * standard_error,
    };
}

std::vector<HoldoutTask> holdout_schedule(
    std::uint64_t seed, std::size_t generation,
    std::size_t balanced_blocks) {
    if (generation == 0 || balanced_blocks == 0) {
        throw std::invalid_argument(
            "holdout schedule generation and block count must "
            "be positive");
    }
    std::vector<HoldoutTask> result;
    result.reserve(
        balanced_blocks *
        learned_iteration::kBalancedScheduleGames);
    for (std::size_t block = 0; block < balanced_blocks;
         ++block) {
        const auto schedule =
            learned_iteration::balanced_schedule(
                seed, generation, block);
        for (const auto& game : schedule) {
            result.push_back({
                .physical_game = result.size(),
                .scheduled = game,
            });
        }
    }
    return result;
}

HoldoutReport score_holdout_records(
    std::span<const HoldoutRecord> records) {
    if (records.empty()) {
        throw std::invalid_argument(
            "holdout report requires records");
    }
    HoldoutReport report;
    report.pooled = score_scope(
        select_records(records, std::nullopt));
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        report.by_deck[deck] = score_scope(
            select_records(
                records, static_cast<DeckId>(deck)));
    }
    return report;
}

OfflineGateReport evaluate_offline_gate(
    const HoldoutReport& report, OfflineGateConfig config) {
    if (config.expected_physical_games == 0 ||
        config.expected_perspectives_per_deck == 0) {
        throw std::invalid_argument(
            "offline gate expected accounting must be positive");
    }
    OfflineGateReport gate;
    gate.accounting_exact =
        report.pooled.physical_games ==
            config.expected_physical_games &&
        report.pooled.perspectives ==
            kDeckCount *
                config.expected_perspectives_per_deck;
    for (const auto& deck : report.by_deck) {
        gate.accounting_exact =
            gate.accounting_exact &&
            deck.physical_games ==
                config.expected_perspectives_per_deck &&
            deck.perspectives ==
                config.expected_perspectives_per_deck;
    }

    gate.pooled_losses_improved = true;
    for (const auto& comparison :
         report.pooled.treatment_comparisons) {
        gate.pooled_losses_improved =
            gate.pooled_losses_improved &&
            comparison.brier_delta.confidence_upper_95 <
                0.0 &&
            comparison.soft_log_loss_delta
                    .confidence_upper_95 <
                0.0;
    }

    gate.per_deck_loss_guard = true;
    for (const auto& deck : report.by_deck) {
        for (const auto& comparison :
             deck.treatment_comparisons) {
            gate.per_deck_loss_guard =
                gate.per_deck_loss_guard &&
                comparison.brier_delta.mean <=
                    kDeckLossGuard &&
                comparison.soft_log_loss_delta.mean <=
                    kDeckLossGuard;
        }
    }

    const auto bias_shrank =
        [&report](DeckId deck) {
            const auto& models =
                report.by_deck[deck_index(deck)].models;
            const double treatment =
                std::abs(
                    models[kTreatmentIndex]
                        .signed_bias.mean);
            return treatment <
                       std::abs(
                           models[kControlIndex]
                               .signed_bias.mean) &&
                   treatment <
                       std::abs(
                           models[kParentIndex]
                               .signed_bias.mean);
        };
    gate.green_bias_shrank =
        bias_shrank(DeckId::Green);
    gate.blue_bias_shrank =
        bias_shrank(DeckId::Blue);

    gate.no_new_material_bias = true;
    for (const auto& deck : report.by_deck) {
        const auto& treatment =
            deck.models[kTreatmentIndex].signed_bias;
        if (!material_bias(treatment)) {
            continue;
        }
        const bool inherited =
            same_sign_material_bias(
                treatment,
                deck.models[kControlIndex].signed_bias) ||
            same_sign_material_bias(
                treatment,
                deck.models[kParentIndex].signed_bias);
        gate.no_new_material_bias =
            gate.no_new_material_bias && inherited;
    }

    append_failure(
        gate.accounting_exact,
        "HOLD1 physical-game/perspective accounting is not exact",
        gate.failures);
    append_failure(
        gate.pooled_losses_improved,
        "TW75 pooled Brier/log-loss upper CI did not beat both controls",
        gate.failures);
    append_failure(
        gate.per_deck_loss_guard,
        "TW75 worsened a per-deck loss by more than 0.01",
        gate.failures);
    append_failure(
        gate.green_bias_shrank,
        "Green absolute signed bias did not strictly shrink against both controls",
        gate.failures);
    append_failure(
        gate.blue_bias_shrank,
        "Blue absolute signed bias did not strictly shrink against both controls",
        gate.failures);
    append_failure(
        gate.no_new_material_bias,
        "TW75 introduced a new same-sign-uncontrolled material deck bias",
        gate.failures);
    gate.passed =
        gate.accounting_exact &&
        gate.pooled_losses_improved &&
        gate.per_deck_loss_guard &&
        gate.green_bias_shrank &&
        gate.blue_bias_shrank &&
        gate.no_new_material_bias;
    return gate;
}

std::vector<GameplayTask> same_deck_gameplay_schedule(
    std::uint64_t seed, std::size_t quartets_per_deck) {
    if (quartets_per_deck == 0) {
        throw std::invalid_argument(
            "gameplay schedule requires quartets");
    }
    std::vector<GameplayTask> result;
    result.reserve(
        kDeckCount * quartets_per_deck * 4);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        for (std::size_t quartet = 0;
             quartet < quartets_per_deck; ++quartet) {
            const std::uint64_t game_seed =
                learned_iteration::derive_seed(
                    seed,
                    learned_iteration::SeedDomain::
                        SelfPlayGame,
                    kHoldoutGeneration,
                    deck * quartets_per_deck + quartet);
            for (std::size_t challenger_player = 0;
                 challenger_player < 2;
                 ++challenger_player) {
                for (std::size_t starting_player = 0;
                     starting_player < 2;
                     ++starting_player) {
                    result.push_back({
                        .deck = static_cast<DeckId>(deck),
                        .quartet = quartet,
                        .challenger_player =
                            challenger_player,
                        .starting_player =
                            starting_player,
                        .seed = game_seed,
                    });
                }
            }
        }
    }
    return result;
}

GameplayPanelReport score_gameplay_outcomes(
    std::span<const GameplayOutcome> outcomes) {
    if (outcomes.empty()) {
        throw std::invalid_argument(
            "gameplay panel requires outcomes");
    }
    GameplayPanelReport report;
    std::vector<ClusteredValue> pooled_scores;
    std::array<std::vector<ClusteredValue>, kDeckCount>
        deck_scores;
    pooled_scores.reserve(outcomes.size());
    std::map<std::pair<std::size_t, std::size_t>, std::size_t>
        pooled_cluster_ids;
    for (const GameplayOutcome& outcome : outcomes) {
        const std::size_t deck = deck_index(outcome.deck);
        if (outcome.challenger_won &&
            outcome.baseline_won) {
            throw std::invalid_argument(
                "gameplay outcome cannot have two winners");
        }
        const double score =
            outcome.challenger_won
                ? 1.0
                : (outcome.baseline_won ? 0.0 : 0.5);
        const auto cluster_key =
            std::pair{deck, outcome.quartet};
        const auto [cluster, inserted] =
            pooled_cluster_ids.emplace(
                cluster_key,
                pooled_cluster_ids.size());
        static_cast<void>(inserted);
        pooled_scores.push_back({
            .cluster = cluster->second,
            .value = score,
        });
        deck_scores[deck].push_back({
            .cluster = outcome.quartet,
            .value = score,
        });

        ++report.games;
        ++report.by_deck[deck].games;
        if (outcome.challenger_won) {
            ++report.wins;
            ++report.by_deck[deck].wins;
        } else if (outcome.baseline_won) {
            ++report.losses;
            ++report.by_deck[deck].losses;
        } else {
            ++report.draws;
            ++report.by_deck[deck].draws;
        }
    }
    report.score =
        cr1_clustered_estimate(pooled_scores);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        if (deck_scores[deck].empty()) {
            throw std::invalid_argument(
                "gameplay panel is missing a deck");
        }
        report.by_deck[deck].score =
            cr1_clustered_estimate(deck_scores[deck]);
    }
    return report;
}

GameplayGateReport evaluate_gameplay_gate(
    const GameplayPanelReport& report,
    GameplayGateConfig config) {
    if (config.expected_total_games == 0 ||
        config.expected_games_per_deck == 0 ||
        config.expected_quartets_per_deck == 0 ||
        config.minimum_aggregate_wins == 0) {
        throw std::invalid_argument(
            "gameplay gate expected accounting must be positive");
    }
    GameplayGateReport gate;
    gate.accounting_exact =
        report.games == config.expected_total_games &&
        report.wins + report.losses + report.draws ==
            report.games &&
        report.score.records == report.games &&
        report.score.clusters ==
            kDeckCount *
                config.expected_quartets_per_deck;
    std::size_t deck_games = 0;
    std::size_t deck_wins = 0;
    std::size_t deck_losses = 0;
    std::size_t deck_draws = 0;
    for (const auto& deck : report.by_deck) {
        gate.accounting_exact =
            gate.accounting_exact &&
            deck.games == config.expected_games_per_deck;
        gate.accounting_exact =
            gate.accounting_exact &&
            deck.wins + deck.losses + deck.draws ==
                deck.games &&
            deck.score.records == deck.games &&
            deck.score.clusters ==
                config.expected_quartets_per_deck;
        deck_games += deck.games;
        deck_wins += deck.wins;
        deck_losses += deck.losses;
        deck_draws += deck.draws;
    }
    gate.accounting_exact =
        gate.accounting_exact &&
        deck_games == report.games &&
        deck_wins == report.wins &&
        deck_losses == report.losses &&
        deck_draws == report.draws;
    gate.aggregate_wins_passed =
        report.wins >= config.minimum_aggregate_wins;
    gate.every_deck_non_losing =
        std::all_of(
            report.by_deck.begin(), report.by_deck.end(),
            [](const GameplayDeckReport& deck) {
                return deck.wins >= deck.losses;
            });
    append_failure(
        gate.accounting_exact,
        "gameplay panel accounting is not exact",
        gate.failures);
    append_failure(
        gate.aggregate_wins_passed,
        "gameplay panel did not reach the aggregate win threshold",
        gate.failures);
    append_failure(
        gate.every_deck_non_losing,
        "gameplay panel has a losing deck",
        gate.failures);
    gate.passed =
        gate.accounting_exact &&
        gate.aggregate_wins_passed &&
        gate.every_deck_non_losing;
    return gate;
}

StageDecision evaluation_stage_decision(
    bool offline_passed,
    std::optional<bool> panel_one_passed) {
    return {
        .run_panel_one = offline_passed,
        .run_panel_two =
            offline_passed &&
            panel_one_passed.value_or(false),
    };
}

SealedEvaluationReport
run_sealed_terminal_weight_c17_evaluation(
    std::ostream& progress) {
    const std::string parent_path =
        learned_value_challenger_cache_path(
            800, kDefaultLearnedTrainingSeed, 16);
    const std::string bundle_path =
        learned_terminal_weight_c17_cache_path(
            800, kDefaultLearnedTrainingSeed,
            kTerminalWeightC17ShardSeed);

    SealedEvaluationReport output;
    output.artifacts_before = {
        snapshot_artifact(parent_path),
        snapshot_artifact(bundle_path),
    };
    bool snapshots_verified = false;
    const auto verify_snapshots = [&] {
        output.artifacts_after = {
            snapshot_artifact(parent_path),
            snapshot_artifact(bundle_path),
        };
        if (output.artifacts_before !=
            output.artifacts_after) {
            throw std::runtime_error(
                "sealed evaluation artifact content, size, or "
                "timestamp changed during evaluation");
        }
        snapshots_verified = true;
    };

    try {
        progress
            << "Loading exact frozen C16 and paired TW-C17 "
               "artifacts..."
            << std::flush;
        const auto parent_artifact =
            load_learned_value_challenger_artifact(
                parent_path, 800,
                kDefaultLearnedTrainingSeed, 16);
        const auto bundle =
            load_learned_terminal_weight_c17_artifact(
                bundle_path, 800,
                kDefaultLearnedTrainingSeed,
                kTerminalWeightC17ShardSeed);
        require_canonical_artifacts(
            parent_artifact, bundle);
        const auto parent = parent_artifact.model();
        const auto control = bundle.control_model();
        const auto treatment = bundle.treatment_model();
        output.parent_fingerprint =
            learned_model_fingerprint(parent);
        output.control_fingerprint =
            learned_model_fingerprint(control);
        output.treatment_fingerprint =
            learned_model_fingerprint(treatment);
        progress << " done\n";

        progress
            << "Checking deployed Value selection under hidden-zone "
               "repartition on immutable dev-v3 fixtures..."
            << std::flush;
        const auto hidden =
            probe_runner::verify_value_hidden_repartition(
                probe_runner::ProbeCorpusKind::DevV3,
                {
                    {
                        .name = "C16",
                        .model = parent,
                    },
                    {
                        .name = "TW50-C17",
                        .model = control,
                    },
                    {
                        .name = "TW75-C17",
                        .model = treatment,
                    },
                },
                8, 0.0);
        output.hidden_repartition_passed = hidden.passed;
        output.hidden_repartition_policies =
            hidden.policy_count;
        output.hidden_repartition_probes =
            hidden.probe_count;
        if (!hidden.passed) {
            throw std::runtime_error(
                "terminal-weight hidden-repartition audit "
                "did not pass");
        }
        progress << " done (" << hidden.policy_count
                 << " policies x " << hidden.probe_count
                 << " probes, bit-identical)\n";

        const auto records =
            collect_holdout_records(
                parent, control, treatment,
                {
                    .seed = kTerminalWeightC17HoldoutSeed,
                    .generation = kHoldoutGeneration,
                    .balanced_blocks =
                        kHoldoutBalancedBlocks,
                    .max_game_turns = 500,
                    .pilot_training_games = 800,
                },
                progress);
        output.holdout =
            score_holdout_records(records);
        output.offline_gate =
            evaluate_offline_gate(output.holdout);
        if (!output.offline_gate.passed) {
            progress
                << "HOLD1 rejected TW75; gameplay panels "
                   "suppressed.\n";
            verify_snapshots();
            return output;
        }

        output.treatment_vs_control =
            run_gameplay_panel(
                treatment, control,
                "panel 1 (TW75 vs TW50)", progress);
        output.treatment_vs_control_gate =
            evaluate_gameplay_gate(
                *output.treatment_vs_control);
        if (!output.treatment_vs_control_gate->passed) {
            progress
                << "Panel 1 rejected TW75; panel 2 "
                   "suppressed.\n";
            verify_snapshots();
            return output;
        }

        output.treatment_vs_parent =
            run_gameplay_panel(
                treatment, parent,
                "panel 2 (TW75 vs C16)", progress);
        output.treatment_vs_parent_gate =
            evaluate_gameplay_gate(
                *output.treatment_vs_parent);
        output.passed =
            output.treatment_vs_parent_gate->passed;
        verify_snapshots();
        return output;
    } catch (...) {
        if (!snapshots_verified) {
            verify_snapshots();
        }
        throw;
    }
}

void write_human_report(
    const SealedEvaluationReport& report,
    std::ostream& output) {
    output
        << "\nTW-C17 sealed evaluation\n"
        << "  Training/shard/holdout/gameplay seeds: "
        << kDefaultLearnedTrainingSeed << " / "
        << kTerminalWeightC17ShardSeed << " / "
        << kTerminalWeightC17HoldoutSeed << " / "
        << kTerminalWeightC17GameplaySeed
        << "\n  HOLD1: 5x40 balanced C16-mirror games, "
           "K=1/H=4, epsilon=0.05, max_turns=500"
        << "\n  Gameplay: 50 common-seed quartets/deck, "
           "K=8/H=4, epsilon=0, residual=0, max_turns=500\n"
        << "  Parent C16: " << report.parent_fingerprint
        << "\n  TW50-C17: " << report.control_fingerprint
        << "\n  TW75-C17: " << report.treatment_fingerprint
        << "\n  Artifact snapshots: "
        << (report.artifacts_before ==
                    report.artifacts_after
                ? "UNCHANGED"
                : "CHANGED")
        << "\n\nHOLD1 terminal calibration\n";

    const auto write_scope =
        [&output](
            std::string_view name,
            const HoldoutScopeMetrics& scope) {
            output
                << "  " << name << ": "
                << scope.records << " records, "
                << scope.perspectives << " perspectives, "
                << scope.physical_games << " physical games"
                << "\n    target mean/variance: "
                << format_real(scope.target_mean) << " / "
                << format_real(scope.target_variance)
                << "\n    i->i+4 trace turn distance: mean "
                << format_real(
                       scope.mean_trace_turn_distance)
                << ", range "
                << scope.minimum_trace_turn_distance << '-'
                << scope.maximum_trace_turn_distance
                << ", bootstrap/tail "
                << scope.bootstrapped_records << '/'
                << scope.terminal_tail_records << '\n';
            for (std::size_t model = 0;
                 model < kCriticModelCount; ++model) {
                const auto& metrics =
                    scope.models[model];
                output
                    << "    "
                    << critic_model_name(model)
                    << ": Brier "
                    << format_real(metrics.brier.mean)
                    << ", log loss "
                    << format_real(
                           metrics.soft_log_loss.mean)
                    << ", bias "
                    << format_real(
                           metrics.signed_bias.mean)
                    << " ["
                    << format_real(
                           metrics.signed_bias
                               .confidence_lower_95)
                    << ", "
                    << format_real(
                           metrics.signed_bias
                               .confidence_upper_95)
                    << "], saturation "
                    << format_real(
                           metrics.saturation_fraction)
                    << '\n';
            }
            constexpr std::array<std::string_view, 2>
                controls = {"TW50", "C16"};
            for (std::size_t comparison = 0;
                 comparison < controls.size();
                 ++comparison) {
                const auto& metrics =
                    scope.treatment_comparisons[
                        comparison];
                output
                    << "    TW75-" << controls[comparison]
                    << ": dBrier "
                    << format_real(
                           metrics.brier_delta.mean)
                    << " ["
                    << format_real(
                           metrics.brier_delta
                               .confidence_lower_95)
                    << ", "
                    << format_real(
                           metrics.brier_delta
                               .confidence_upper_95)
                    << "], dLogLoss "
                    << format_real(
                           metrics.soft_log_loss_delta
                               .mean)
                    << " ["
                    << format_real(
                           metrics.soft_log_loss_delta
                               .confidence_lower_95)
                    << ", "
                    << format_real(
                           metrics.soft_log_loss_delta
                               .confidence_upper_95)
                    << "]\n";
            }
        };
    write_scope("Pooled", report.holdout.pooled);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        write_scope(
            deck_name(static_cast<DeckId>(deck)),
            report.holdout.by_deck[deck]);
    }

    const auto write_gate =
        [&output](
            std::string_view name, bool passed,
            const std::vector<std::string>& failures) {
            output << "\n" << name << ": "
                   << (passed ? "PASS" : "REJECT") << '\n';
            for (const auto& failure : failures) {
                output << "  - " << failure << '\n';
            }
        };
    write_gate(
        "Offline gate", report.offline_gate.passed,
        report.offline_gate.failures);
    output
        << "Action probes: UNAVAILABLE "
           "(no qualified immutable Environment-v3 "
           "deep-reference cache; no agreement/regret claim)\n"
        << "Hidden-repartition invariance: "
        << (report.hidden_repartition_passed
                ? "PASS"
                : "FAIL")
        << " (" << report.hidden_repartition_policies
        << " policies x "
        << report.hidden_repartition_probes
        << " immutable dev-v3 probes)\n";

    const auto write_panel =
        [&output](
            std::string_view name,
            const GameplayPanelReport& panel,
            const GameplayGateReport& gate) {
            output
                << "\n" << name << ": "
                << panel.wins << '-' << panel.losses << '-'
                << panel.draws << " across " << panel.games
                << " games, quartet-cluster score "
                << format_real(panel.score.mean) << " ["
                << format_real(
                       panel.score.confidence_lower_95)
                << ", "
                << format_real(
                       panel.score.confidence_upper_95)
                << "]\n";
            for (std::size_t deck = 0;
                 deck < kDeckCount; ++deck) {
                const auto& row = panel.by_deck[deck];
                output
                    << "  "
                    << deck_name(
                           static_cast<DeckId>(deck))
                    << ": " << row.wins << '-'
                    << row.losses << '-' << row.draws
                    << ", score "
                    << format_real(row.score.mean)
                    << " ["
                    << format_real(
                           row.score.confidence_lower_95)
                    << ", "
                    << format_real(
                           row.score.confidence_upper_95)
                    << "]\n";
            }
            output << "  Gate: "
                   << (gate.passed ? "PASS" : "REJECT")
                   << '\n';
            for (const auto& failure : gate.failures) {
                output << "    - " << failure << '\n';
            }
        };
    if (report.treatment_vs_control.has_value()) {
        write_panel(
            "Panel 1, TW75 vs TW50",
            *report.treatment_vs_control,
            *report.treatment_vs_control_gate);
    } else {
        output
            << "\nPanel 1, TW75 vs TW50: SUPPRESSED\n";
    }
    if (report.treatment_vs_parent.has_value()) {
        write_panel(
            "Panel 2, TW75 vs C16",
            *report.treatment_vs_parent,
            *report.treatment_vs_parent_gate);
    } else {
        output
            << "Panel 2, TW75 vs C16: SUPPRESSED\n";
    }
    output << "\nScientific verdict: "
           << (report.passed ? "PASS" : "REJECT")
           << '\n';
}

void write_tsv_report(
    const SealedEvaluationReport& report,
    std::ostream& output) {
    constexpr std::string_view schema = "tw-c17-eval-v1";
    output
        << "\nTW_C17_TSV_BEGIN\n"
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
                << schema << '\t' << sanitize_tsv(type)
                << '\t' << sanitize_tsv(scope)
                << '\t' << sanitize_tsv(subject)
                << '\t' << sanitize_tsv(metric)
                << '\t' << sanitize_tsv(estimate)
                << '\t' << sanitize_tsv(se)
                << '\t' << sanitize_tsv(low)
                << '\t' << sanitize_tsv(high)
                << '\t' << sanitize_tsv(records)
                << '\t' << sanitize_tsv(clusters)
                << '\t' << sanitize_tsv(status)
                << '\t' << sanitize_tsv(detail) << '\n';
        };
    for (std::size_t artifact = 0; artifact < 2; ++artifact) {
        const auto& snapshot =
            report.artifacts_before[artifact];
        row(
            "artifact", "global",
            artifact == 0 ? "C16" : "TW-C17",
            "content_hash", "", "", "", "", "", "",
            snapshot ==
                    report.artifacts_after[artifact]
                ? "UNCHANGED"
                : "CHANGED",
            snapshot.path + "|" +
                std::to_string(snapshot.size) + "|" +
                std::to_string(
                    snapshot.modification_time_ticks) +
                "|" + snapshot.content_hash);
    }
    row(
        "config", "global", "seeds", "identity", "", "", "",
        "", "", "", "FIXED",
        "training=" +
            std::to_string(kDefaultLearnedTrainingSeed) +
            ";shard=" +
            std::to_string(kTerminalWeightC17ShardSeed) +
            ";holdout=" +
            std::to_string(kTerminalWeightC17HoldoutSeed) +
            ";gameplay=" +
            std::to_string(kTerminalWeightC17GameplaySeed));
    row(
        "config", "HOLD1", "C16-mirror", "recipe", "", "", "",
        "", std::to_string(kHoldoutPhysicalGames), "", "FIXED",
        "blocks=5;generation=17;K=1;H=4;epsilon=0.05;"
        "residual=0;max_turns=500");
    row(
        "config", "gameplay", "same-deck", "recipe", "", "", "",
        "", std::to_string(kGameplayTotalGames), "", "FIXED",
        "quartets_per_deck=50;K=8;H=4;epsilon=0;"
        "residual=0;max_turns=500");
    const auto write_model_identity =
        [&row](
            std::string_view model,
            std::string_view fingerprint) {
            row(
                "model", "global", model, "fingerprint",
                "", "", "", "", "", "", "FROZEN",
                fingerprint);
        };
    write_model_identity("C16", report.parent_fingerprint);
    write_model_identity(
        "TW50-C17", report.control_fingerprint);
    write_model_identity(
        "TW75-C17", report.treatment_fingerprint);

    const auto write_estimate =
        make_tsv_estimate_writer(row);
    const auto write_scope =
        [&row, &write_estimate](
            std::string_view name,
            const HoldoutScopeMetrics& scope) {
            row(
                "holdout", name, "target",
                "variance",
                format_real(scope.target_variance), "", "",
                "", std::to_string(scope.records), "", "", "");
            row(
                "holdout", name, "trace",
                "i_plus_4_turn_distance_mean",
                format_real(
                    scope.mean_trace_turn_distance),
                "", "", "",
                std::to_string(
                    scope.bootstrapped_records),
                "", "",
                "range=" +
                    std::to_string(
                        scope.minimum_trace_turn_distance) +
                    "-" +
                    std::to_string(
                        scope.maximum_trace_turn_distance) +
                    ";tail=" +
                    std::to_string(
                        scope.terminal_tail_records));
            for (std::size_t model = 0;
                 model < kCriticModelCount; ++model) {
                const auto subject =
                    critic_model_name(model);
                const auto& metrics =
                    scope.models[model];
                write_estimate(
                    name, subject, "brier",
                    metrics.brier);
                write_estimate(
                    name, subject, "soft_log_loss",
                    metrics.soft_log_loss);
                write_estimate(
                    name, subject, "signed_bias",
                    metrics.signed_bias);
                row(
                    "metric", name, subject,
                    "saturation_fraction",
                    format_real(
                        metrics.saturation_fraction),
                    "", "", "",
                    std::to_string(scope.records), "", "", "");
            }
            constexpr std::array<std::string_view, 2>
                comparisons = {
                    "TW75-minus-TW50",
                    "TW75-minus-C16",
                };
            for (std::size_t comparison = 0;
                 comparison < comparisons.size();
                 ++comparison) {
                write_estimate(
                    name, comparisons[comparison],
                    "brier_delta",
                    scope.treatment_comparisons[
                        comparison]
                        .brier_delta);
                write_estimate(
                    name, comparisons[comparison],
                    "soft_log_loss_delta",
                    scope.treatment_comparisons[
                        comparison]
                        .soft_log_loss_delta);
            }
        };
    write_scope("Pooled", report.holdout.pooled);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        write_scope(
            deck_name(static_cast<DeckId>(deck)),
            report.holdout.by_deck[deck]);
    }
    row(
        "gate", "offline", "TW75", "verdict", "", "", "",
        "", "", "",
        report.offline_gate.passed ? "PASS" : "REJECT",
        report.offline_gate.failures.empty()
            ? ""
            : report.offline_gate.failures.front());
    row(
        "diagnostic", "actions", "deep-reference",
        "availability", "", "", "", "", "", "",
        "UNAVAILABLE",
        "no qualified immutable Environment-v3 cache; not a gate");
    row(
        "diagnostic", "actions", "hidden-repartition",
        "bit_identity", "", "", "", "",
        std::to_string(
            report.hidden_repartition_policies *
            report.hidden_repartition_probes),
        "", report.hidden_repartition_passed ? "PASS" : "FAIL",
        std::to_string(report.hidden_repartition_policies) +
            " policies x " +
            std::to_string(report.hidden_repartition_probes) +
            " dev-v3 probes");

    const auto write_panel =
        [&row, &write_estimate](
            std::string_view name,
            const std::optional<GameplayPanelReport>& panel,
            const std::optional<GameplayGateReport>& gate) {
            if (!panel.has_value()) {
                row(
                    "panel", name, "TW75", "verdict", "", "",
                    "", "", "", "", "SUPPRESSED", "");
                return;
            }
            write_estimate(
                name, "TW75", "quartet_cluster_score",
                panel->score);
            row(
                "panel", name, "TW75", "record", "", "", "",
                "", std::to_string(panel->games), "",
                gate->passed ? "PASS" : "REJECT",
                std::to_string(panel->wins) + "-" +
                    std::to_string(panel->losses) + "-" +
                    std::to_string(panel->draws));
            for (std::size_t deck = 0; deck < kDeckCount;
                 ++deck) {
                const auto& deck_report =
                    panel->by_deck[deck];
                const auto deck_scope =
                    deck_name(static_cast<DeckId>(deck));
                write_estimate(
                    name, deck_scope,
                    "quartet_cluster_score",
                    deck_report.score);
                row(
                    "panel-deck", name, deck_scope, "record", "",
                    "", "", "",
                    std::to_string(deck_report.games), "",
                    deck_report.wins >= deck_report.losses
                        ? "NON_LOSING"
                        : "LOSING",
                    std::to_string(deck_report.wins) + "-" +
                        std::to_string(deck_report.losses) + "-" +
                        std::to_string(deck_report.draws));
            }
        };
    write_panel(
        "TW75-v-TW50", report.treatment_vs_control,
        report.treatment_vs_control_gate);
    write_panel(
        "TW75-v-C16", report.treatment_vs_parent,
        report.treatment_vs_parent_gate);
    row(
        "gate", "final", "TW75", "verdict", "", "", "", "",
        "", "", report.passed ? "PASS" : "REJECT", "");
    output << "TW_C17_TSV_END\n";
}

} // namespace old_school::terminal_weight_eval
