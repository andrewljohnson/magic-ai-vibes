#include "old_school/output_calibration.hpp"

#include "old_school/audit_common.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <utility>

namespace old_school::output_calibration {
namespace {

namespace common = audit_common;

class CompensatedSum {
  public:
    void add(long double value) {
        const long double next = sum_ + value;
        if (std::abs(sum_) >= std::abs(value)) {
            correction_ += (sum_ - next) + value;
        } else {
            correction_ += (value - next) + sum_;
        }
        sum_ = next;
    }

    long double value() const {
        return sum_ + correction_;
    }

  private:
    long double sum_ = 0.0L;
    long double correction_ = 0.0L;
};

std::size_t checked_deck_index(DeckId deck) {
    const std::size_t index = static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::out_of_range("OC1 deck is out of range");
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
    throw std::out_of_range("OC1 deck is out of range");
}

void validate_collection_config(const CollectionConfig& config) {
    if (config.generation == 0 ||
        config.balanced_blocks == 0 ||
        config.max_game_turns == 0 ||
        config.pilot_training_games == 0) {
        throw std::invalid_argument(
            "OC1 collection configuration must be positive");
    }
    if (config.balanced_blocks >
        std::numeric_limits<std::size_t>::max() /
            learned_iteration::kBalancedScheduleGames) {
        throw std::overflow_error(
            "OC1 collection schedule size overflows");
    }
}

bool collection_recipe_equal(
    const CollectionConfig& left,
    const CollectionConfig& right) {
    return left.seed == right.seed &&
           left.generation == right.generation &&
           left.balanced_blocks == right.balanced_blocks &&
           left.max_game_turns == right.max_game_turns &&
           left.pilot_training_games ==
               right.pilot_training_games;
}

bool finite_probability(double value) {
    return std::isfinite(value) && value >= 0.0 &&
           value <= 1.0;
}

void require_probability(double value, const char* field) {
    if (!finite_probability(value)) {
        throw std::invalid_argument(
            std::string("OC1 ") + field +
            " must be finite and in [0, 1]");
    }
}

bool arrays_bit_identical(
    const std::array<double, 2>& left,
    const std::array<double, 2>& right) {
    return common::bit_identical(
        std::span<const double>(left),
        std::span<const double>(right));
}

void require_leaf_mean(
    const std::array<double, 2>& leaves, double ensemble,
    const char* field) {
    require_probability(leaves[0], field);
    require_probability(leaves[1], field);
    require_probability(ensemble, field);
    const double expected = (leaves[0] + leaves[1]) / 2.0;
    if (!common::bit_identical(expected, ensemble)) {
        throw std::runtime_error(
            std::string("OC1 ") + field +
            " does not equal the two-leaf mean");
    }
}

void hash_row_prefix(
    common::ContentHash& hash, std::size_t physical_game,
    std::size_t perspective, DeckId deck,
    std::size_t trace_index, std::size_t trace_size) {
    hash.add_size(physical_game);
    hash.add_size(perspective);
    hash.add_size(checked_deck_index(deck));
    hash.add_size(trace_index);
    hash.add_size(trace_size);
}

void hash_encoded_row(
    common::ContentHash& hash, std::size_t physical_game,
    std::size_t perspective, DeckId deck,
    std::size_t trace_index, std::size_t trace_size,
    std::span<const double> features, double target,
    double weight) {
    hash_row_prefix(
        hash, physical_game, perspective, deck,
        trace_index, trace_size);
    common::hash_observation(hash, features);
    hash.add_double(target);
    hash.add_double(weight);
}

void hash_prediction_row(
    common::ContentHash& hash, std::size_t physical_game,
    std::size_t perspective, DeckId deck,
    std::size_t trace_index, std::size_t trace_size,
    std::span<const double> predictions) {
    hash_row_prefix(
        hash, physical_game, perspective, deck,
        trace_index, trace_size);
    common::hash_observation(hash, predictions);
}

void hash_bool(common::ContentHash& hash, bool value) {
    hash.add_u64(value ? 1U : 0U);
}

void hash_signed(common::ContentHash& hash, int value) {
    hash.add_u64(static_cast<std::uint64_t>(
        static_cast<std::int64_t>(value)));
}

void hash_cards(
    common::ContentHash& hash,
    std::span<const CardId> cards) {
    hash.add_size(cards.size());
    for (const CardId card : cards) {
        hash.add_size(static_cast<std::size_t>(card));
    }
}

void hash_public_player(
    common::ContentHash& hash,
    const PlayerState& player) {
    hash_signed(hash, player.life);
    hash_cards(hash, player.graveyard);
    hash_cards(hash, player.exile);
    hash.add_size(player.lands.size());
    for (const LandPermanent& land : player.lands) {
        hash.add_size(static_cast<std::size_t>(land.card));
        hash_bool(hash, land.tapped);
    }
    hash.add_size(player.creatures.size());
    for (const CreaturePermanent& creature :
         player.creatures) {
        hash.add_u64(creature.id);
        hash.add_size(
            static_cast<std::size_t>(creature.card));
        hash_bool(hash, creature.tapped);
        hash_bool(hash, creature.summoning_sick);
        hash_signed(hash, creature.damage);
        hash_signed(
            hash, creature.temporary_power_bonus);
        hash_signed(
            hash, creature.temporary_toughness_bonus);
        hash_bool(
            hash, creature.exile_on_death_this_turn);
    }
    hash.add_size(player.artifacts.size());
    for (const ArtifactPermanent& artifact :
         player.artifacts) {
        hash.add_u64(artifact.id);
        hash.add_size(
            static_cast<std::size_t>(artifact.card));
        hash_bool(hash, artifact.tapped);
    }
    hash_cards(hash, player.enchantments);
    hash_signed(hash, player.mana_pool.generic);
    hash_signed(hash, player.mana_pool.green);
    hash_signed(hash, player.mana_pool.red);
    hash_signed(hash, player.mana_pool.blue);
    hash_signed(hash, player.mana_pool.white);
    hash_bool(hash, player.land_played_this_turn);
}

void hash_stats(
    common::ContentHash& hash,
    const PlayerGameStats& stats) {
    hash.add_size(stats.cards_drawn);
    hash.add_size(stats.lands_played);
    hash.add_size(stats.spells_cast);
    hash.add_size(stats.spells_countered);
    hash.add_size(stats.damage_to_opponent);
    hash.add_size(stats.cards_milled);
    hash.add_size(stats.decisions);
    hash.add_size(stats.monte_carlo_rollouts);
}

void hash_target(
    common::ContentHash& hash,
    const std::optional<Target>& target) {
    hash_bool(hash, target.has_value());
    if (!target.has_value()) {
        return;
    }
    hash.add_size(target->player);
    hash_bool(hash, target->creature.has_value());
    if (target->creature.has_value()) {
        hash.add_u64(*target->creature);
    }
}

void hash_owner_visible_row(
    common::ContentHash& hash,
    std::size_t physical_game,
    std::size_t perspective, DeckId deck,
    std::size_t trace_index, std::size_t trace_size,
    const GameState& state) {
    hash_row_prefix(
        hash, physical_game, perspective, deck,
        trace_index, trace_size);
    hash.add_size(state.active_player);
    hash.add_size(state.starting_player);
    hash.add_size(state.turn_number);
    for (const std::size_t extra_turns :
         state.extra_turns_pending) {
        hash.add_size(extra_turns);
    }
    for (const bool failed_draw : state.failed_draw) {
        hash_bool(hash, failed_draw);
    }
    hash.add_u64(state.next_permanent_id);
    hash.add_u64(state.next_stack_object_id);
    for (std::size_t player = 0; player < 2; ++player) {
        hash_public_player(hash, state.players[player]);
        hash_stats(hash, state.stats[player]);
        hash.add_size(state.players[player].library.size());
        hash.add_size(state.players[player].hand.size());
        if (player == perspective) {
            hash_cards(hash, state.players[player].hand);
        }
    }
    hash.add_size(state.stack.size());
    for (const StackObject& object : state.stack) {
        hash.add_size(static_cast<std::size_t>(
            object.kind));
        hash.add_u64(object.id);
        hash.add_size(static_cast<std::size_t>(
            object.card));
        hash.add_size(object.controller);
        hash_target(hash, object.target);
        hash_bool(hash, object.spell_target.has_value());
        if (object.spell_target.has_value()) {
            hash.add_u64(*object.spell_target);
        }
        hash_signed(hash, object.x_value);
    }
}

void hash_game_result(
    common::ContentHash& hash, std::size_t physical_game,
    const GameResult& result) {
    hash.add_size(physical_game);
    hash.add_u64(static_cast<std::uint64_t>(
        static_cast<std::int64_t>(result.winner)));
    hash.add_size(static_cast<std::size_t>(result.reason));
    hash.add_size(result.turns);
    hash.add_size(result.starting_player);
    hash.add_u64(static_cast<std::uint64_t>(
        static_cast<std::int64_t>(result.ending_life[0])));
    hash.add_u64(static_cast<std::uint64_t>(
        static_cast<std::int64_t>(result.ending_life[1])));
    for (std::size_t player = 0; player < 2; ++player) {
        hash_stats(hash, result.player_stats[player]);
        hash.add_size(static_cast<std::size_t>(
            result.bots[player]));
    }
}

struct CollectedRow {
    std::size_t physical_game = 0;
    std::size_t perspective = 0;
    DeckId deck = DeckId::Green;
    std::size_t trace_index = 0;
    std::size_t trace_size = 0;
    std::vector<double> features;
    double target = 0.5;
    double weight = 1.0;
    std::array<double, 2> parent_leaf_predictions{};
    double parent_prediction = 0.5;
    std::array<double, 2> candidate_leaf_predictions{};
    double candidate_prediction = 0.5;
};

struct GameSlot {
    GameResult result;
    std::vector<CollectedRow> rows;
    std::array<HiddenDeckCounts, kDeckCount> hidden{};
    common::ContentHash original_owner_visible;
    common::ContentHash repartitioned_owner_visible;
    common::ContentHash original_encoded;
    common::ContentHash repartitioned_encoded;
    common::ContentHash original_parent_leaf;
    common::ContentHash repartitioned_parent_leaf;
    common::ContentHash original_parent_prediction;
    common::ContentHash repartitioned_parent_prediction;
    common::ContentHash original_candidate_leaf;
    common::ContentHash repartitioned_candidate_leaf;
    common::ContentHash original_candidate_prediction;
    common::ContentHash repartitioned_candidate_prediction;
};

struct RawCorpus {
    std::vector<CollectionTask> tasks;
    std::vector<GameResult> outcomes;
    std::vector<CollectedRow> rows;
    CollectionAccounting accounting;
    CorpusHashes hashes;
};

void validate_collection_tasks(
    std::span<const CollectionTask> tasks,
    const CollectionConfig& config) {
    if (tasks.empty()) {
        throw std::invalid_argument(
            "OC1 collection requires at least one task");
    }
    const ScheduleAccounting schedule =
        inspect_collection_schedule(
            tasks, config.balanced_blocks);
    if (!schedule.tasks_well_formed) {
        throw std::invalid_argument(
            "OC1 collection tasks are malformed");
    }

    for (const CollectionTask& task : tasks) {
        const auto expected =
            learned_iteration::balanced_schedule(
                config.seed, config.generation, task.block);
        if (task.scheduled.schedule_index >= expected.size() ||
            task.scheduled !=
                expected[task.scheduled.schedule_index]) {
            throw std::invalid_argument(
                "OC1 collection task does not match its "
                "configured schedule");
        }
    }
}

GameConfig frozen_parent_game_config(
    const CollectionTask& task,
    const CollectionConfig& collection,
    const std::shared_ptr<const LearnedModel>& parent) {
    GameConfig config;
    config.max_turns = collection.max_game_turns;
    config.starting_player =
        task.scheduled.starting_player;
    config.learned_model = parent;
    config.learned_search_depth = 1;
    const BotConfig pilot = {
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 1,
        .exploration_rate = kExplorationRate,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .training_games =
            collection.pilot_training_games,
        .learned_model = parent,
    };
    config.bots = {pilot, pilot};
    return config;
}

RawCorpus collect_raw_corpus(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    std::span<const CollectionTask> tasks,
    const CollectionConfig& config) {
    if (!parent) {
        throw std::invalid_argument(
            "OC1 collection requires a parent model");
    }
    validate_collection_config(config);
    validate_collection_tasks(tasks, config);

    // Fail before source games if either supplied model is not the exact
    // two-leaf LegacyStateOnly Value topology required by OC1.
    const GameState topology_state;
    static_cast<void>(
        learned_critic_leaf_values(
            topology_state, 0, parent));
    if (candidate) {
        static_cast<void>(
            learned_critic_leaf_values(
                topology_state, 0, candidate));
    }

    std::vector<GameSlot> slots(tasks.size());
    std::vector<std::exception_ptr> errors(tasks.size());
    std::atomic_size_t next_task = 0;
    const std::size_t requested_workers =
        config.worker_count == 0
            ? std::max(
                  1U, std::thread::hardware_concurrency())
            : config.worker_count;
    const std::size_t worker_count =
        std::min(tasks.size(), requested_workers);
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

    for (std::size_t worker = 0; worker < worker_count;
         ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t task_index =
                    next_task.fetch_add(
                        1, std::memory_order_relaxed);
                if (task_index >= tasks.size()) {
                    return;
                }
                try {
                    const CollectionTask& task =
                        tasks[task_index];
                    Game game(
                        cards_for_deck(
                            task.scheduled.seat_decks[0]),
                        cards_for_deck(
                            task.scheduled.seat_decks[1]),
                        task.scheduled.seed,
                        frozen_parent_game_config(
                            task, config, parent));
                    std::vector<GameState> trace;
                    GameSlot& slot = slots[task_index];
                    slot.result = game.run_with_trace(trace);
                    if (trace.empty()) {
                        throw std::runtime_error(
                            "OC1 source game produced an empty "
                            "trace");
                    }
                    if (slot.result.starting_player !=
                        task.scheduled.starting_player) {
                        throw std::runtime_error(
                            "OC1 source game starting player "
                            "does not match its schedule");
                    }

                    slot.rows.reserve(2 * trace.size());
                    const double weight =
                        1.0 /
                        static_cast<double>(trace.size());
                    if (!std::isfinite(weight) ||
                        weight <= 0.0) {
                        throw std::runtime_error(
                            "OC1 trace weight is invalid");
                    }
                    for (std::size_t trace_index = 0;
                         trace_index < trace.size();
                         ++trace_index) {
                        for (std::size_t perspective = 0;
                             perspective < 2; ++perspective) {
                            const DeckId deck =
                                task.scheduled
                                    .seat_decks[perspective];
                            const std::size_t deck_number =
                                checked_deck_index(deck);
                            const double target =
                                learned_discounted_terminal_target(
                                    slot.result, perspective);
                            require_probability(
                                target, "terminal target");
                            std::vector<double> features =
                                learned_observation(
                                    trace[trace_index],
                                    perspective);
                            if (features.empty() ||
                                !std::all_of(
                                    features.begin(),
                                    features.end(),
                                    [](double value) {
                                        return std::isfinite(
                                            value);
                                    })) {
                                throw std::runtime_error(
                                    "OC1 learned observation "
                                    "contains a nonfinite value");
                            }
                            const auto parent_leaves =
                                learned_critic_leaf_values(
                                    trace[trace_index],
                                    perspective, parent);
                            const double parent_prediction =
                                learned_critic_value(
                                    trace[trace_index],
                                    perspective, parent);
                            require_leaf_mean(
                                parent_leaves,
                                parent_prediction,
                                "parent prediction");

                            std::array<double, 2>
                                candidate_leaves{};
                            double candidate_prediction = 0.5;
                            if (candidate) {
                                candidate_leaves =
                                    learned_critic_leaf_values(
                                        trace[trace_index],
                                        perspective, candidate);
                                candidate_prediction =
                                    learned_critic_value(
                                        trace[trace_index],
                                        perspective, candidate);
                                require_leaf_mean(
                                    candidate_leaves,
                                    candidate_prediction,
                                    "candidate prediction");
                            }

                            const HiddenExchange hidden =
                                exchange_opponent_hidden_identity(
                                    trace[trace_index],
                                    perspective);
                            HiddenDeckCounts& hidden_counts =
                                slot.hidden[deck_number];
                            ++hidden_counts.attempted;
                            if (hidden.changed) {
                                ++hidden_counts.changed;
                            } else {
                                ++hidden_counts.unchanged;
                            }
                            const std::vector<double>
                                hidden_features =
                                    learned_observation(
                                        hidden.state,
                                        perspective);
                            if (!common::bit_identical(
                                    features,
                                    hidden_features)) {
                                throw std::runtime_error(
                                    "OC1 hidden repartition "
                                    "changed the learned "
                                    "observation");
                            }
                            const auto hidden_parent_leaves =
                                learned_critic_leaf_values(
                                    hidden.state, perspective,
                                    parent);
                            const double
                                hidden_parent_prediction =
                                    learned_critic_value(
                                        hidden.state,
                                        perspective, parent);
                            if (!arrays_bit_identical(
                                    parent_leaves,
                                    hidden_parent_leaves) ||
                                !common::bit_identical(
                                    parent_prediction,
                                    hidden_parent_prediction)) {
                                throw std::runtime_error(
                                    "OC1 hidden repartition "
                                    "changed a parent "
                                    "prediction");
                            }
                            std::array<double, 2>
                                hidden_candidate_leaves{};
                            double
                                hidden_candidate_prediction =
                                    0.5;
                            if (candidate) {
                                hidden_candidate_leaves =
                                    learned_critic_leaf_values(
                                        hidden.state,
                                        perspective, candidate);
                                hidden_candidate_prediction =
                                    learned_critic_value(
                                        hidden.state,
                                        perspective, candidate);
                                if (!arrays_bit_identical(
                                        candidate_leaves,
                                        hidden_candidate_leaves) ||
                                    !common::bit_identical(
                                        candidate_prediction,
                                        hidden_candidate_prediction)) {
                                    throw std::runtime_error(
                                        "OC1 hidden repartition "
                                        "changed a candidate "
                                        "prediction");
                                }
                            }

                            hash_owner_visible_row(
                                slot.original_owner_visible,
                                task.physical_game,
                                perspective, deck,
                                trace_index, trace.size(),
                                trace[trace_index]);
                            hash_owner_visible_row(
                                slot.repartitioned_owner_visible,
                                task.physical_game,
                                perspective, deck,
                                trace_index, trace.size(),
                                hidden.state);
                            hash_encoded_row(
                                slot.original_encoded,
                                task.physical_game,
                                perspective, deck,
                                trace_index, trace.size(),
                                features, target, weight);
                            hash_encoded_row(
                                slot.repartitioned_encoded,
                                task.physical_game,
                                perspective, deck,
                                trace_index, trace.size(),
                                hidden_features, target, weight);
                            hash_prediction_row(
                                slot.original_parent_leaf,
                                task.physical_game,
                                perspective, deck,
                                trace_index, trace.size(),
                                parent_leaves);
                            hash_prediction_row(
                                slot.repartitioned_parent_leaf,
                                task.physical_game,
                                perspective, deck,
                                trace_index, trace.size(),
                                hidden_parent_leaves);
                            const std::array<double, 1>
                                parent_row = {
                                    parent_prediction};
                            const std::array<double, 1>
                                hidden_parent_row = {
                                    hidden_parent_prediction};
                            hash_prediction_row(
                                slot.original_parent_prediction,
                                task.physical_game,
                                perspective, deck,
                                trace_index, trace.size(),
                                parent_row);
                            hash_prediction_row(
                                slot.repartitioned_parent_prediction,
                                task.physical_game,
                                perspective, deck,
                                trace_index, trace.size(),
                                hidden_parent_row);
                            if (candidate) {
                                hash_prediction_row(
                                    slot.original_candidate_leaf,
                                    task.physical_game,
                                    perspective, deck,
                                    trace_index, trace.size(),
                                    candidate_leaves);
                                hash_prediction_row(
                                    slot.repartitioned_candidate_leaf,
                                    task.physical_game,
                                    perspective, deck,
                                    trace_index, trace.size(),
                                    hidden_candidate_leaves);
                                const std::array<double, 1>
                                    candidate_row = {
                                        candidate_prediction};
                                const std::array<double, 1>
                                    hidden_candidate_row = {
                                        hidden_candidate_prediction};
                                hash_prediction_row(
                                    slot.original_candidate_prediction,
                                    task.physical_game,
                                    perspective, deck,
                                    trace_index, trace.size(),
                                    candidate_row);
                                hash_prediction_row(
                                    slot.repartitioned_candidate_prediction,
                                    task.physical_game,
                                    perspective, deck,
                                    trace_index, trace.size(),
                                    hidden_candidate_row);
                            }

                            slot.rows.push_back({
                                .physical_game =
                                    task.physical_game,
                                .perspective = perspective,
                                .deck = deck,
                                .trace_index = trace_index,
                                .trace_size = trace.size(),
                                .features =
                                    std::move(features),
                                .target = target,
                                .weight = weight,
                                .parent_leaf_predictions =
                                    parent_leaves,
                                .parent_prediction =
                                    parent_prediction,
                                .candidate_leaf_predictions =
                                    candidate_leaves,
                                .candidate_prediction =
                                    candidate_prediction,
                            });
                        }
                    }
                } catch (...) {
                    errors[task_index] =
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

    RawCorpus output;
    output.tasks.assign(tasks.begin(), tasks.end());
    output.accounting.config = config;
    output.accounting.schedule =
        inspect_collection_schedule(
            tasks, config.balanced_blocks);
    output.accounting.actor_perspectives =
        2 * tasks.size();

    common::ContentHash schedule_hash;
    common::ContentHash outcome_hash;
    common::ContentHash record_count_hash;
    common::ContentHash feature_hash;
    common::ContentHash target_hash;
    common::ContentHash weight_hash;
    common::ContentHash optimizer_hash;
    common::ContentHash parent_leaf_hash;
    common::ContentHash parent_prediction_hash;
    common::ContentHash candidate_leaf_hash;
    common::ContentHash candidate_prediction_hash;
    common::ContentHash hidden_original_owner_visible;
    common::ContentHash
        hidden_repartitioned_owner_visible;
    common::ContentHash hidden_original_encoded;
    common::ContentHash hidden_repartitioned_encoded;
    common::ContentHash hidden_original_parent_leaf;
    common::ContentHash hidden_repartitioned_parent_leaf;
    common::ContentHash hidden_original_parent_prediction;
    common::ContentHash
        hidden_repartitioned_parent_prediction;
    common::ContentHash hidden_original_candidate_leaf;
    common::ContentHash hidden_repartitioned_candidate_leaf;
    common::ContentHash hidden_original_candidate_prediction;
    common::ContentHash
        hidden_repartitioned_candidate_prediction;

    schedule_hash.add_text("old-school-oc1-schedule-v1");
    outcome_hash.add_text("old-school-oc1-outcomes-v1");
    record_count_hash.add_text(
        "old-school-oc1-record-counts-v1");
    feature_hash.add_text("old-school-oc1-features-v1");
    target_hash.add_text("old-school-oc1-targets-v1");
    weight_hash.add_text("old-school-oc1-weights-v1");
    optimizer_hash.add_text(
        "old-school-oc1-optimizer-input-v1");
    parent_leaf_hash.add_text(
        "old-school-oc1-parent-leaf-v1");
    parent_prediction_hash.add_text(
        "old-school-oc1-parent-prediction-v1");
    candidate_leaf_hash.add_text(
        "old-school-oc1-candidate-leaf-v1");
    candidate_prediction_hash.add_text(
        "old-school-oc1-candidate-prediction-v1");
    hidden_original_owner_visible.add_text(
        "old-school-oc1-hidden-owner-visible-v1");
    hidden_repartitioned_owner_visible.add_text(
        "old-school-oc1-hidden-owner-visible-v1");
    hidden_original_encoded.add_text(
        "old-school-oc1-hidden-encoded-v1");
    hidden_repartitioned_encoded.add_text(
        "old-school-oc1-hidden-encoded-v1");
    hidden_original_parent_leaf.add_text(
        "old-school-oc1-hidden-parent-leaf-v1");
    hidden_repartitioned_parent_leaf.add_text(
        "old-school-oc1-hidden-parent-leaf-v1");
    hidden_original_parent_prediction.add_text(
        "old-school-oc1-hidden-parent-prediction-v1");
    hidden_repartitioned_parent_prediction.add_text(
        "old-school-oc1-hidden-parent-prediction-v1");
    hidden_original_candidate_leaf.add_text(
        "old-school-oc1-hidden-candidate-leaf-v1");
    hidden_repartitioned_candidate_leaf.add_text(
        "old-school-oc1-hidden-candidate-leaf-v1");
    hidden_original_candidate_prediction.add_text(
        "old-school-oc1-hidden-candidate-prediction-v1");
    hidden_repartitioned_candidate_prediction.add_text(
        "old-school-oc1-hidden-candidate-prediction-v1");

    std::size_t total_records = 0;
    for (const auto& slot : slots) {
        total_records += slot.rows.size();
    }
    output.rows.reserve(total_records);
    output.outcomes.reserve(tasks.size());
    for (std::size_t index = 0; index < tasks.size();
         ++index) {
        const CollectionTask& task = tasks[index];
        GameSlot& slot = slots[index];
        common::hash_task(schedule_hash, task);
        hash_game_result(
            outcome_hash, task.physical_game,
            slot.result);
        output.outcomes.push_back(slot.result);
        record_count_hash.add_size(task.physical_game);
        record_count_hash.add_size(slot.rows.size());
        hidden_original_owner_visible.add_text(
            slot.original_owner_visible.finish());
        hidden_repartitioned_owner_visible.add_text(
            slot.repartitioned_owner_visible.finish());
        hidden_original_encoded.add_text(
            slot.original_encoded.finish());
        hidden_repartitioned_encoded.add_text(
            slot.repartitioned_encoded.finish());
        hidden_original_parent_leaf.add_text(
            slot.original_parent_leaf.finish());
        hidden_repartitioned_parent_leaf.add_text(
            slot.repartitioned_parent_leaf.finish());
        hidden_original_parent_prediction.add_text(
            slot.original_parent_prediction.finish());
        hidden_repartitioned_parent_prediction.add_text(
            slot.repartitioned_parent_prediction.finish());
        hidden_original_candidate_leaf.add_text(
            slot.original_candidate_leaf.finish());
        hidden_repartitioned_candidate_leaf.add_text(
            slot.repartitioned_candidate_leaf.finish());
        hidden_original_candidate_prediction.add_text(
            slot.original_candidate_prediction.finish());
        hidden_repartitioned_candidate_prediction.add_text(
            slot.repartitioned_candidate_prediction.finish());

        for (std::size_t deck = 0; deck < kDeckCount;
             ++deck) {
            HiddenDeckCounts& destination =
                output.accounting.hidden.by_deck[deck];
            const HiddenDeckCounts& source =
                slot.hidden[deck];
            destination.attempted += source.attempted;
            destination.changed += source.changed;
            destination.unchanged += source.unchanged;
        }
        output.rows.insert(
            output.rows.end(),
            std::make_move_iterator(slot.rows.begin()),
            std::make_move_iterator(slot.rows.end()));
    }

    CompensatedSum total_weight;
    std::array<CompensatedSum, kDeckCount>
        deck_total_weight{};
    for (const CollectedRow& row : output.rows) {
        const std::size_t deck =
            checked_deck_index(row.deck);
        ++output.accounting.by_deck[deck].records;
        deck_total_weight[deck].add(row.weight);
        total_weight.add(row.weight);

        hash_row_prefix(
            feature_hash, row.physical_game,
            row.perspective, row.deck, row.trace_index,
            row.trace_size);
        common::hash_observation(
            feature_hash, row.features);
        hash_row_prefix(
            target_hash, row.physical_game,
            row.perspective, row.deck, row.trace_index,
            row.trace_size);
        target_hash.add_double(row.target);
        hash_row_prefix(
            weight_hash, row.physical_game,
            row.perspective, row.deck, row.trace_index,
            row.trace_size);
        weight_hash.add_double(row.weight);
        common::hash_observation(
            optimizer_hash, row.features);
        optimizer_hash.add_double(row.target);
        optimizer_hash.add_double(row.weight);
        hash_prediction_row(
            parent_leaf_hash, row.physical_game,
            row.perspective, row.deck, row.trace_index,
            row.trace_size, row.parent_leaf_predictions);
        const std::array<double, 1> parent_row = {
            row.parent_prediction};
        hash_prediction_row(
            parent_prediction_hash, row.physical_game,
            row.perspective, row.deck, row.trace_index,
            row.trace_size, parent_row);
        if (candidate) {
            hash_prediction_row(
                candidate_leaf_hash, row.physical_game,
                row.perspective, row.deck,
                row.trace_index, row.trace_size,
                row.candidate_leaf_predictions);
            const std::array<double, 1> candidate_row = {
                row.candidate_prediction};
            hash_prediction_row(
                candidate_prediction_hash,
                row.physical_game, row.perspective,
                row.deck, row.trace_index, row.trace_size,
                candidate_row);
        }
    }
    output.accounting.records = output.rows.size();
    output.accounting.total_weight =
        static_cast<double>(total_weight.value());
    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        output.accounting.by_deck[deck].total_weight =
            static_cast<double>(
                deck_total_weight[deck].value());
        output.accounting.by_deck[deck].perspectives =
            output.accounting.schedule
                .perspectives_by_deck[deck];
        HiddenDeckCounts& pooled =
            output.accounting.hidden.pooled;
        const HiddenDeckCounts& source =
            output.accounting.hidden.by_deck[deck];
        pooled.attempted += source.attempted;
        pooled.changed += source.changed;
        pooled.unchanged += source.unchanged;
    }

    output.hashes = {
        .schedule = schedule_hash.finish(),
        .outcomes = outcome_hash.finish(),
        .record_counts = record_count_hash.finish(),
        .features = feature_hash.finish(),
        .targets = target_hash.finish(),
        .weights = weight_hash.finish(),
        .optimizer_input = optimizer_hash.finish(),
        .parent_leaf_predictions =
            parent_leaf_hash.finish(),
        .parent_predictions =
            parent_prediction_hash.finish(),
        .candidate_leaf_predictions =
            candidate_leaf_hash.finish(),
        .candidate_predictions =
            candidate_prediction_hash.finish(),
    };
    HiddenRepartitionReport& hidden =
        output.accounting.hidden;
    hidden.original_owner_visible_rows_hash =
        hidden_original_owner_visible.finish();
    hidden.repartitioned_owner_visible_rows_hash =
        hidden_repartitioned_owner_visible.finish();
    hidden.original_encoded_rows_hash =
        hidden_original_encoded.finish();
    hidden.repartitioned_encoded_rows_hash =
        hidden_repartitioned_encoded.finish();
    hidden.original_parent_leaf_hash =
        hidden_original_parent_leaf.finish();
    hidden.repartitioned_parent_leaf_hash =
        hidden_repartitioned_parent_leaf.finish();
    hidden.original_parent_prediction_hash =
        hidden_original_parent_prediction.finish();
    hidden.repartitioned_parent_prediction_hash =
        hidden_repartitioned_parent_prediction.finish();
    hidden.original_candidate_leaf_hash =
        hidden_original_candidate_leaf.finish();
    hidden.repartitioned_candidate_leaf_hash =
        hidden_repartitioned_candidate_leaf.finish();
    hidden.original_candidate_prediction_hash =
        hidden_original_candidate_prediction.finish();
    hidden.repartitioned_candidate_prediction_hash =
        hidden_repartitioned_candidate_prediction.finish();
    hidden.owner_visible_rows_bit_identical =
        hidden.original_owner_visible_rows_hash ==
        hidden.repartitioned_owner_visible_rows_hash;
    hidden.encoded_features_bit_identical =
        hidden.original_encoded_rows_hash ==
        hidden.repartitioned_encoded_rows_hash;
    hidden.parent_leaf_predictions_bit_identical =
        hidden.original_parent_leaf_hash ==
        hidden.repartitioned_parent_leaf_hash;
    hidden.parent_predictions_bit_identical =
        hidden.original_parent_prediction_hash ==
        hidden.repartitioned_parent_prediction_hash;
    hidden.candidate_leaf_predictions_bit_identical =
        hidden.original_candidate_leaf_hash ==
        hidden.repartitioned_candidate_leaf_hash;
    hidden.candidate_predictions_bit_identical =
        hidden.original_candidate_prediction_hash ==
        hidden.repartitioned_candidate_prediction_hash;
    if (!hidden.bit_identical()) {
        throw std::runtime_error(
            "OC1 hidden repartition hashes differ");
    }
    return output;
}

template <typename Record>
void validate_holdout_record(const Record& record) {
    checked_deck_index(record.deck);
    if (record.perspective >= 2 ||
        record.trace_size == 0 ||
        record.trace_index >= record.trace_size ||
        !std::isfinite(record.weight) ||
        record.weight <= 0.0 ||
        !common::bit_identical(
            record.weight,
            1.0 / static_cast<double>(
                      record.trace_size))) {
        throw std::invalid_argument(
            "OC1 holdout record metadata is invalid");
    }
    require_probability(record.target, "holdout target");
    require_leaf_mean(
        record.parent_leaf_predictions,
        record.parent_prediction,
        "holdout parent prediction");
    require_leaf_mean(
        record.candidate_leaf_predictions,
        record.candidate_prediction,
        "holdout candidate prediction");
}

void validate_complete_holdout_records(
    std::span<const HoldoutRecord> records) {
    if (records.empty()) {
        throw std::invalid_argument(
            "OC1 holdout scoring requires records");
    }
    using PerspectiveKey =
        std::pair<std::size_t, std::size_t>;
    struct PerspectiveLedger {
        DeckId deck = DeckId::Green;
        std::size_t trace_size = 0;
        std::vector<bool> indices;
    };
    std::map<PerspectiveKey, PerspectiveLedger> ledgers;
    for (const HoldoutRecord& record : records) {
        validate_holdout_record(record);
        const PerspectiveKey key = {
            record.physical_game, record.perspective};
        auto [iterator, inserted] =
            ledgers.try_emplace(
                key,
                PerspectiveLedger{
                    .deck = record.deck,
                    .trace_size = record.trace_size,
                    .indices =
                        std::vector<bool>(
                            record.trace_size, false),
                });
        PerspectiveLedger& ledger = iterator->second;
        if (!inserted &&
            (ledger.deck != record.deck ||
             ledger.trace_size != record.trace_size)) {
            throw std::invalid_argument(
                "OC1 holdout perspective metadata changed");
        }
        if (ledger.indices[record.trace_index]) {
            throw std::invalid_argument(
                "OC1 holdout contains a duplicate trace row");
        }
        ledger.indices[record.trace_index] = true;
    }
    for (const auto& [key, ledger] : ledgers) {
        static_cast<void>(key);
        if (!std::all_of(
                ledger.indices.begin(), ledger.indices.end(),
                [](bool present) { return present; })) {
            throw std::invalid_argument(
                "OC1 holdout perspective trace is incomplete");
        }
    }
}

std::vector<const HoldoutRecord*> select_records(
    std::span<const HoldoutRecord> records,
    const std::optional<DeckId>& deck) {
    std::vector<const HoldoutRecord*> selected;
    selected.reserve(records.size());
    for (const HoldoutRecord& record : records) {
        if (!deck.has_value() || record.deck == *deck) {
            selected.push_back(&record);
        }
    }
    if (selected.empty()) {
        throw std::invalid_argument(
            "OC1 metric scope has no records");
    }
    return selected;
}

template <typename Function>
WeightedClusteredEstimate estimate_records(
    const std::vector<const HoldoutRecord*>& records,
    Function metric) {
    std::vector<WeightedClusteredValue> values;
    values.reserve(records.size());
    for (const HoldoutRecord* record : records) {
        values.push_back({
            .cluster = record->physical_game,
            .weight = record->weight,
            .value = metric(*record),
        });
    }
    return weighted_cr1_estimate(values);
}

ModelMetrics score_model(
    const std::vector<const HoldoutRecord*>& records,
    bool candidate) {
    const auto prediction =
        [candidate](const HoldoutRecord& record) {
            return candidate
                       ? record.candidate_prediction
                       : record.parent_prediction;
        };
    ModelMetrics result;
    result.brier = estimate_records(
        records, [&](const HoldoutRecord& record) {
            const double error =
                prediction(record) - record.target;
            return error * error;
        });
    result.soft_log_loss = estimate_records(
        records, [&](const HoldoutRecord& record) {
            return common::soft_log_loss(
                prediction(record), record.target);
        });
    result.signed_bias = estimate_records(
        records, [&](const HoldoutRecord& record) {
            return prediction(record) - record.target;
        });
    CompensatedSum prediction_sum;
    CompensatedSum saturated_weight;
    for (const HoldoutRecord* record : records) {
        const double value = prediction(*record);
        prediction_sum.add(
            static_cast<long double>(record->weight) *
            value);
        if (value <= 0.01 || value >= 0.99) {
            ++result.saturated_records;
            saturated_weight.add(record->weight);
        }
    }
    result.prediction_mean = static_cast<double>(
        prediction_sum.value() /
        result.brier.total_weight);
    result.saturated_weight =
        static_cast<double>(saturated_weight.value());
    result.saturation_fraction =
        result.saturated_weight /
        result.brier.total_weight;
    return result;
}

ScopeReport score_scope(
    const std::vector<const HoldoutRecord*>& records) {
    ScopeReport result;
    result.records = records.size();
    std::set<std::size_t> games;
    std::set<std::pair<std::size_t, std::size_t>>
        perspectives;
    CompensatedSum target_sum;
    CompensatedSum total_weight;
    for (const HoldoutRecord* record : records) {
        games.insert(record->physical_game);
        perspectives.emplace(
            record->physical_game, record->perspective);
        target_sum.add(
            static_cast<long double>(record->weight) *
            record->target);
        total_weight.add(record->weight);
    }
    result.physical_games = games.size();
    result.actor_perspectives = perspectives.size();
    result.total_weight =
        static_cast<double>(total_weight.value());
    result.target_mean =
        static_cast<double>(
            target_sum.value() / total_weight.value());
    result.parent = score_model(records, false);
    result.candidate = score_model(records, true);
    result.candidate_minus_parent.brier_delta =
        estimate_records(
            records,
            [](const HoldoutRecord& record) {
                const double parent_error =
                    record.parent_prediction -
                    record.target;
                const double candidate_error =
                    record.candidate_prediction -
                    record.target;
                return candidate_error * candidate_error -
                       parent_error * parent_error;
            });
    result.candidate_minus_parent.soft_log_loss_delta =
        estimate_records(
            records,
            [](const HoldoutRecord& record) {
                return common::soft_log_loss(
                           record.candidate_prediction,
                           record.target) -
                       common::soft_log_loss(
                           record.parent_prediction,
                           record.target);
            });
    return result;
}

void hash_estimate(
    common::ContentHash& hash,
    const WeightedClusteredEstimate& estimate) {
    hash.add_size(estimate.records);
    hash.add_size(estimate.clusters);
    hash.add_double(estimate.total_weight);
    hash.add_double(estimate.mean);
    hash.add_double(estimate.standard_error);
    hash.add_double(estimate.confidence_lower_95);
    hash.add_double(estimate.confidence_upper_95);
}

void hash_model_metrics(
    common::ContentHash& hash,
    const ModelMetrics& metrics) {
    hash_estimate(hash, metrics.brier);
    hash_estimate(hash, metrics.soft_log_loss);
    hash_estimate(hash, metrics.signed_bias);
    hash.add_double(metrics.prediction_mean);
    hash.add_size(metrics.saturated_records);
    hash.add_double(metrics.saturated_weight);
    hash.add_double(metrics.saturation_fraction);
}

void hash_scope(
    common::ContentHash& hash,
    const ScopeReport& scope) {
    hash.add_size(scope.records);
    hash.add_size(scope.physical_games);
    hash.add_size(scope.actor_perspectives);
    hash.add_double(scope.total_weight);
    hash.add_double(scope.target_mean);
    hash_model_metrics(hash, scope.parent);
    hash_model_metrics(hash, scope.candidate);
    hash_estimate(
        hash,
        scope.candidate_minus_parent.brier_delta);
    hash_estimate(
        hash,
        scope.candidate_minus_parent
            .soft_log_loss_delta);
}

bool material_bias(
    const WeightedClusteredEstimate& bias,
    double threshold) {
    return std::abs(bias.mean) >= threshold &&
           (bias.confidence_lower_95 > 0.0 ||
            bias.confidence_upper_95 < 0.0);
}

bool valid_digest(const std::string& digest) {
    return common::is_lower_hex_digest(digest);
}

bool hidden_hashes_valid(
    const HiddenRepartitionReport& hidden) {
    return valid_digest(
               hidden.original_owner_visible_rows_hash) &&
           valid_digest(
               hidden.repartitioned_owner_visible_rows_hash) &&
           valid_digest(
               hidden.original_encoded_rows_hash) &&
           valid_digest(
               hidden.repartitioned_encoded_rows_hash) &&
           valid_digest(
               hidden.original_parent_leaf_hash) &&
           valid_digest(
               hidden.repartitioned_parent_leaf_hash) &&
           valid_digest(
               hidden.original_parent_prediction_hash) &&
           valid_digest(
               hidden.repartitioned_parent_prediction_hash) &&
           valid_digest(
               hidden.original_candidate_leaf_hash) &&
           valid_digest(
               hidden.repartitioned_candidate_leaf_hash) &&
           valid_digest(
               hidden.original_candidate_prediction_hash) &&
           valid_digest(
               hidden.repartitioned_candidate_prediction_hash);
}

bool collection_exact(
    const CollectionAccounting& accounting,
    const CollectionConfig& expected,
    std::size_t expected_physical_games,
    std::size_t expected_perspectives_per_deck) {
    HiddenDeckCounts hidden_sum;
    std::size_t perspective_sum = 0;
    std::size_t record_sum = 0;
    long double weight_sum = 0.0L;
    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        const HiddenDeckCounts& hidden =
            accounting.hidden.by_deck[deck];
        hidden_sum.attempted += hidden.attempted;
        hidden_sum.changed += hidden.changed;
        hidden_sum.unchanged += hidden.unchanged;
        perspective_sum +=
            accounting.by_deck[deck].perspectives;
        record_sum += accounting.by_deck[deck].records;
        weight_sum +=
            accounting.by_deck[deck].total_weight;
    }
    if (!collection_recipe_equal(
            accounting.config, expected) ||
        !accounting.schedule.tasks_well_formed ||
        !accounting.schedule.exact_balanced_blocks ||
        accounting.schedule.physical_games !=
            expected_physical_games ||
        accounting.actor_perspectives !=
            2 * expected_physical_games ||
        perspective_sum != accounting.actor_perspectives ||
        accounting.records == 0 ||
        record_sum != accounting.records ||
        accounting.hidden.pooled.attempted !=
            accounting.records ||
        accounting.hidden.pooled != hidden_sum ||
        accounting.hidden.pooled.changed +
                accounting.hidden.pooled.unchanged !=
            accounting.hidden.pooled.attempted ||
        !accounting.hidden.bit_identical() ||
        !accounting.hidden.nonvacuous_all_decks()) {
        return false;
    }
    const double expected_total_mass =
        static_cast<double>(
            2 * expected_physical_games);
    if (std::abs(
            accounting.total_weight -
            expected_total_mass) >
            common::mass_tolerance(expected_total_mass) ||
        std::abs(
            accounting.total_weight -
            static_cast<double>(weight_sum)) >
            common::mass_tolerance(expected_total_mass)) {
        return false;
    }
    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        const DeckAccounting& row =
            accounting.by_deck[deck];
        const HiddenDeckCounts& hidden =
            accounting.hidden.by_deck[deck];
        const double expected_mass =
            static_cast<double>(
                expected_perspectives_per_deck);
        if (accounting.schedule
                    .perspectives_by_deck[deck] !=
                expected_perspectives_per_deck ||
            row.perspectives !=
                expected_perspectives_per_deck ||
            row.records == 0 ||
            std::abs(row.total_weight - expected_mass) >
                common::mass_tolerance(expected_mass) ||
            hidden.attempted != row.records ||
            hidden.changed == 0 ||
            hidden.changed + hidden.unchanged !=
                hidden.attempted) {
            return false;
        }
    }
    return hidden_hashes_valid(accounting.hidden);
}

bool report_accounting_exact(
    const HoldoutReport& report,
    const CollectionAccounting& accounting,
    std::size_t expected_physical_games,
    std::size_t expected_perspectives_per_deck) {
    const double expected_total_mass =
        static_cast<double>(
            kDeckCount *
            expected_perspectives_per_deck);
    if (report.pooled.physical_games !=
            expected_physical_games ||
        report.pooled.actor_perspectives !=
            kDeckCount *
                expected_perspectives_per_deck ||
        report.pooled.records != accounting.records ||
        report.pooled.actor_perspectives !=
            accounting.actor_perspectives ||
        std::abs(
            report.pooled.total_weight -
            expected_total_mass) >
            common::mass_tolerance(
                expected_total_mass) ||
        std::abs(
            report.pooled.total_weight -
            accounting.total_weight) >
            common::mass_tolerance(
                expected_total_mass)) {
        return false;
    }
    for (std::size_t index = 0; index < kDeckCount;
         ++index) {
        const ScopeReport& deck = report.by_deck[index];
        const DeckAccounting& collected =
            accounting.by_deck[index];
        const double expected_mass =
            static_cast<double>(
                expected_perspectives_per_deck);
        if (deck.actor_perspectives !=
                expected_perspectives_per_deck ||
            deck.physical_games !=
                expected_perspectives_per_deck ||
            deck.records != collected.records ||
            deck.actor_perspectives !=
                collected.perspectives ||
            std::abs(deck.total_weight - expected_mass) >
                common::mass_tolerance(expected_mass) ||
            std::abs(
                deck.total_weight -
                collected.total_weight) >
                common::mass_tolerance(expected_mass)) {
            return false;
        }
    }
    return true;
}

void append_failure(
    bool passed, std::string message,
    std::vector<std::string>& failures) {
    if (!passed) {
        failures.push_back(std::move(message));
    }
}

} // namespace

