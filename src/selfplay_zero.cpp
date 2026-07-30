#include "old_school/selfplay_zero.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <istream>
#include <limits>
#include <numeric>
#include <ostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace old_school::selfplay_zero {
namespace {

constexpr double kIllegalScore = -1e9;
constexpr double kImprovementMargin = 1e-9;

std::uint64_t mix_seed(std::uint64_t seed, std::uint64_t salt) {
    std::uint64_t z = seed + 0x9E3779B97F4A7C15ULL * (salt + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

using CardCountArray = std::array<int, kCardCount>;

void add_card(CardCountArray& counts, CardId card) {
    counts[static_cast<std::size_t>(card)] += 1;
}

void subtract_card(CardCountArray& counts, CardId card) {
    counts[static_cast<std::size_t>(card)] -= 1;
}

CardCountArray count_cards(const std::vector<CardId>& cards) {
    CardCountArray counts{};
    for (const CardId card : cards) {
        add_card(counts, card);
    }
    return counts;
}

void append_counts(std::vector<float>& features,
                   const CardCountArray& counts) {
    for (const int count : counts) {
        features.push_back(
            static_cast<float>(std::max(count, 0)) / 4.0f);
    }
}

CardCountArray public_battlefield_counts(const PublicPlayerState& player) {
    CardCountArray counts{};
    for (const auto& land : player.lands) {
        add_card(counts, land.card);
    }
    for (const auto& creature : player.creatures) {
        add_card(counts, creature.card);
    }
    for (const auto& artifact : player.artifacts) {
        add_card(counts, artifact.card);
    }
    for (const CardId enchantment : player.enchantments) {
        add_card(counts, enchantment);
    }
    return counts;
}

void subtract_public_zones(CardCountArray& counts,
                           const PublicPlayerState& player) {
    const CardCountArray battlefield = public_battlefield_counts(player);
    for (std::size_t index = 0; index < kCardCount; ++index) {
        counts[index] -= battlefield[index];
    }
    for (const CardId card : player.graveyard) {
        subtract_card(counts, card);
    }
    for (const CardId card : player.exile) {
        subtract_card(counts, card);
    }
}

int mana_pool_total(const ManaCost& pool) {
    return pool.generic + pool.green + pool.red + pool.blue + pool.white;
}

void append_player_scalars(std::vector<float>& features,
                           const PublicPlayerState& player,
                           std::size_t hand_size) {
    const auto push = [&features](double value) {
        features.push_back(static_cast<float>(value));
    };
    push(player.life / 20.0);
    push(std::min(player.life, 8) / 8.0);
    push(static_cast<double>(hand_size) / 7.0);
    push(static_cast<double>(player.library_size) / 40.0);
    push(static_cast<double>(player.graveyard.size()) / 20.0);
    const std::size_t lands = player.lands.size();
    std::size_t untapped_lands = 0;
    for (const auto& land : player.lands) {
        untapped_lands += land.tapped ? 0 : 1;
    }
    push(static_cast<double>(lands) / 10.0);
    push(static_cast<double>(untapped_lands) / 10.0);
    int power = 0;
    int toughness = 0;
    int untapped_power = 0;
    int ready_power = 0;
    int flying_power = 0;
    int damage = 0;
    int bonus_power = 0;
    std::size_t untapped_creatures = 0;
    std::size_t ready_creatures = 0;
    for (const auto& creature : player.creatures) {
        const auto& definition = card_definition(creature.card);
        const int creature_power =
            definition.power + creature.temporary_power_bonus;
        power += creature_power;
        toughness +=
            definition.toughness + creature.temporary_toughness_bonus;
        damage += creature.damage;
        bonus_power += creature.temporary_power_bonus;
        if (!creature.tapped) {
            untapped_creatures += 1;
            untapped_power += creature_power;
        }
        if (!creature.tapped && !creature.summoning_sick) {
            ready_creatures += 1;
            ready_power += creature_power;
        }
        if (definition.flying) {
            flying_power += creature_power;
        }
    }
    push(static_cast<double>(player.creatures.size()) / 6.0);
    push(static_cast<double>(untapped_creatures) / 6.0);
    push(static_cast<double>(ready_creatures) / 6.0);
    push(power / 12.0);
    push(toughness / 12.0);
    push(untapped_power / 12.0);
    push(ready_power / 12.0);
    push(flying_power / 8.0);
    push(damage / 8.0);
    push(bonus_power / 6.0);
    std::size_t untapped_artifacts = 0;
    for (const auto& artifact : player.artifacts) {
        untapped_artifacts += artifact.tapped ? 0 : 1;
    }
    push(static_cast<double>(player.artifacts.size()) / 4.0);
    push(static_cast<double>(untapped_artifacts) / 4.0);
    push(static_cast<double>(player.enchantments.size()) / 2.0);
    push(mana_pool_total(player.mana_pool) / 4.0);
    push(player.land_played_this_turn ? 1.0 : 0.0);
}

constexpr std::size_t kGlobalScalarCount = 12;
constexpr std::size_t kPlayerScalarCount = 22;
constexpr std::size_t kCardBlockCount = 11;

}  // namespace

const std::array<std::vector<CardId>, kSpzDeckCount>& spz_decks() {
    static const std::array<std::vector<CardId>, kSpzDeckCount> decks = {
        green_deck(), red_deck(), blue_deck(), white_control_deck(),
        ru_aggro_deck(),
    };
    return decks;
}

std::string_view spz_deck_name(std::size_t deck_index) {
    static constexpr std::array<std::string_view, kSpzDeckCount> names = {
        "Green", "Red", "Blue", "White", "RU Aggro",
    };
    return names.at(deck_index);
}

std::size_t spz_feature_count() {
    return kGlobalScalarCount + 2 * kPlayerScalarCount +
           kCardBlockCount * kCardCount;
}

std::vector<float> spz_features(
    const PlayerObservation& observation,
    const std::array<std::vector<CardId>, 2>& original_decks,
    TurnPhase phase) {
    const std::size_t me = observation.observer;
    const std::size_t opponent = 1 - me;
    const PublicPlayerState& my_public = observation.players[me];
    const PublicPlayerState& opponent_public = observation.players[opponent];

    std::vector<float> features;
    features.reserve(spz_feature_count());
    const auto push = [&features](double value) {
        features.push_back(static_cast<float>(value));
    };

    push(static_cast<double>(observation.turn_number) / 20.0);
    push(observation.active_player == me ? 1.0 : 0.0);
    for (std::size_t index = 0; index < kSpzPhaseCount; ++index) {
        push(static_cast<std::size_t>(phase) == index ? 1.0 : 0.0);
    }
    push(static_cast<double>(observation.stack.size()) / 3.0);
    push(static_cast<double>(observation.extra_turns_pending[me]));
    push(static_cast<double>(observation.extra_turns_pending[opponent]));

    append_player_scalars(features, my_public, observation.hand.size());
    append_player_scalars(features, opponent_public,
                          opponent_public.hand_size);

    // Stack spells are physical cards that must leave the hidden pools; the
    // extractor also exposes them directly as pending effects.
    CardCountArray my_stack{};
    CardCountArray opponent_stack{};
    CardCountArray my_stack_spells{};
    CardCountArray opponent_stack_spells{};
    for (const auto& object : observation.stack) {
        auto& exposure =
            object.controller == me ? my_stack : opponent_stack;
        add_card(exposure, object.card);
        if (object.kind == StackObjectKind::Spell) {
            auto& physical = object.controller == me
                                 ? my_stack_spells
                                 : opponent_stack_spells;
            add_card(physical, object.card);
        }
    }

    const CardCountArray my_hand = count_cards(observation.hand);

    // The observer's remaining library pool: its decklist minus every card
    // it can already account for.
    CardCountArray my_library = count_cards(original_decks[me]);
    subtract_public_zones(my_library, my_public);
    for (std::size_t index = 0; index < kCardCount; ++index) {
        my_library[index] -= my_hand[index] + my_stack_spells[index];
    }

    // The opponent's unseen pool (hand plus library combined).
    CardCountArray opponent_unseen = count_cards(original_decks[opponent]);
    subtract_public_zones(opponent_unseen, opponent_public);
    for (std::size_t index = 0; index < kCardCount; ++index) {
        opponent_unseen[index] -= opponent_stack_spells[index];
    }

    CardCountArray my_untapped_creatures{};
    for (const auto& creature : my_public.creatures) {
        if (!creature.tapped) {
            add_card(my_untapped_creatures, creature.card);
        }
    }
    CardCountArray opponent_untapped_creatures{};
    for (const auto& creature : opponent_public.creatures) {
        if (!creature.tapped) {
            add_card(opponent_untapped_creatures, creature.card);
        }
    }

    append_counts(features, my_hand);
    append_counts(features, public_battlefield_counts(my_public));
    append_counts(features, public_battlefield_counts(opponent_public));
    append_counts(features, my_untapped_creatures);
    append_counts(features, opponent_untapped_creatures);
    append_counts(features, count_cards(my_public.graveyard));
    append_counts(features, count_cards(opponent_public.graveyard));
    append_counts(features, my_library);
    append_counts(features, opponent_unseen);
    append_counts(features, my_stack);
    append_counts(features, opponent_stack);

    if (features.size() != spz_feature_count()) {
        throw std::logic_error("spz feature schema size mismatch");
    }
    return features;
}

GameState reconstruct_observed_state(const PlayerObservation& observation) {
    const std::size_t me = observation.observer;
    const std::size_t opponent = 1 - me;

    GameState state;
    for (std::size_t player = 0; player < 2; ++player) {
        const PublicPlayerState& public_state = observation.players[player];
        PlayerState& reconstructed = state.players[player];
        reconstructed.life = public_state.life;
        reconstructed.graveyard = public_state.graveyard;
        reconstructed.exile = public_state.exile;
        reconstructed.lands = public_state.lands;
        reconstructed.creatures = public_state.creatures;
        reconstructed.artifacts = public_state.artifacts;
        reconstructed.enchantments = public_state.enchantments;
        reconstructed.mana_pool = public_state.mana_pool;
        reconstructed.land_played_this_turn =
            public_state.land_played_this_turn;
        reconstructed.library.assign(public_state.library_size, CardId{});
    }
    state.players[me].hand = observation.hand;
    state.players[opponent].hand.assign(
        observation.players[opponent].hand_size, CardId{});
    state.stack = observation.stack;
    state.extra_turns_pending = observation.extra_turns_pending;
    state.active_player = observation.active_player;
    state.starting_player = observation.starting_player;
    state.turn_number = observation.turn_number;

    PermanentId maximum_permanent = 0;
    for (const PlayerState& player : state.players) {
        for (const auto& creature : player.creatures) {
            maximum_permanent = std::max(maximum_permanent, creature.id);
        }
        for (const auto& artifact : player.artifacts) {
            maximum_permanent = std::max(maximum_permanent, artifact.id);
        }
    }
    state.next_permanent_id = maximum_permanent + 1;
    StackObjectId maximum_stack_object = 0;
    for (const auto& object : state.stack) {
        maximum_stack_object = std::max(maximum_stack_object, object.id);
    }
    state.next_stack_object_id = maximum_stack_object + 1;
    return state;
}

// ---------------------------------------------------------------------------
// Value network

SpzNet::SpzNet(std::size_t inputs, std::size_t hidden, std::uint64_t seed)
    : inputs_(inputs), hidden_(hidden) {
    if (inputs == 0 || hidden == 0) {
        throw std::invalid_argument("SpzNet requires nonzero dimensions");
    }
    std::mt19937_64 random(seed);
    const double scale =
        1.0 / std::sqrt(static_cast<double>(inputs));
    std::uniform_real_distribution<double> hidden_init(-scale, scale);
    hidden_weights_.resize(hidden * inputs);
    for (double& weight : hidden_weights_) {
        weight = hidden_init(random);
    }
    hidden_bias_.assign(hidden, 0.0);
    const double output_scale =
        1.0 / std::sqrt(static_cast<double>(hidden));
    std::uniform_real_distribution<double> output_init(-output_scale,
                                                       output_scale);
    output_weights_.resize(hidden);
    for (double& weight : output_weights_) {
        weight = output_init(random);
    }
    output_bias_ = 0.0;
    momentum_.assign(hidden * inputs + hidden + hidden + 1, 0.0);
}

namespace {

double sigmoid(double value) {
    return 1.0 / (1.0 + std::exp(-value));
}

}  // namespace

double SpzNet::value(const std::vector<float>& features) const {
    if (features.size() != inputs_) {
        throw std::invalid_argument("SpzNet feature size mismatch");
    }
    double output = output_bias_;
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        double activation = hidden_bias_[unit];
        const double* row = hidden_weights_.data() + unit * inputs_;
        for (std::size_t input = 0; input < inputs_; ++input) {
            activation += row[input] * features[input];
        }
        output += output_weights_[unit] * std::tanh(activation);
    }
    return sigmoid(output);
}

double SpzNet::train_batch(
    const std::vector<const std::vector<float>*>& features,
    const std::vector<float>& targets, double learning_rate) {
    if (features.size() != targets.size() || features.empty()) {
        throw std::invalid_argument("SpzNet batch size mismatch");
    }
    const std::size_t weight_count = hidden_ * inputs_;
    std::vector<double> gradient(weight_count + hidden_ + hidden_ + 1, 0.0);
    std::vector<double> activations(hidden_, 0.0);
    double total_loss = 0.0;
    for (std::size_t example = 0; example < features.size(); ++example) {
        const std::vector<float>& row = *features[example];
        if (row.size() != inputs_) {
            throw std::invalid_argument("SpzNet feature size mismatch");
        }
        double output = output_bias_;
        for (std::size_t unit = 0; unit < hidden_; ++unit) {
            double activation = hidden_bias_[unit];
            const double* weights =
                hidden_weights_.data() + unit * inputs_;
            for (std::size_t input = 0; input < inputs_; ++input) {
                activation += weights[input] * row[input];
            }
            activations[unit] = std::tanh(activation);
            output += output_weights_[unit] * activations[unit];
        }
        const double prediction = sigmoid(output);
        const double target = targets[example];
        const double clamped =
            std::clamp(prediction, 1e-7, 1.0 - 1e-7);
        total_loss += -(target * std::log(clamped) +
                        (1.0 - target) * std::log(1.0 - clamped));
        const double output_delta = prediction - target;
        for (std::size_t unit = 0; unit < hidden_; ++unit) {
            gradient[weight_count + hidden_ + unit] +=
                output_delta * activations[unit];
            const double hidden_delta =
                output_delta * output_weights_[unit] *
                (1.0 - activations[unit] * activations[unit]);
            gradient[weight_count + unit] += hidden_delta;
            double* row_gradient = gradient.data() + unit * inputs_;
            for (std::size_t input = 0; input < inputs_; ++input) {
                row_gradient[input] += hidden_delta * row[input];
            }
        }
        gradient[weight_count + hidden_ + hidden_] += output_delta;
    }

    const double batch_scale = 1.0 / static_cast<double>(features.size());
    constexpr double kMomentum = 0.9;
    const auto apply = [&](std::size_t offset, double* parameter) {
        momentum_[offset] = kMomentum * momentum_[offset] -
                            learning_rate * gradient[offset] * batch_scale;
        *parameter += momentum_[offset];
    };
    for (std::size_t index = 0; index < weight_count; ++index) {
        apply(index, &hidden_weights_[index]);
    }
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        apply(weight_count + unit, &hidden_bias_[unit]);
    }
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        apply(weight_count + hidden_ + unit, &output_weights_[unit]);
    }
    apply(weight_count + hidden_ + hidden_, &output_bias_);
    return total_loss * batch_scale;
}