CollectionConfig canonical_fit_config() {
    return {
        .seed = kFitSeed,
        .generation = kFitGeneration,
        .balanced_blocks = kBalancedBlocks,
        .max_game_turns = kMaximumGameTurns,
        .pilot_training_games = kPilotTrainingGames,
        .worker_count = 0,
    };
}

CollectionConfig canonical_holdout_config() {
    return {
        .seed = kHoldoutSeed,
        .generation = kHoldoutGeneration,
        .balanced_blocks = kBalancedBlocks,
        .max_game_turns = kMaximumGameTurns,
        .pilot_training_games = kPilotTrainingGames,
        .worker_count = 0,
    };
}

std::vector<CollectionTask> collection_schedule(
    const CollectionConfig& config) {
    validate_collection_config(config);
    std::vector<CollectionTask> tasks;
    tasks.reserve(
        config.balanced_blocks *
        learned_iteration::kBalancedScheduleGames);
    for (std::size_t block = 0;
         block < config.balanced_blocks; ++block) {
        const auto schedule =
            learned_iteration::balanced_schedule(
                config.seed, config.generation, block);
        for (const auto& game : schedule) {
            tasks.push_back({
                .physical_game = tasks.size(),
                .block = block,
                .scheduled = game,
            });
        }
    }
    return tasks;
}

ScheduleAccounting inspect_collection_schedule(
    std::span<const CollectionTask> tasks,
    std::size_t expected_balanced_blocks) {
    ScheduleAccounting result;
    result.physical_games = tasks.size();
    if (expected_balanced_blocks == 0 ||
        expected_balanced_blocks >
            std::numeric_limits<std::size_t>::max() /
                learned_iteration::kBalancedScheduleGames) {
        return result;
    }
    std::vector<std::array<bool,
                           learned_iteration::
                               kBalancedScheduleGames>>
        seen(expected_balanced_blocks);
    bool well_formed = true;
    std::optional<std::size_t> previous_ordinal;
    for (std::size_t index = 0; index < tasks.size();
         ++index) {
        const CollectionTask& task = tasks[index];
        if (task.physical_game != index ||
            task.block >= expected_balanced_blocks ||
            task.scheduled.schedule_index >=
                learned_iteration::
                    kBalancedScheduleGames ||
            task.scheduled.pairing_index >=
                learned_iteration::kBalancedPairings ||
            task.scheduled.starting_player >= 2 ||
            task.scheduled.seat_decks[0] ==
                task.scheduled.seat_decks[1]) {
            well_formed = false;
            continue;
        }
        const std::size_t canonical_ordinal =
            task.block *
                learned_iteration::
                    kBalancedScheduleGames +
            task.scheduled.schedule_index;
        if (task.physical_game != canonical_ordinal ||
            (previous_ordinal.has_value() &&
             canonical_ordinal <= *previous_ordinal)) {
            well_formed = false;
            continue;
        }
        previous_ordinal = canonical_ordinal;
        if (seen[task.block]
                [task.scheduled.schedule_index]) {
            well_formed = false;
            continue;
        }
        seen[task.block]
            [task.scheduled.schedule_index] = true;
        for (std::size_t seat = 0; seat < 2; ++seat) {
            const std::size_t deck =
                static_cast<std::size_t>(
                    task.scheduled.seat_decks[seat]);
            if (deck >= kDeckCount) {
                well_formed = false;
                continue;
            }
            ++result.perspectives_by_deck[deck];
            const std::size_t starts =
                task.scheduled.starting_player == seat
                    ? 1
                    : 0;
            ++result.deck_seat_start[deck][seat][starts];
        }
    }
    result.tasks_well_formed = well_formed;

    const std::size_t expected_games =
        expected_balanced_blocks *
        learned_iteration::kBalancedScheduleGames;
    const std::size_t expected_deck_perspectives =
        16 * expected_balanced_blocks;
    const std::size_t expected_quadrant =
        4 * expected_balanced_blocks;
    bool exact =
        well_formed && tasks.size() == expected_games;
    for (const auto& block : seen) {
        exact =
            exact &&
            std::all_of(
                block.begin(), block.end(),
                [](bool present) { return present; });
    }
    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        exact =
            exact &&
            result.perspectives_by_deck[deck] ==
                expected_deck_perspectives;
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t starts = 0; starts < 2;
                 ++starts) {
                exact =
                    exact &&
                    result.deck_seat_start[deck][seat]
                                                [starts] ==
                        expected_quadrant;
            }
        }
    }
    result.exact_balanced_blocks = exact;
    return result;
}