void SpzNet::save(std::ostream& out) const {
    out << "spz-net-v1\n" << inputs_ << ' ' << hidden_ << '\n';
    out << std::hexfloat;
    for (const double weight : hidden_weights_) {
        out << weight << '\n';
    }
    for (const double bias : hidden_bias_) {
        out << bias << '\n';
    }
    for (const double weight : output_weights_) {
        out << weight << '\n';
    }
    out << output_bias_ << '\n';
}

SpzNet SpzNet::load(std::istream& in) {
    std::string magic;
    in >> magic;
    if (magic != "spz-net-v1") {
        throw std::runtime_error("unrecognized SPZ net artifact header");
    }
    SpzNet net;
    in >> net.inputs_ >> net.hidden_;
    if (!in || net.inputs_ == 0 || net.hidden_ == 0) {
        throw std::runtime_error("malformed SPZ net dimensions");
    }
    const auto read_value = [&in]() {
        std::string token;
        in >> token;
        if (!in) {
            throw std::runtime_error("truncated SPZ net artifact");
        }
        return std::strtod(token.c_str(), nullptr);
    };
    net.hidden_weights_.resize(net.hidden_ * net.inputs_);
    for (double& weight : net.hidden_weights_) {
        weight = read_value();
    }
    net.hidden_bias_.resize(net.hidden_);
    for (double& bias : net.hidden_bias_) {
        bias = read_value();
    }
    net.output_weights_.resize(net.hidden_);
    for (double& weight : net.output_weights_) {
        weight = read_value();
    }
    net.output_bias_ = read_value();
    net.momentum_.assign(
        net.hidden_ * net.inputs_ + net.hidden_ + net.hidden_ + 1, 0.0);
    return net;
}