HiddenExchange exchange_opponent_hidden_identity(
    const GameState& source, std::size_t perspective) {
    if (perspective >= 2) {
        throw std::out_of_range(
            "OC1 hidden-exchange perspective is out of range");
    }
    HiddenExchange output{.state = source};
    PlayerState& opponent =
        output.state.players[1 - perspective];
    for (std::size_t hand_index = 0;
         hand_index < opponent.hand.size(); ++hand_index) {
        const auto different = std::find_if(
            opponent.library.begin(),
            opponent.library.end(),
            [&](CardId card) {
                return card != opponent.hand[hand_index];
            });
        if (different != opponent.library.end()) {
            std::iter_swap(
                opponent.hand.begin() +
                    static_cast<std::ptrdiff_t>(
                        hand_index),
                different);
            output.changed = true;
            break;
        }
    }
    return output;
}

bool HiddenRepartitionReport::bit_identical() const {
    return owner_visible_rows_bit_identical &&
           encoded_features_bit_identical &&
           parent_leaf_predictions_bit_identical &&
           parent_predictions_bit_identical &&
           candidate_leaf_predictions_bit_identical &&
           candidate_predictions_bit_identical &&
           original_owner_visible_rows_hash ==
               repartitioned_owner_visible_rows_hash &&
           original_encoded_rows_hash ==
               repartitioned_encoded_rows_hash &&
           original_parent_leaf_hash ==
               repartitioned_parent_leaf_hash &&
           original_parent_prediction_hash ==
               repartitioned_parent_prediction_hash &&
           original_candidate_leaf_hash ==
               repartitioned_candidate_leaf_hash &&
           original_candidate_prediction_hash ==
               repartitioned_candidate_prediction_hash;
}