void save_spz_net(const SpzNet& net, const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open SPZ artifact for writing: " +
                                 path);
    }
    net.save(out);
    if (!out) {
        throw std::runtime_error("failed writing SPZ artifact: " + path);
    }
}

SpzNet load_spz_net(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open SPZ artifact: " + path);
    }
    return SpzNet::load(in);
}

// ---------------------------------------------------------------------------
// Agent

namespace {

struct SpzAgent {
    std::shared_ptr<const SpzNet> net;
    std::array<std::vector<CardId>, 2> decks;
    std::size_t seat = 0;
    SpzPolicyConfig config;
    SpzRecorder* recorder = nullptr;
    std::mt19937_64 rng;

    SpzAgent(std::shared_ptr<const SpzNet> shared_net,
             const std::array<std::vector<CardId>, 2>& original_decks,
             std::size_t player_seat, const SpzPolicyConfig& policy,
             SpzRecorder* sample_recorder)
        : net(std::move(shared_net)),
          decks(original_decks),
          seat(player_seat),
          config(policy),
          recorder(sample_recorder),
          rng(mix_seed(policy.seed, player_seat)) {}

    bool explore() {
        if (config.epsilon <= 0.0) {
            return false;
        }
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        return unit(rng) < config.epsilon;
    }

    // Terminal-aware value of a full state from `perspective`. Terminal
    // detection mirrors the engine's life/failed-draw rules so combat and
    // consequence simulations score wins exactly.
    double value_for(const GameState& state, std::size_t perspective,
                     TurnPhase phase) const {
        const bool self_dead = state.players[perspective].life <= 0 ||
                               state.failed_draw[perspective];
        const bool opponent_dead =
            state.players[1 - perspective].life <= 0 ||
            state.failed_draw[1 - perspective];
        if (self_dead || opponent_dead) {
            if (self_dead && opponent_dead) {
                return 0.5;
            }
            return self_dead ? 0.0 : 1.0;
        }
        return net->value(spz_features(
            observe_game_state(state, perspective), decks, phase));
    }

    // ------------------------------------------------------------------
    // Rollout lookahead: deterministic greedy forward play on a
    // determinized world until the start of `seat`'s next turn.

    static constexpr int kRolloutDecisionBudget = 300;

    std::optional<double> terminal_value(const GameState& state) const {
        const bool self_dead = state.players[seat].life <= 0 ||
                               state.failed_draw[seat];
        const bool opponent_dead = state.players[1 - seat].life <= 0 ||
                                   state.failed_draw[1 - seat];
        if (!self_dead && !opponent_dead) {
            return std::nullopt;
        }
        if (self_dead && opponent_dead) {
            return 0.5;
        }
        return self_dead ? 0.0 : 1.0;
    }

    PriorityAction choose_rollout_priority(
        const GameState& state, const PriorityState& priority,
        bool sorcery, TurnPhase phase) const {
        const auto actions =
            legal_priority_actions(state, priority.player, sorcery);
        if (actions.size() <= 1) {
            return actions.empty() ? PriorityAction::pass() : actions[0];
        }
        double best_value = -std::numeric_limits<double>::infinity();
        PriorityAction chosen = actions[0];
        for (const PriorityAction& action : actions) {
            const auto consequence = resolve_priority_action_consequence(
                state, priority.player, sorcery,
                std::min(priority.consecutive_passes, 1), action);
            if (!consequence.has_value()) {
                continue;
            }
            double value = 0.0;
            if (consequence->terminal) {
                value = consequence->winner == -1
                            ? 0.5
                            : (static_cast<std::size_t>(
                                   consequence->winner) ==
                                       priority.player
                                   ? 1.0
                                   : 0.0);
            } else {
                value = value_for(consequence->state, priority.player,
                                  phase);
            }
            if (value > best_value) {
                best_value = value;
                chosen = action;
            }
        }
        return chosen;
    }

    // Plays one priority window to completion with the greedy mirror
    // policy. Returns the terminal value when the game ends inside it.
    std::optional<double> rollout_window(GameState& state, TurnPhase phase,
                                         bool sorcery,
                                         PriorityState priority,
                                         int& budget) const {
        while (true) {
            PriorityAction action = PriorityAction::pass();
            if (budget > 0) {
                budget -= 1;
                action = choose_rollout_priority(state, priority, sorcery,
                                                 phase);
            }
            if (action.kind != PriorityActionKind::Pass &&
                !apply_priority_action(state, priority.player, action,
                                       sorcery)) {
                action = PriorityAction::pass();
            }
            if (action.kind == PriorityActionKind::Pass) {
                const PriorityPassResult pass =
                    pass_priority(state, priority);
                if (pass == PriorityPassResult::Passed) {
                    continue;
                }
                if (pass == PriorityPassResult::WindowEnded) {
                    return std::nullopt;
                }
                if (const auto terminal = terminal_value(state)) {
                    return terminal;
                }
                continue;
            }
            priority.consecutive_passes = 0;
        }
    }

    // Myopic greedy attack subset for the current world; shared by rollout
    // combat and root candidate generation.
    std::vector<PermanentId> greedy_attack_set(
        const GameState& state, std::size_t attacking_player,
        const std::vector<PermanentId>& eligible) const {
        const auto evaluate_set =
            [&](const std::vector<PermanentId>& attack_set) -> double {
            if (attack_set.empty()) {
                return value_for(state, attacking_player,
                                 TurnPhase::SecondMain);
            }
            const auto blocks = greedy_blocks(state, attacking_player,
                                              attack_set, nullptr);
            GameState simulation = state;
            if (!resolve_combat(simulation, attacking_player, attack_set,
                                blocks)) {
                return kIllegalScore;
            }
            return value_for(simulation, attacking_player,
                             TurnPhase::SecondMain);
        };
        std::vector<PermanentId> chosen;
        std::vector<PermanentId> remaining = eligible;
        double current = evaluate_set(chosen);
        while (!remaining.empty()) {
            double best_value = current + kImprovementMargin;
            std::size_t best_index = remaining.size();
            for (std::size_t index = 0; index < remaining.size();
                 ++index) {
                auto trial = chosen;
                trial.push_back(remaining[index]);
                const double trial_value = evaluate_set(trial);
                if (trial_value > best_value) {
                    best_value = trial_value;
                    best_index = index;
                }
            }
            if (best_index == remaining.size()) {
                break;
            }
            chosen.push_back(remaining[best_index]);
            remaining.erase(remaining.begin() +
                            static_cast<std::ptrdiff_t>(best_index));
            current = best_value;
        }
        return chosen;
    }

    std::optional<double> rollout_combat_after_beginning(
        GameState& state, int& budget) const {
        const std::size_t attacking_player = state.active_player;
        const auto eligible =
            old_school::legal_attackers(state, attacking_player);
        if (!eligible.empty()) {
            const auto attack_set =
                greedy_attack_set(state, attacking_player, eligible);
            if (!attack_set.empty()) {
                const auto blocks = greedy_blocks(
                    state, attacking_player, attack_set, nullptr);
                if (resolve_combat(state, attacking_player, attack_set,
                                   blocks)) {
                    if (const auto terminal = terminal_value(state)) {
                        return terminal;
                    }
                }
            }
        }
        return rollout_window(state, TurnPhase::EndCombat, false,
                              {state.active_player, 0}, budget);
    }

    std::optional<double> rollout_combat(GameState& state,
                                         int& budget) const {
        if (const auto terminal =
                rollout_window(state, TurnPhase::BeginCombat, false,
                               {state.active_player, 0}, budget)) {
            return terminal;
        }
        return rollout_combat_after_beginning(state, budget);
    }

    // Greedy cleanup discards evaluated on the full state for `player`.
    std::vector<std::size_t> greedy_discards(const GameState& state,
                                             std::size_t player,
                                             std::size_t excess) const {
        const auto& hand = state.players[player].hand;
        std::vector<std::size_t> chosen;
        std::vector<bool> discarded(hand.size(), false);
        for (std::size_t round = 0; round < excess && round < hand.size();
             ++round) {
            double best_value = -std::numeric_limits<double>::infinity();
            std::size_t best_index = hand.size();
            for (std::size_t index = 0; index < hand.size(); ++index) {
                if (discarded[index]) {
                    continue;
                }
                GameState trial = state;
                std::vector<CardId> remaining;
                for (std::size_t position = 0; position < hand.size();
                     ++position) {
                    if (!discarded[position] && position != index) {
                        remaining.push_back(hand[position]);
                    }
                }
                trial.players[player].hand = std::move(remaining);
                trial.players[player].graveyard.push_back(hand[index]);
                const double value =
                    value_for(trial, player, TurnPhase::SecondMain);
                if (value > best_value) {
                    best_value = value;
                    best_index = index;
                }
            }
            if (best_index == hand.size()) {
                break;
            }
            discarded[best_index] = true;
            chosen.push_back(best_index);
        }
        std::sort(chosen.begin(), chosen.end());
        return chosen;
    }

    void rollout_cleanup(GameState& state) const {
        const auto& hand = state.players[state.active_player].hand;
        std::vector<std::size_t> discards;
        if (hand.size() > kMaximumHandSize) {
            discards = greedy_discards(state, state.active_player,
                                       hand.size() - kMaximumHandSize);
        }
        cleanup_turn(state, state.active_player, discards);
    }

    // Completes the current turn from `entry_phase` (resuming its priority
    // window when applicable), then plays full turns until `seat` would
    // receive its next turn; evaluates the network at that boundary.
    // Combat-decision roots pass a combat phase and a state where combat
    // has already been resolved.
    double finish_turn_and_rollout(GameState state, TurnPhase entry_phase,
                                   PriorityState resume) const {
        if (const auto terminal = terminal_value(state)) {
            return *terminal;
        }
        int budget = kRolloutDecisionBudget;
        const auto run_second_main =
            [&](GameState& current) -> std::optional<double> {
            return rollout_window(current, TurnPhase::SecondMain, true,
                                  {current.active_player, 0}, budget);
        };
        std::optional<double> terminal;
        switch (entry_phase) {
            case TurnPhase::FirstMain:
                terminal = rollout_window(state, TurnPhase::FirstMain,
                                          true, resume, budget);
                if (!terminal) {
                    terminal = rollout_combat(state, budget);
                }
                if (!terminal) {
                    terminal = run_second_main(state);
                }
                break;
            case TurnPhase::BeginCombat:
                terminal = rollout_window(state, TurnPhase::BeginCombat,
                                          false, resume, budget);
                if (!terminal) {
                    terminal =
                        rollout_combat_after_beginning(state, budget);
                }
                if (!terminal) {
                    terminal = run_second_main(state);
                }
                break;
            case TurnPhase::EndCombat:
                terminal = rollout_window(state, TurnPhase::EndCombat,
                                          false, resume, budget);
                if (!terminal) {
                    terminal = run_second_main(state);
                }
                break;
            case TurnPhase::SecondMain:
                terminal = rollout_window(state, TurnPhase::SecondMain,
                                          true, resume, budget);
                break;
            default:
                // Combat decision roots: combat already resolved.
                terminal = rollout_window(state, TurnPhase::EndCombat,
                                          false,
                                          {state.active_player, 0},
                                          budget);
                if (!terminal) {
                    terminal = run_second_main(state);
                }
                break;
        }
        if (terminal) {
            return *terminal;
        }
        rollout_cleanup(state);

        // Time Walk chains permit a few consecutive turns by one player.
        for (int guard = 0; guard < 4; ++guard) {
            if (state.turn_number >= 500) {
                break;
            }
            state.turn_number += 1;
            advance_turn_player(state);
            begin_turn(state, state.active_player);
            auto& active = state.players[state.active_player];
            if (active.library.empty()) {
                return state.active_player == seat ? 0.0 : 1.0;
            }
            active.hand.push_back(active.library.back());
            active.library.pop_back();
            if (state.active_player == seat) {
                return value_for(state, seat, TurnPhase::FirstMain);
            }
            terminal = rollout_window(state, TurnPhase::FirstMain, true,
                                      {state.active_player, 0}, budget);
            if (!terminal) {
                terminal = rollout_combat(state, budget);
            }
            if (!terminal) {
                terminal = run_second_main(state);
            }
            if (terminal) {
                return *terminal;
            }
            rollout_cleanup(state);
        }
        return value_for(state, seat, TurnPhase::FirstMain);
    }

    void record(const PlayerObservation& observation, TurnPhase phase) {
        if (recorder != nullptr) {
            recorder->feature_rows.push_back(
                spz_features(observation, decks, phase));
        }
    }

    void record_state(const GameState& state, TurnPhase phase) {
        if (recorder != nullptr &&
            state.players[0].life > 0 && state.players[1].life > 0) {
            recorder->feature_rows.push_back(spz_features(
                observe_game_state(state, seat), decks, phase));
        }
    }