bool HiddenRepartitionReport::nonvacuous_all_decks() const {
    return std::all_of(
        by_deck.begin(), by_deck.end(),
        [](const HiddenDeckCounts& deck) {
            return deck.attempted > 0 &&
                   deck.changed > 0 &&
                   deck.changed + deck.unchanged ==
                       deck.attempted;
        });
}

TrainingCorpus collect_training_corpus(
    std::shared_ptr<const LearnedModel> parent,
    const CollectionConfig& config) {
    const std::vector<CollectionTask> tasks =
        collection_schedule(config);
    return collect_training_corpus(
        std::move(parent), tasks, config);
}

TrainingCorpus collect_training_corpus(
    std::shared_ptr<const LearnedModel> parent,
    std::span<const CollectionTask> tasks,
    const CollectionConfig& config) {
    RawCorpus raw = collect_raw_corpus(
        std::move(parent), nullptr, tasks, config);
    TrainingCorpus output;
    output.tasks = std::move(raw.tasks);
    output.outcomes = std::move(raw.outcomes);
    output.accounting = std::move(raw.accounting);
    output.hashes = std::move(raw.hashes);
    output.records.reserve(raw.rows.size());
    for (CollectedRow& row : raw.rows) {
        output.records.push_back({
            .physical_game = row.physical_game,
            .perspective = row.perspective,
            .deck = row.deck,
            .trace_index = row.trace_index,
            .trace_size = row.trace_size,
            .features = std::move(row.features),
            .target = row.target,
            .weight = row.weight,
            .parent_leaf_predictions =
                row.parent_leaf_predictions,
            .parent_prediction = row.parent_prediction,
        });
    }
    return output;
}

std::vector<LearnedWeightedCriticTrainingExample>
training_examples(const TrainingCorpus& corpus) {
    if (corpus.records.empty()) {
        throw std::invalid_argument(
            "OC1 training corpus is empty");
    }
    std::vector<LearnedWeightedCriticTrainingExample>
        output;
    output.reserve(corpus.records.size());
    for (const TrainingRecord& record : corpus.records) {
        checked_deck_index(record.deck);
        if (record.perspective >= 2 ||
            record.trace_size == 0 ||
            record.trace_index >= record.trace_size ||
            record.features.empty() ||
            !std::all_of(
                record.features.begin(),
                record.features.end(),
                [](double value) {
                    return std::isfinite(value);
                }) ||
            !std::isfinite(record.weight) ||
            record.weight <= 0.0 ||
            !common::bit_identical(
                record.weight,
                1.0 / static_cast<double>(
                          record.trace_size))) {
            throw std::invalid_argument(
                "OC1 training record is malformed");
        }
        require_probability(
            record.target, "training target");
        require_leaf_mean(
            record.parent_leaf_predictions,
            record.parent_prediction,
            "training parent prediction");
        output.push_back({
            .features = record.features,
            .target = record.target,
            .weight = record.weight,
        });
    }
    return output;
}

HoldoutCorpus collect_holdout_corpus(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const CollectionConfig& config) {
    const std::vector<CollectionTask> tasks =
        collection_schedule(config);
    return collect_holdout_corpus(
        std::move(parent), std::move(candidate),
        tasks, config);
}