    std::size_t choose_priority_action(
        const PlayerObservation& observation, TurnPhase phase,
        const std::vector<PriorityAction>& actions) {
        record(observation, phase);
        if (actions.size() <= 1) {
            return 0;
        }
        const GameState reconstructed =
            reconstruct_observed_state(observation);
        const bool sorcery_actions =
            (phase == TurnPhase::FirstMain ||
             phase == TurnPhase::SecondMain) &&
            observation.active_player == observation.observer;
        // Rules-only prune: never take an action whose settled consequence
        // is the Pass consequence minus resources. Placeholder hidden zones
        // are safe here — any action that reveals hidden cards settles to a
        // different observation and is therefore retained.
        std::vector<bool> dominated(actions.size(), false);
        if (config.pass_dominance_prune) {
            const auto dominance = diagnose_value_pass_dominance(
                reconstructed, seat, sorcery_actions, phase, 0);
            for (const auto& entry : dominance.actions) {
                if (!entry.strictly_dominated_by_pass) {
                    continue;
                }
                for (std::size_t index = 0; index < actions.size();
                     ++index) {
                    if (actions[index] == entry.action) {
                        dominated[index] = true;
                    }
                }
            }
        }
        if (explore()) {
            std::vector<std::size_t> retained;
            for (std::size_t index = 0; index < actions.size();
                 ++index) {
                if (!dominated[index]) {
                    retained.push_back(index);
                }
            }
            if (retained.empty()) {
                return 0;
            }
            std::uniform_int_distribution<std::size_t> pick(
                0, retained.size() - 1);
            return retained[pick(rng)];
        }
        const std::size_t worlds = std::max<std::size_t>(1, config.worlds);
        std::vector<GameState> sampled_worlds;
        sampled_worlds.reserve(worlds);
        for (std::size_t world = 0; world < worlds; ++world) {
            sampled_worlds.push_back(sample_determinization(
                reconstructed, decks, seat, rng()));
        }
        const auto score_action =
            [&](const GameState& sampled, const PriorityAction& action,
                bool with_rollout) -> double {
            const auto consequence = resolve_priority_action_consequence(
                sampled, seat, sorcery_actions, 0, action);
            if (!consequence.has_value()) {
                return kIllegalScore;
            }
            if (consequence->terminal) {
                return consequence->winner == -1
                           ? 0.5
                           : (static_cast<std::size_t>(
                                  consequence->winner) == seat
                                  ? 1.0
                                  : 0.0);
            }
            if (with_rollout) {
                return finish_turn_and_rollout(consequence->state, phase,
                                               consequence->priority);
            }
            return value_for(consequence->state, seat, phase);
        };
        std::vector<double> totals(actions.size(), 0.0);
        for (std::size_t index = 0; index < actions.size(); ++index) {
            if (dominated[index]) {
                totals[index] =
                    kIllegalScore * static_cast<double>(worlds);
            }
        }
        for (const GameState& sampled : sampled_worlds) {
            for (std::size_t index = 0; index < actions.size(); ++index) {
                if (dominated[index]) {
                    continue;
                }
                totals[index] += score_action(sampled, actions[index],
                                              false);
            }
        }
        if (config.rollout) {
            // Rollout-rescore the myopically strongest candidates.
            std::vector<std::size_t> order(actions.size());
            for (std::size_t index = 0; index < order.size(); ++index) {
                order[index] = index;
            }
            std::stable_sort(order.begin(), order.end(),
                             [&](std::size_t left, std::size_t right) {
                                 return totals[left] > totals[right];
                             });
            const std::size_t candidates = std::min(
                std::max<std::size_t>(config.rollout_top_k, 2),
                order.size());
            std::vector<double> rollout_totals(actions.size(),
                                               kIllegalScore);
            for (std::size_t rank = 0; rank < candidates; ++rank) {
                const std::size_t index = order[rank];
                if (totals[index] <=
                    kIllegalScore * static_cast<double>(worlds) / 2.0) {
                    continue;
                }
                double total = 0.0;
                for (const GameState& sampled : sampled_worlds) {
                    total += score_action(sampled, actions[index], true);
                }
                rollout_totals[index] = total;
            }
            totals = std::move(rollout_totals);
        }
        const std::size_t best = static_cast<std::size_t>(
            std::max_element(totals.begin(), totals.end()) -
            totals.begin());
        if (recorder != nullptr) {
            const GameState sampled = sample_determinization(
                reconstructed, decks, seat, rng());
            const auto consequence = resolve_priority_action_consequence(
                sampled, seat, sorcery_actions, 0, actions[best]);
            if (consequence.has_value() && !consequence->terminal) {
                record_state(consequence->state, phase);
            }
        }
        return best;
    }

    // Greedy block assignment for `defender`, evaluated by resolving combat
    // on a copy of `state`. `restrictions` optionally limits each blocker to
    // its engine-provided legal attackers; resolve_combat itself rejects any
    // remaining illegal pairing.
    std::vector<std::pair<PermanentId, PermanentId>> greedy_blocks(
        const GameState& state, std::size_t attacking_player,
        const std::vector<PermanentId>& attackers,
        const std::vector<LegalBlockerChoice>* restrictions) const {
        const std::size_t defender = 1 - attacking_player;
        std::vector<std::pair<PermanentId, PermanentId>> blocks;

        std::vector<std::pair<PermanentId, std::vector<PermanentId>>>
            candidates;
        if (restrictions != nullptr) {
            for (const auto& choice : *restrictions) {
                candidates.emplace_back(choice.blocker,
                                        choice.legal_attackers);
            }
        } else {
            for (const auto& creature :
                 state.players[defender].creatures) {
                if (!creature.tapped) {
                    candidates.emplace_back(creature.id, attackers);
                }
            }
        }

        const auto combat_value =
            [&](const std::vector<std::pair<PermanentId, PermanentId>>&
                    trial) -> double {
            GameState simulation = state;
            if (!resolve_combat(simulation, attacking_player, attackers,
                                trial)) {
                return kIllegalScore;
            }
            return value_for(simulation, defender, TurnPhase::SecondMain);
        };

        double current = combat_value(blocks);
        std::vector<bool> assigned(candidates.size(), false);
        while (true) {
            double best_value = current + kImprovementMargin;
            std::size_t best_candidate = candidates.size();
            PermanentId best_attacker = 0;
            for (std::size_t index = 0; index < candidates.size();
                 ++index) {
                if (assigned[index]) {
                    continue;
                }
                for (const PermanentId attacker :
                     candidates[index].second) {
                    auto trial = blocks;
                    trial.emplace_back(attacker,
                                       candidates[index].first);
                    const double trial_value = combat_value(trial);
                    if (trial_value > best_value) {
                        best_value = trial_value;
                        best_candidate = index;
                        best_attacker = attacker;
                    }
                }
            }
            if (best_candidate == candidates.size()) {
                break;
            }
            blocks.emplace_back(best_attacker,
                                candidates[best_candidate].first);
            assigned[best_candidate] = true;
            current = best_value;
        }
        return blocks;
    }

    std::vector<PermanentId> choose_attackers(
        const PlayerObservation& observation,
        const std::vector<PermanentId>& eligible) {
        record(observation, TurnPhase::DeclareAttackers);
        if (eligible.empty()) {
            return {};
        }
        if (explore()) {
            std::vector<PermanentId> random_set;
            std::uniform_int_distribution<int> coin(0, 1);
            for (const PermanentId attacker : eligible) {
                if (coin(rng) == 1) {
                    random_set.push_back(attacker);
                }
            }
            return random_set;
        }
        const GameState reconstructed =
            reconstruct_observed_state(observation);
        const std::size_t worlds =
            std::max<std::size_t>(1, config.block_prediction_worlds);
        std::vector<GameState> sampled_worlds;
        sampled_worlds.reserve(worlds);
        for (std::size_t world = 0; world < worlds; ++world) {
            sampled_worlds.push_back(sample_determinization(
                reconstructed, decks, seat, rng()));
        }

        const auto evaluate_set =
            [&](const std::vector<PermanentId>& attack_set,
                bool with_rollout) -> double {
            double total = 0.0;
            for (const GameState& world : sampled_worlds) {
                GameState simulation = world;
                if (!attack_set.empty()) {
                    const auto predicted_blocks = greedy_blocks(
                        world, seat, attack_set, nullptr);
                    if (!resolve_combat(simulation, seat, attack_set,
                                        predicted_blocks)) {
                        return kIllegalScore;
                    }
                }
                if (with_rollout) {
                    total += finish_turn_and_rollout(
                        std::move(simulation),
                        TurnPhase::DeclareAttackers, {seat, 0});
                } else if (attack_set.empty()) {
                    total += value_for(world, seat,
                                       TurnPhase::SecondMain);
                } else {
                    total += value_for(simulation, seat,
                                       TurnPhase::SecondMain);
                }
            }
            return total / static_cast<double>(worlds);
        };

        if (config.rollout) {
            // Candidate sets around the myopic greedy set: none, greedy,
            // all-in, and single-creature edits of the greedy set.
            std::vector<std::vector<PermanentId>> candidates;
            const auto add_candidate =
                [&candidates](std::vector<PermanentId> candidate) {
                    std::sort(candidate.begin(), candidate.end());
                    if (std::find(candidates.begin(), candidates.end(),
                                  candidate) == candidates.end()) {
                        candidates.push_back(std::move(candidate));
                    }
                };
            add_candidate({});
            const auto greedy_set =
                greedy_attack_set(sampled_worlds.front(), seat, eligible);
            add_candidate(greedy_set);
            add_candidate(eligible);
            constexpr std::size_t kEditLimit = 3;
            std::size_t additions = 0;
            for (const PermanentId attacker : eligible) {
                if (additions >= kEditLimit) {
                    break;
                }
                if (std::find(greedy_set.begin(), greedy_set.end(),
                              attacker) == greedy_set.end()) {
                    auto extended = greedy_set;
                    extended.push_back(attacker);
                    add_candidate(std::move(extended));
                    additions += 1;
                }
            }
            std::size_t removals = 0;
            for (const PermanentId attacker : greedy_set) {
                if (removals >= kEditLimit) {
                    break;
                }
                std::vector<PermanentId> reduced;
                for (const PermanentId kept : greedy_set) {
                    if (kept != attacker) {
                        reduced.push_back(kept);
                    }
                }
                add_candidate(std::move(reduced));
                removals += 1;
            }
            double best_value =
                -std::numeric_limits<double>::infinity();
            std::size_t best_candidate = 0;
            for (std::size_t index = 0; index < candidates.size();
                 ++index) {
                const double value =
                    evaluate_set(candidates[index], true);
                if (value > best_value) {
                    best_value = value;
                    best_candidate = index;
                }
            }
            return candidates[best_candidate];
        }

        std::vector<PermanentId> chosen;
        std::vector<PermanentId> remaining = eligible;
        double current = evaluate_set(chosen, false);
        while (!remaining.empty()) {
            double best_value = current + kImprovementMargin;
            std::size_t best_index = remaining.size();
            for (std::size_t index = 0; index < remaining.size();
                 ++index) {
                auto trial = chosen;
                trial.push_back(remaining[index]);
                const double trial_value = evaluate_set(trial, false);
                if (trial_value > best_value) {
                    best_value = trial_value;
                    best_index = index;
                }
            }
            if (best_index == remaining.size()) {
                break;
            }
            chosen.push_back(remaining[best_index]);
            remaining.erase(remaining.begin() +
                            static_cast<std::ptrdiff_t>(best_index));
            current = best_value;
        }
        return chosen;
    }

    std::vector<std::pair<PermanentId, PermanentId>> choose_blockers(
        const PlayerObservation& observation,
        const std::vector<PermanentId>& attackers,
        const std::vector<LegalBlockerChoice>& choices) {
        record(observation, TurnPhase::DeclareBlockers);
        if (attackers.empty() || choices.empty()) {
            return {};
        }
        const GameState reconstructed =
            reconstruct_observed_state(observation);
        const std::size_t attacking_player = observation.active_player;
        if (explore()) {
            std::vector<std::pair<PermanentId, PermanentId>> random_blocks;
            for (const auto& choice : choices) {
                std::uniform_int_distribution<std::size_t> pick(
                    0, choice.legal_attackers.size());
                const std::size_t selection = pick(rng);
                if (selection < choice.legal_attackers.size()) {
                    random_blocks.emplace_back(
                        choice.legal_attackers[selection], choice.blocker);
                }
            }
            GameState simulation = reconstructed;
            if (resolve_combat(simulation, attacking_player, attackers,
                               random_blocks)) {
                return random_blocks;
            }
            return {};
        }
        if (config.rollout) {
            // Compare the myopic greedy assignment against declining to
            // block, judged by where the whole turn cycle actually lands.
            const auto greedy = greedy_blocks(
                reconstructed, attacking_player, attackers, &choices);
            std::vector<std::vector<std::pair<PermanentId, PermanentId>>>
                candidates;
            candidates.push_back(greedy);
            if (!greedy.empty()) {
                candidates.push_back({});
            }
            // Single-edit variants of the greedy assignment.
            constexpr std::size_t kBlockEditLimit = 3;
            std::size_t block_removals = 0;
            for (const auto& removed : greedy) {
                if (block_removals >= kBlockEditLimit) {
                    break;
                }
                std::vector<std::pair<PermanentId, PermanentId>> reduced;
                for (const auto& kept : greedy) {
                    if (kept != removed) {
                        reduced.push_back(kept);
                    }
                }
                candidates.push_back(std::move(reduced));
                block_removals += 1;
            }
            std::size_t block_additions = 0;
            for (const auto& choice : choices) {
                if (block_additions >= kBlockEditLimit) {
                    break;
                }
                const bool already_blocking = std::any_of(
                    greedy.begin(), greedy.end(),
                    [&choice](const auto& block) {
                        return block.second == choice.blocker;
                    });
                if (already_blocking || choice.legal_attackers.empty()) {
                    continue;
                }
                // Myopically best attacker for this extra blocker.
                double best_value =
                    -std::numeric_limits<double>::infinity();
                std::optional<PermanentId> best_attacker;
                for (const PermanentId attacker :
                     choice.legal_attackers) {
                    auto trial = greedy;
                    trial.emplace_back(attacker, choice.blocker);
                    GameState simulation = reconstructed;
                    if (!resolve_combat(simulation, attacking_player,
                                        attackers, trial)) {
                        continue;
                    }
                    const double value = value_for(
                        simulation, seat, TurnPhase::SecondMain);
                    if (value > best_value) {
                        best_value = value;
                        best_attacker = attacker;
                    }
                }
                if (best_attacker.has_value()) {
                    auto extended = greedy;
                    extended.emplace_back(*best_attacker, choice.blocker);
                    candidates.push_back(std::move(extended));
                    block_additions += 1;
                }
            }
            const std::size_t worlds = std::max<std::size_t>(
                1, config.block_prediction_worlds);
            std::vector<GameState> sampled_worlds;
            sampled_worlds.reserve(worlds);
            for (std::size_t world = 0; world < worlds; ++world) {
                sampled_worlds.push_back(sample_determinization(
                    reconstructed, decks, seat, rng()));
            }
            double best_value =
                -std::numeric_limits<double>::infinity();
            std::size_t best_candidate = 0;
            for (std::size_t index = 0; index < candidates.size();
                 ++index) {
                double total = 0.0;
                bool legal = true;
                for (const GameState& world : sampled_worlds) {
                    GameState simulation = world;
                    if (!resolve_combat(simulation, attacking_player,
                                        attackers,
                                        candidates[index])) {
                        legal = false;
                        break;
                    }
                    total += finish_turn_and_rollout(
                        std::move(simulation),
                        TurnPhase::DeclareBlockers,
                        {attacking_player, 0});
                }
                if (legal && total > best_value) {
                    best_value = total;
                    best_candidate = index;
                }
            }
            return candidates[best_candidate];
        }
        // Block evaluation reads only public zones and the observer's own
        // hand, so a single reconstructed world is exact.
        return greedy_blocks(reconstructed, attacking_player, attackers,
                             &choices);
    }

    std::vector<PermanentId> choose_damage_order(
        const PlayerObservation& observation, PermanentId attacker,
        const std::vector<PermanentId>& blockers) {
        record(observation, TurnPhase::DamageOrder);
        if (blockers.size() <= 1) {
            return blockers;
        }
        const GameState reconstructed =
            reconstruct_observed_state(observation);
        std::vector<PermanentId> order = blockers;
        std::sort(order.begin(), order.end());
        std::vector<PermanentId> best_order = blockers;
        double best_value = -std::numeric_limits<double>::infinity();
        std::size_t permutations = 0;
        constexpr std::size_t kPermutationLimit = 24;
        do {
            // Damage assignment is independent per attacker, so ranking by
            // this attacker's isolated combat is exact up to net curvature.
            GameState simulation = reconstructed;
            std::vector<std::pair<PermanentId, PermanentId>> blocks;
            blocks.reserve(order.size());
            for (const PermanentId blocker : order) {
                blocks.emplace_back(attacker, blocker);
            }
            if (resolve_combat(simulation, seat, {attacker}, blocks)) {
                const double value =
                    value_for(simulation, seat, TurnPhase::EndCombat);
                if (value > best_value) {
                    best_value = value;
                    best_order = order;
                }
            }
            permutations += 1;
        } while (permutations < kPermutationLimit &&
                 std::next_permutation(order.begin(), order.end()));
        return best_order;
    }