HoldoutCorpus collect_holdout_corpus(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    std::span<const CollectionTask> tasks,
    const CollectionConfig& config) {
    if (!candidate) {
        throw std::invalid_argument(
            "OC1 holdout collection requires a candidate");
    }
    RawCorpus raw = collect_raw_corpus(
        std::move(parent), std::move(candidate),
        tasks, config);
    HoldoutCorpus output;
    output.tasks = std::move(raw.tasks);
    output.outcomes = std::move(raw.outcomes);
    output.accounting = std::move(raw.accounting);
    output.hashes = std::move(raw.hashes);
    output.records.reserve(raw.rows.size());
    for (const CollectedRow& row : raw.rows) {
        output.records.push_back({
            .physical_game = row.physical_game,
            .perspective = row.perspective,
            .deck = row.deck,
            .trace_index = row.trace_index,
            .trace_size = row.trace_size,
            .target = row.target,
            .weight = row.weight,
            .parent_leaf_predictions =
                row.parent_leaf_predictions,
            .parent_prediction = row.parent_prediction,
            .candidate_leaf_predictions =
                row.candidate_leaf_predictions,
            .candidate_prediction =
                row.candidate_prediction,
        });
    }
    return output;
}

WeightedClusteredEstimate weighted_cr1_estimate(
    std::span<const WeightedClusteredValue> values) {
    if (values.empty()) {
        throw std::invalid_argument(
            "OC1 weighted CR1 estimate requires records");
    }
    CompensatedSum weighted_sum;
    CompensatedSum total_weight;
    std::map<std::size_t,
             std::vector<WeightedClusteredValue>>
        clusters;
    for (const WeightedClusteredValue& value : values) {
        if (!std::isfinite(value.weight) ||
            value.weight <= 0.0 ||
            !std::isfinite(value.value)) {
            throw std::invalid_argument(
                "OC1 weighted CR1 inputs must be finite "
                "with positive weights");
        }
        weighted_sum.add(
            static_cast<long double>(value.weight) *
            value.value);
        total_weight.add(value.weight);
        clusters[value.cluster].push_back(value);
    }
    if (!std::isfinite(
            static_cast<double>(
                total_weight.value())) ||
        total_weight.value() <= 0.0L) {
        throw std::invalid_argument(
            "OC1 weighted CR1 total weight is invalid");
    }
    if (clusters.size() < 2) {
        throw std::invalid_argument(
            "OC1 weighted CR1 estimate requires two "
            "physical-game clusters");
    }
    const long double mean =
        weighted_sum.value() / total_weight.value();
    CompensatedSum cluster_square_sum;
    for (const auto& [cluster, observations] : clusters) {
        static_cast<void>(cluster);
        CompensatedSum residual_sum;
        for (const auto& observation : observations) {
            residual_sum.add(
                static_cast<long double>(
                    observation.weight) *
                (observation.value - mean));
        }
        cluster_square_sum.add(
            residual_sum.value() *
            residual_sum.value());
    }
    const long double cluster_count =
        static_cast<long double>(clusters.size());
    const long double variance =
        cluster_count / (cluster_count - 1.0L) *
        cluster_square_sum.value() /
        (total_weight.value() *
         total_weight.value());
    const double standard_error =
        std::sqrt(std::max(
            0.0, static_cast<double>(variance)));
    const double estimate = static_cast<double>(mean);
    return {
        .records = values.size(),
        .clusters = clusters.size(),
        .total_weight =
            static_cast<double>(total_weight.value()),
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

HoldoutReport score_holdout_records(
    std::span<const HoldoutRecord> records) {
    validate_complete_holdout_records(records);
    HoldoutReport output;
    output.pooled = score_scope(
        select_records(records, std::nullopt));
    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        output.by_deck[deck] = score_scope(
            select_records(
                records, static_cast<DeckId>(deck)));
    }
    return output;
}

std::string hash_holdout_report(
    const HoldoutReport& report) {
    common::ContentHash hash;
    hash.add_text("old-school-oc1-holdout-report-v1");
    hash_scope(hash, report.pooled);
    for (const ScopeReport& deck : report.by_deck) {
        hash_scope(hash, deck);
    }
    return hash.finish();
}

bool IntegrityEvidence::passed() const {
    return parent_identity && fit_provenance &&
           holdout_provenance && component_isolation &&
           artifact && determinism && finite_values &&
           valid_digest(original_fit_parameters_hash) &&
           valid_digest(
               repartitioned_fit_parameters_hash) &&
           original_fit_parameters_hash ==
               repartitioned_fit_parameters_hash &&
           valid_digest(
               original_scientific_report_hash) &&
           valid_digest(
               repartitioned_scientific_report_hash) &&
           original_scientific_report_hash ==
               repartitioned_scientific_report_hash;
}

GateReport evaluate_gate(
    const HoldoutReport& report,
    const CollectionAccounting& fit_accounting,
    const CollectionAccounting& holdout_accounting,
    const IntegrityEvidence& integrity,
    GateConfig config) {
    validate_collection_config(config.expected_fit);
    validate_collection_config(config.expected_holdout);
    if (config.expected_physical_games >
            std::numeric_limits<std::size_t>::max() / 2 ||
        config.expected_perspectives_per_deck >
            std::numeric_limits<std::size_t>::max() /
                kDeckCount) {
        throw std::invalid_argument(
            "OC1 gate accounting size overflows");
    }
    const std::size_t fit_games =
        config.expected_fit.balanced_blocks *
        learned_iteration::kBalancedScheduleGames;
    const std::size_t holdout_games =
        config.expected_holdout.balanced_blocks *
        learned_iteration::kBalancedScheduleGames;
    if (config.expected_physical_games == 0 ||
        config.expected_perspectives_per_deck == 0 ||
        fit_games != config.expected_physical_games ||
        holdout_games != config.expected_physical_games ||
        kDeckCount *
                config.expected_perspectives_per_deck !=
            2 * config.expected_physical_games ||
        config.expected_fit.balanced_blocks !=
            config.expected_holdout.balanced_blocks ||
        config.expected_fit.max_game_turns !=
            config.expected_holdout.max_game_turns ||
        config.expected_fit.pilot_training_games !=
            config.expected_holdout.pilot_training_games ||
        !std::isfinite(config.deck_loss_guard) ||
        config.deck_loss_guard < 0.0 ||
        !std::isfinite(
            config.other_deck_bias_guard) ||
        config.other_deck_bias_guard < 0.0 ||
        !std::isfinite(
            config.material_bias_threshold) ||
        config.material_bias_threshold <= 0.0 ||
        config.expected_fit.seed ==
            config.expected_holdout.seed ||
        config.expected_fit.generation ==
            config.expected_holdout.generation) {
        throw std::invalid_argument(
            "OC1 gate configuration is invalid");
    }

    GateReport gate;
    gate.integrity_passed = integrity.passed();
    gate.collection_accounting_exact =
        collection_exact(
            fit_accounting, config.expected_fit,
            config.expected_physical_games,
            config.expected_perspectives_per_deck) &&
        collection_exact(
            holdout_accounting,
            config.expected_holdout,
            config.expected_physical_games,
            config.expected_perspectives_per_deck) &&
        report_accounting_exact(
            report, holdout_accounting,
            config.expected_physical_games,
            config.expected_perspectives_per_deck);

    gate.pooled_losses_improved =
        report.pooled.candidate_minus_parent
                .brier_delta.confidence_upper_95 <
            0.0 &&
        report.pooled.candidate_minus_parent
                .soft_log_loss_delta
                .confidence_upper_95 <
            0.0;

    gate.per_deck_loss_guard = true;
    for (const ScopeReport& deck : report.by_deck) {
        gate.per_deck_loss_guard =
            gate.per_deck_loss_guard &&
            deck.candidate_minus_parent.brier_delta.mean <=
                config.deck_loss_guard &&
            deck.candidate_minus_parent
                    .soft_log_loss_delta.mean <=
                config.deck_loss_guard;
    }

    const auto absolute_bias =
        [&report](DeckId deck, bool candidate) {
            const ScopeReport& scope =
                report.by_deck[
                    static_cast<std::size_t>(deck)];
            return std::abs(
                candidate
                    ? scope.candidate.signed_bias.mean
                    : scope.parent.signed_bias.mean);
        };
    gate.blue_bias_shrank =
        absolute_bias(DeckId::Blue, true) <
        absolute_bias(DeckId::Blue, false);
    gate.green_bias_nonincreasing =
        absolute_bias(DeckId::Green, true) <=
        absolute_bias(DeckId::Green, false);

    gate.other_deck_bias_guard = true;
    for (const DeckId deck :
         {DeckId::Red, DeckId::White,
          DeckId::RUAggro}) {
        gate.other_deck_bias_guard =
            gate.other_deck_bias_guard &&
            absolute_bias(deck, true) -
                    absolute_bias(deck, false) <=
                config.other_deck_bias_guard;
    }

    gate.no_new_material_same_sign_bias = true;
    for (const ScopeReport& deck : report.by_deck) {
        const auto& candidate =
            deck.candidate.signed_bias;
        if (!material_bias(
                candidate,
                config.material_bias_threshold)) {
            continue;
        }
        const auto& parent = deck.parent.signed_bias;
        const bool inherited =
            material_bias(
                parent,
                config.material_bias_threshold) &&
            common::same_strict_sign(
                candidate.mean, parent.mean);
        gate.no_new_material_same_sign_bias =
            gate.no_new_material_same_sign_bias &&
            inherited;
    }

    append_failure(
        gate.integrity_passed,
        "OC1 provenance/component/artifact integrity failed",
        gate.failures);
    append_failure(
        gate.collection_accounting_exact,
        "OC1 fit/holdout accounting or hidden invariance is "
        "not exact",
        gate.failures);
    append_failure(
        gate.pooled_losses_improved,
        "OC1 pooled Brier/log-loss upper confidence bound "
        "did not beat C16",
        gate.failures);
    append_failure(
        gate.per_deck_loss_guard,
        "OC1 worsened a per-deck loss by more than 0.002",
        gate.failures);
    append_failure(
        gate.blue_bias_shrank,
        "OC1 Blue absolute signed bias did not strictly "
        "shrink",
        gate.failures);
    append_failure(
        gate.green_bias_nonincreasing,
        "OC1 Green absolute signed bias increased",
        gate.failures);
    append_failure(
        gate.other_deck_bias_guard,
        "OC1 increased another deck's absolute signed bias "
        "by more than 0.010",
        gate.failures);
    append_failure(
        gate.no_new_material_same_sign_bias,
        "OC1 introduced a new same-sign material deck bias",
        gate.failures);

    gate.passed =
        gate.integrity_passed &&
        gate.collection_accounting_exact &&
        gate.pooled_losses_improved &&
        gate.per_deck_loss_guard &&
        gate.blue_bias_shrank &&
        gate.green_bias_nonincreasing &&
        gate.other_deck_bias_guard &&
        gate.no_new_material_same_sign_bias;
    return gate;
}

} // namespace old_school::output_calibration