    std::vector<std::size_t> choose_cleanup_discards(
        const PlayerObservation& observation, std::size_t excess) {
        record(observation, TurnPhase::SecondMain);
        std::vector<std::size_t> chosen;
        if (excess == 0 || observation.hand.empty()) {
            return chosen;
        }
        const GameState reconstructed =
            reconstruct_observed_state(observation);
        std::vector<bool> discarded(observation.hand.size(), false);
        for (std::size_t round = 0;
             round < excess && round < observation.hand.size(); ++round) {
            double best_value =
                -std::numeric_limits<double>::infinity();
            std::size_t best_index = observation.hand.size();
            for (std::size_t index = 0; index < observation.hand.size();
                 ++index) {
                if (discarded[index]) {
                    continue;
                }
                GameState trial = reconstructed;
                std::vector<CardId> remaining_hand;
                for (std::size_t position = 0;
                     position < observation.hand.size(); ++position) {
                    if (!discarded[position] && position != index) {
                        remaining_hand.push_back(
                            observation.hand[position]);
                    }
                }
                trial.players[seat].hand = std::move(remaining_hand);
                trial.players[seat].graveyard.push_back(
                    observation.hand[index]);
                const double value =
                    value_for(trial, seat, TurnPhase::SecondMain);
                if (value > best_value) {
                    best_value = value;
                    best_index = index;
                }
            }
            if (best_index == observation.hand.size()) {
                break;
            }
            discarded[best_index] = true;
            chosen.push_back(best_index);
        }
        std::sort(chosen.begin(), chosen.end());
        return chosen;
    }
};

}  // namespace

HumanController make_spz_controller(
    std::shared_ptr<const SpzNet> net,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t seat, const SpzPolicyConfig& config,
    SpzRecorder* recorder) {
    auto agent = std::make_shared<SpzAgent>(std::move(net), original_decks,
                                            seat, config, recorder);
    HumanController controller;
    controller.choose_priority_action =
        [agent](const PlayerObservation& observation, TurnPhase phase,
                const std::vector<PriorityAction>& actions) {
            return agent->choose_priority_action(observation, phase,
                                                 actions);
        };
    controller.choose_attackers =
        [agent](const PlayerObservation& observation,
                const std::vector<PermanentId>& eligible) {
            return agent->choose_attackers(observation, eligible);
        };
    controller.choose_blockers =
        [agent](const PlayerObservation& observation,
                const std::vector<PermanentId>& attackers,
                const std::vector<LegalBlockerChoice>& choices) {
            return agent->choose_blockers(observation, attackers, choices);
        };
    controller.choose_damage_order =
        [agent](const PlayerObservation& observation, PermanentId attacker,
                const std::vector<PermanentId>& blockers) {
            return agent->choose_damage_order(observation, attacker,
                                              blockers);
        };
    controller.choose_cleanup_discards =
        [agent](const PlayerObservation& observation, std::size_t excess) {
            return agent->choose_cleanup_discards(observation, excess);
        };
    return controller;
}

// ---------------------------------------------------------------------------
// Training

namespace {

void run_indexed_jobs(std::size_t job_count, std::size_t threads,
                      const std::function<void(std::size_t)>& job) {
    const std::size_t worker_count =
        std::max<std::size_t>(1, std::min(threads, job_count));
    if (worker_count <= 1) {
        for (std::size_t index = 0; index < job_count; ++index) {
            job(index);
        }
        return;
    }
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&]() {
            while (true) {
                const std::size_t index =
                    next.fetch_add(1, std::memory_order_relaxed);
                if (index >= job_count) {
                    return;
                }
                job(index);
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
}

float outcome_target(const GameResult& result, std::size_t seat,
                     bool discounted) {
    if (discounted) {
        return static_cast<float>(
            learned_discounted_terminal_target(result, seat));
    }
    if (result.winner == -1) {
        return 0.5f;
    }
    return static_cast<std::size_t>(result.winner) == seat ? 1.0f : 0.0f;
}

}  // namespace

std::shared_ptr<SpzNet> train_spz(const SpzTrainConfig& config) {
    const auto& decks = spz_decks();
    auto net = config.initial_net != nullptr
                   ? std::make_shared<SpzNet>(*config.initial_net)
                   : std::make_shared<SpzNet>(spz_feature_count(),
                                              config.hidden, config.seed);
    std::vector<SpzSample> replay;
    replay.reserve(std::min<std::size_t>(config.replay_capacity, 1 << 20));
    std::size_t replay_cursor = 0;
    std::mt19937_64 trainer_rng(mix_seed(config.seed, 0xABCDEF));
    std::vector<std::shared_ptr<const SpzNet>> league_pool;

    struct GameRecord {
        SpzRecorder recorders[2];
        GameResult result;
        std::size_t turns = 0;
    };

    for (std::size_t iteration = 0; iteration < config.iterations;
         ++iteration) {
        const double progress =
            config.iterations <= 1
                ? 1.0
                : static_cast<double>(iteration) /
                      static_cast<double>(config.iterations - 1);
        const double epsilon =
            config.epsilon_start +
            (config.epsilon_final - config.epsilon_start) * progress;

        auto frozen = std::make_shared<const SpzNet>(*net);
        if (config.league_snapshot_interval > 0 &&
            iteration % config.league_snapshot_interval == 0) {
            league_pool.push_back(frozen);
            if (league_pool.size() > config.league_pool_size) {
                league_pool.erase(league_pool.begin());
            }
        }
        std::vector<GameRecord> records(config.games_per_iteration);
        run_indexed_jobs(
            config.games_per_iteration, config.threads,
            [&](std::size_t game_index) {
                std::mt19937_64 game_rng(mix_seed(
                    mix_seed(config.seed, iteration + 1), game_index));
                const std::size_t deck_zero = game_rng() % kSpzDeckCount;
                const std::size_t deck_one = game_rng() % kSpzDeckCount;
                const std::array<std::vector<CardId>, 2> game_decks = {
                    decks[deck_zero], decks[deck_one]};
                GameRecord& record = records[game_index];
                // League play: sometimes seat an earlier snapshot so the
                // learner keeps beating past selves, not only its mirror.
                std::array<std::shared_ptr<const SpzNet>, 2> seat_nets = {
                    frozen, frozen};
                std::array<bool, 2> record_seat = {true, true};
                std::uniform_real_distribution<double> unit(0.0, 1.0);
                if (!league_pool.empty() &&
                    unit(game_rng) < config.league_probability) {
                    const std::size_t snapshot =
                        game_rng() % league_pool.size();
                    const std::size_t league_seat = game_rng() % 2;
                    seat_nets[league_seat] = league_pool[snapshot];
                    // Snapshot trajectories are still true outcome-labeled
                    // observations, so both seats keep recording.
                }
                GameConfig game_config;
                game_config.max_turns = config.max_turns;
                for (std::size_t seat = 0; seat < 2; ++seat) {
                    SpzPolicyConfig policy;
                    policy.worlds = config.training_worlds;
                    policy.block_prediction_worlds = config.training_worlds;
                    policy.epsilon = epsilon;
                    policy.rollout = config.rollout;
                    policy.seed = game_rng();
                    game_config.human_controllers[seat] =
                        make_spz_controller(
                            seat_nets[seat], game_decks, seat, policy,
                            record_seat[seat] ? &record.recorders[seat]
                                              : nullptr);
                }
                Game game(game_decks[0], game_decks[1], game_rng(),
                          game_config);
                record.result = game.run();
                record.turns = record.result.turns;
            });

        std::size_t new_samples = 0;
        std::size_t decisive = 0;
        std::size_t total_turns = 0;
        for (const GameRecord& record : records) {
            decisive += record.result.winner == -1 ? 0 : 1;
            total_turns += record.turns;
            for (std::size_t seat = 0; seat < 2; ++seat) {
                const float target = outcome_target(
                    record.result, seat, config.discounted_targets);
                for (const auto& row :
                     record.recorders[seat].feature_rows) {
                    SpzSample sample{row, target};
                    if (replay.size() < config.replay_capacity) {
                        replay.push_back(std::move(sample));
                    } else {
                        replay[replay_cursor] = std::move(sample);
                        replay_cursor =
                            (replay_cursor + 1) % config.replay_capacity;
                    }
                    new_samples += 1;
                }
            }
        }

        double mean_loss = 0.0;
        std::size_t steps = 0;
        if (!replay.empty()) {
            const std::size_t batch =
                std::max<std::size_t>(1, config.batch_size);
            steps = static_cast<std::size_t>(
                config.replay_passes *
                static_cast<double>(new_samples) /
                static_cast<double>(batch));
            steps = std::max<std::size_t>(steps, 1);
            std::uniform_int_distribution<std::size_t> pick(
                0, replay.size() - 1);
            std::vector<const std::vector<float>*> batch_features(batch);
            std::vector<float> batch_targets(batch);
            for (std::size_t step = 0; step < steps; ++step) {
                for (std::size_t slot = 0; slot < batch; ++slot) {
                    const SpzSample& sample = replay[pick(trainer_rng)];
                    batch_features[slot] = &sample.features;
                    batch_targets[slot] = sample.target;
                }
                mean_loss += net->train_batch(batch_features,
                                              batch_targets,
                                              config.learning_rate);
            }
            mean_loss /= static_cast<double>(steps);
        }

        if (config.log) {
            std::ostringstream line;
            line << "iteration " << (iteration + 1) << '/'
                 << config.iterations << " epsilon " << std::fixed
                 << std::setprecision(3) << epsilon << " games "
                 << config.games_per_iteration << " decisive "
                 << decisive << " avg-turns "
                 << (config.games_per_iteration == 0
                         ? 0.0
                         : static_cast<double>(total_turns) /
                               static_cast<double>(
                                   config.games_per_iteration))
                 << " new-samples " << new_samples << " replay "
                 << replay.size() << " steps " << steps << " loss "
                 << std::setprecision(4) << mean_loss;
            config.log(line.str());
        }
        if (!config.checkpoint_prefix.empty() &&
            config.checkpoint_interval > 0 &&
            (iteration + 1) % config.checkpoint_interval == 0) {
            save_spz_net(*net, config.checkpoint_prefix +
                                   std::to_string(iteration + 1) +
                                   ".txt");
        }
    }
    return net;
}

// ---------------------------------------------------------------------------
// Benchmark

double SpzDeckStats::win_rate() const {
    if (games == 0) {
        return 0.0;
    }
    return (static_cast<double>(wins) + 0.5 * static_cast<double>(draws)) /
           static_cast<double>(games);
}

double SpzBenchmarkResult::baseline_deck_win_rate(std::size_t deck) const {
    std::size_t games = 0;
    double baseline_score = 0.0;
    for (std::size_t spz_deck = 0; spz_deck < kSpzDeckCount; ++spz_deck) {
        const SpzDeckStats& stats = matchups[spz_deck][deck];
        games += stats.games;
        baseline_score += static_cast<double>(stats.losses) +
                          0.5 * static_cast<double>(stats.draws);
    }
    return games == 0 ? 0.0
                      : baseline_score / static_cast<double>(games);
}

namespace {

double wilson_lower_bound(double successes, double games) {
    if (games <= 0.0) {
        return 0.0;
    }
    constexpr double z = 1.959963984540054;
    const double phat = successes / games;
    const double z2 = z * z;
    const double denominator = 1.0 + z2 / games;
    const double center = phat + z2 / (2.0 * games);
    const double margin =
        z * std::sqrt(phat * (1.0 - phat) / games +
                      z2 / (4.0 * games * games));
    return (center - margin) / denominator;
}

}  // namespace

SpzBenchmarkResult run_spz_benchmark(
    std::shared_ptr<const SpzNet> net, BotKind baseline,
    std::size_t repetitions_per_pairing, std::uint64_t seed,
    const SpzPolicyConfig& policy, std::size_t max_turns,
    std::size_t threads,
    const std::function<void(const std::string&)>& log,
    std::shared_ptr<const LearnedModel> baseline_learned_model,
    std::size_t baseline_learned_rollouts) {
    if (baseline == BotKind::Learned &&
        baseline_learned_model == nullptr) {
        throw std::invalid_argument(
            "a Learned baseline requires a frozen model artifact");
    }
    const auto& decks = spz_decks();

    struct Job {
        std::size_t spz_deck = 0;
        std::size_t opponent_deck = 0;
        std::size_t repetition = 0;
    };
    std::vector<Job> jobs;
    for (std::size_t spz_deck = 0; spz_deck < kSpzDeckCount; ++spz_deck) {
        for (std::size_t opponent_deck = 0;
             opponent_deck < kSpzDeckCount; ++opponent_deck) {
            for (std::size_t repetition = 0;
                 repetition < repetitions_per_pairing; ++repetition) {
                jobs.push_back({spz_deck, opponent_deck, repetition});
            }
        }
    }

    struct JobOutcome {
        // Two seat-swapped games: 1 win, 0 loss, draws counted separately.
        std::array<int, 2> spz_wins = {0, 0};
        std::array<int, 2> draws = {0, 0};
    };
    std::vector<JobOutcome> outcomes(jobs.size());
    std::atomic<std::size_t> completed{0};

    run_indexed_jobs(jobs.size(), threads, [&](std::size_t job_index) {
        const Job& job = jobs[job_index];
        const std::uint64_t pairing_seed = mix_seed(
            mix_seed(mix_seed(seed, job.spz_deck * kSpzDeckCount +
                                        job.opponent_deck),
                     job.repetition),
            0x51AB);
        const bool spz_on_play = job.repetition % 2 == 0;
        for (std::size_t game_index = 0; game_index < 2; ++game_index) {
            const std::size_t spz_seat = game_index;
            const std::size_t baseline_seat = 1 - spz_seat;
            GameConfig game_config;
            game_config.max_turns = max_turns;
            game_config.starting_player =
                spz_on_play ? spz_seat : baseline_seat;
            game_config.bots[baseline_seat].kind = baseline;
            if (baseline == BotKind::Learned) {
                game_config.bots[baseline_seat].learned_variant =
                    LearnedVariant::ValueSearchChampion;
                game_config.bots[baseline_seat].rollouts_per_action =
                    baseline_learned_rollouts;
                game_config.bots[baseline_seat].learned_model =
                    baseline_learned_model;
                game_config.learned_model = baseline_learned_model;
            }
            std::array<std::vector<CardId>, 2> game_decks;
            game_decks[spz_seat] = decks[job.spz_deck];
            game_decks[baseline_seat] = decks[job.opponent_deck];
            SpzPolicyConfig game_policy = policy;
            game_policy.epsilon = 0.0;
            game_policy.seed =
                mix_seed(pairing_seed, 100 + game_index);
            game_config.human_controllers[spz_seat] =
                make_spz_controller(net, game_decks, spz_seat,
                                    game_policy);
            Game game(game_decks[0], game_decks[1], pairing_seed,
                      game_config);
            const GameResult result = game.run();
            if (result.winner == -1) {
                outcomes[job_index].draws[game_index] = 1;
            } else if (static_cast<std::size_t>(result.winner) ==
                       spz_seat) {
                outcomes[job_index].spz_wins[game_index] = 1;
            }
        }
        const std::size_t done =
            completed.fetch_add(1, std::memory_order_relaxed) + 1;
        if (log && done % 25 == 0) {
            std::ostringstream line;
            line << "benchmark " << done << '/' << jobs.size()
                 << " pairings";
            log(line.str());
        }
    });

    SpzBenchmarkResult result;
    for (std::size_t job_index = 0; job_index < jobs.size(); ++job_index) {
        const Job& job = jobs[job_index];
        const JobOutcome& outcome = outcomes[job_index];
        for (std::size_t game_index = 0; game_index < 2; ++game_index) {
            SpzDeckStats& deck_stats = result.per_deck[job.spz_deck];
            SpzDeckStats& matchup_stats =
                result.matchups[job.spz_deck][job.opponent_deck];
            deck_stats.games += 1;
            matchup_stats.games += 1;
            result.aggregate.games += 1;
            if (outcome.spz_wins[game_index] == 1) {
                deck_stats.wins += 1;
                matchup_stats.wins += 1;
                result.aggregate.wins += 1;
            } else if (outcome.draws[game_index] == 1) {
                deck_stats.draws += 1;
                matchup_stats.draws += 1;
                result.aggregate.draws += 1;
            } else {
                deck_stats.losses += 1;
                matchup_stats.losses += 1;
                result.aggregate.losses += 1;
            }
        }
    }
    result.wilson_lower_bound_95 = wilson_lower_bound(
        static_cast<double>(result.aggregate.wins) +
            0.5 * static_cast<double>(result.aggregate.draws),
        static_cast<double>(result.aggregate.games));
    return result;
}

}  // namespace old_school::selfplay_zero
