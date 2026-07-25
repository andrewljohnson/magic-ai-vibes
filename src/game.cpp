#include "alpha/game.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace alpha {

constexpr std::size_t kLearnedCardCount =
    static_cast<std::size_t>(CardId::Moat) + 1;

class LearnedModel {
  public:
    static constexpr std::size_t kScalarFeatureCount = 22;
    static constexpr std::size_t kCardPlanes = 14;
    static constexpr std::size_t kFeatureCount =
        kScalarFeatureCount + kCardPlanes * kLearnedCardCount;
    static constexpr std::size_t kHiddenCount = 16;
    using FeatureVector = std::array<double, kFeatureCount>;
    using TrainingExample = std::pair<FeatureVector, double>;

    explicit LearnedModel(std::uint64_t seed) {
        std::mt19937_64 random(seed);
        std::normal_distribution<double> initialize(0.0, 0.12);
        for (auto& hidden_weights : input_weights_) {
            for (double& weight : hidden_weights) {
                weight = initialize(random);
            }
        }
        for (double& weight : output_weights_) {
            weight = initialize(random);
        }
    }

    explicit LearnedModel(
        std::vector<std::shared_ptr<const LearnedModel>> members)
        : ensemble_(std::move(members)) {
        if (ensemble_.empty()) {
            throw std::invalid_argument(
                "learned ensemble requires at least one member");
        }
    }

    double predict(const FeatureVector& features) const {
        if (!ensemble_.empty()) {
            double total = 0.0;
            for (const auto& member : ensemble_) {
                total += member->predict(features);
            }
            return total /
                   static_cast<double>(ensemble_.size());
        }
        const auto hidden = hidden_values(features);
        double output = output_bias_;
        for (std::size_t index = 0; index < hidden.size(); ++index) {
            output += output_weights_[index] * hidden[index];
        }
        for (std::size_t feature = 0; feature < features.size();
             ++feature) {
            if (features[feature] != 0.0) {
                output += direct_output_weights_[feature] *
                          features[feature];
            }
        }
        return 1.0 / (1.0 + std::exp(-output));
    }

    void train(const std::vector<TrainingExample>& examples,
               std::size_t epochs, double learning_rate,
               std::uint64_t seed) {
        if (!ensemble_.empty()) {
            throw std::logic_error(
                "cannot train a composite learned ensemble");
        }
        std::mt19937_64 random(seed);
        std::vector<std::size_t> order(examples.size());
        for (std::size_t index = 0; index < order.size(); ++index) {
            order[index] = index;
        }
        for (std::size_t epoch = 0; epoch < epochs; ++epoch) {
            std::shuffle(order.begin(), order.end(), random);
            const double rate =
                learning_rate /
                (1.0 + 0.15 * static_cast<double>(epoch));
            for (const std::size_t example_index : order) {
                const auto& [features, target] =
                    examples[example_index];
                const auto hidden = hidden_values(features);
                double output_sum = output_bias_;
                for (std::size_t index = 0; index < hidden.size();
                     ++index) {
                    output_sum +=
                        output_weights_[index] * hidden[index];
                }
                for (std::size_t feature = 0;
                     feature < features.size(); ++feature) {
                    if (features[feature] != 0.0) {
                        output_sum +=
                            direct_output_weights_[feature] *
                            features[feature];
                    }
                }
                const double output =
                    1.0 / (1.0 + std::exp(-output_sum));
                // Sigmoid + cross entropy derivative.
                const double output_error = output - target;
                const auto old_output_weights = output_weights_;

                for (std::size_t hidden_index = 0;
                     hidden_index < kHiddenCount; ++hidden_index) {
                    output_weights_[hidden_index] -=
                        rate * output_error * hidden[hidden_index];
                }
                for (std::size_t feature = 0;
                     feature < features.size(); ++feature) {
                    if (features[feature] != 0.0) {
                        direct_output_weights_[feature] -=
                            rate * output_error *
                            features[feature];
                    }
                }
                output_bias_ -= rate * output_error;

                for (std::size_t hidden_index = 0;
                     hidden_index < kHiddenCount; ++hidden_index) {
                    const double hidden_error =
                        output_error *
                        old_output_weights[hidden_index] *
                        (1.0 - hidden[hidden_index] *
                                   hidden[hidden_index]);
                    for (std::size_t feature = 0;
                         feature < kFeatureCount; ++feature) {
                        if (features[feature] != 0.0) {
                            input_weights_[hidden_index][feature] -=
                                rate * hidden_error *
                                features[feature];
                        }
                    }
                    hidden_biases_[hidden_index] -= rate * hidden_error;
                }
            }
        }
    }

  private:
    std::array<double, kHiddenCount>
    hidden_values(const FeatureVector& features) const {
        std::array<double, kHiddenCount> hidden;
        for (std::size_t hidden_index = 0;
             hidden_index < kHiddenCount; ++hidden_index) {
            double sum = hidden_biases_[hidden_index];
            for (std::size_t feature = 0; feature < kFeatureCount;
                 ++feature) {
                if (features[feature] != 0.0) {
                    sum += input_weights_[hidden_index][feature] *
                           features[feature];
                }
            }
            hidden[hidden_index] = std::tanh(sum);
        }
        return hidden;
    }

    std::array<std::array<double, kFeatureCount>, kHiddenCount>
        input_weights_{};
    std::array<double, kHiddenCount> hidden_biases_{};
    std::array<double, kHiddenCount> output_weights_{};
    std::array<double, kFeatureCount> direct_output_weights_{};
    double output_bias_ = 0.0;
    std::vector<std::shared_ptr<const LearnedModel>> ensemble_;
};

namespace {

constexpr std::array<CardDefinition, 13> kCardDefinitions = {{
    {CardId::Forest, "Forest", CardType::Land, {}, 0, 0, 0},
    {CardId::Mountain, "Mountain", CardType::Land, {}, 0, 0, 0},
    {CardId::GrizzlyBears,
     "Grizzly Bears",
     CardType::Creature,
     {.generic = 1, .green = 1, .red = 0},
     2,
     2,
     0},
    {CardId::LightningBolt,
     "Lightning Bolt",
     CardType::Instant,
     {.generic = 0, .green = 0, .red = 1},
     0,
     0,
     3},
    {CardId::IronrootTreefolk,
     "Ironroot Treefolk",
     CardType::Creature,
     {.generic = 4, .green = 1, .red = 0},
     3,
     5,
     0},
    {CardId::FireElemental,
     "Fire Elemental",
     CardType::Creature,
     {.generic = 3, .green = 0, .red = 2},
     5,
     4,
     0},
    {CardId::Island, "Island", CardType::Land, {}, 0, 0, 0},
    {CardId::Counterspell,
     "Counterspell",
     CardType::Instant,
     {.generic = 0, .green = 0, .red = 0, .blue = 2},
     0,
     0,
     0},
    {CardId::WaterElemental,
     "Water Elemental",
     CardType::Creature,
     {.generic = 3, .green = 0, .red = 0, .blue = 2},
     5,
     4,
     0},
    {CardId::Tsunami,
     "Tsunami",
     CardType::Sorcery,
     {.generic = 3, .green = 1, .red = 0, .blue = 0},
     0,
     0,
     0},
    {CardId::Plains, "Plains", CardType::Land, {}, 0, 0, 0},
    {CardId::Millstone,
     "Millstone",
     CardType::Artifact,
     {.generic = 2},
     0,
     0,
     0},
    {CardId::Moat,
     "Moat",
     CardType::Enchantment,
     {.generic = 2,
      .green = 0,
      .red = 0,
      .blue = 0,
      .white = 2},
     0,
     0,
     0},
}};

constexpr std::array<CardId, 4> kCreatureCards = {
    CardId::GrizzlyBears,
    CardId::IronrootTreefolk,
    CardId::FireElemental,
    CardId::WaterElemental,
};

constexpr std::array<CardId, 1> kSorceryCards = {
    CardId::Tsunami,
};

constexpr std::array<CardId, 1> kArtifactCards = {
    CardId::Millstone,
};

constexpr std::array<CardId, 1> kEnchantmentCards = {
    CardId::Moat,
};

constexpr ManaCost kMillstoneActivationCost = {.generic = 2};

bool has_card(const std::vector<CardId>& cards, CardId wanted) {
    return std::find(cards.begin(), cards.end(), wanted) != cards.end();
}

bool remove_card(std::vector<CardId>& cards, CardId wanted) {
    const auto position = std::find(cards.begin(), cards.end(), wanted);
    if (position == cards.end()) {
        return false;
    }
    cards.erase(position);
    return true;
}

bool is_land(CardId card) {
    return card == CardId::Forest || card == CardId::Mountain ||
           card == CardId::Island || card == CardId::Plains;
}

bool can_pay(const PlayerState& player, const ManaCost& cost) {
    int forests = 0;
    int mountains = 0;
    int islands = 0;
    int plains = 0;
    int total = 0;
    for (const auto& land : player.lands) {
        if (land.tapped) {
            continue;
        }
        ++total;
        if (land.card == CardId::Forest) {
            ++forests;
        } else if (land.card == CardId::Mountain) {
            ++mountains;
        } else if (land.card == CardId::Island) {
            ++islands;
        } else if (land.card == CardId::Plains) {
            ++plains;
        }
    }

    if (forests < cost.green || mountains < cost.red ||
        islands < cost.blue || plains < cost.white) {
        return false;
    }
    return total >= cost.green + cost.red + cost.blue + cost.white +
                        cost.generic;
}

bool pay_mana(PlayerState& player, const ManaCost& cost) {
    if (!can_pay(player, cost)) {
        return false;
    }

    std::vector<bool> selected(player.lands.size(), false);
    auto select_colored = [&](CardId land_type, int amount) {
        for (std::size_t index = 0;
             index < player.lands.size() && amount > 0; ++index) {
            if (!player.lands[index].tapped &&
                player.lands[index].card == land_type) {
                selected[index] = true;
                --amount;
            }
        }
    };

    select_colored(CardId::Forest, cost.green);
    select_colored(CardId::Mountain, cost.red);
    select_colored(CardId::Island, cost.blue);
    select_colored(CardId::Plains, cost.white);

    int generic_remaining = cost.generic;
    for (std::size_t index = 0;
         index < player.lands.size() && generic_remaining > 0; ++index) {
        if (!player.lands[index].tapped && !selected[index]) {
            selected[index] = true;
            --generic_remaining;
        }
    }

    for (std::size_t index = 0; index < player.lands.size(); ++index) {
        if (selected[index]) {
            player.lands[index].tapped = true;
        }
    }
    return true;
}

CreaturePermanent* find_creature(PlayerState& player, PermanentId id) {
    const auto position = std::find_if(
        player.creatures.begin(), player.creatures.end(),
        [id](const CreaturePermanent& creature) { return creature.id == id; });
    return position == player.creatures.end() ? nullptr : &*position;
}

ArtifactPermanent* find_artifact(PlayerState& player, PermanentId id) {
    const auto position = std::find_if(
        player.artifacts.begin(), player.artifacts.end(),
        [id](const ArtifactPermanent& artifact) {
            return artifact.id == id;
        });
    return position == player.artifacts.end() ? nullptr : &*position;
}

bool moat_on_battlefield(const GameState& state) {
    return std::any_of(
        state.players.begin(), state.players.end(),
        [](const PlayerState& player) {
            return std::find(player.enchantments.begin(),
                             player.enchantments.end(),
                             CardId::Moat) != player.enchantments.end();
        });
}

bool can_attack_through_moat(const GameState& state,
                             const CreaturePermanent& creature) {
    return !moat_on_battlefield(state) ||
           card_definition(creature.card).flying;
}

void remove_dead_creatures(PlayerState& player) {
    auto creature = player.creatures.begin();
    while (creature != player.creatures.end()) {
        const auto& definition = card_definition(creature->card);
        if (creature->damage >= definition.toughness) {
            player.graveyard.push_back(creature->card);
            creature = player.creatures.erase(creature);
        } else {
            ++creature;
        }
    }
}

bool contains_action(const std::vector<PriorityAction>& actions,
                     const PriorityAction& wanted) {
    return std::find(actions.begin(), actions.end(), wanted) != actions.end();
}

std::size_t opponent_of(std::size_t player) {
    return 1 - player;
}

double handcrafted_card_value(CardId card) {
    switch (card) {
    case CardId::Forest:
    case CardId::Mountain:
    case CardId::Island:
    case CardId::Plains:
        return 100.0;
    case CardId::GrizzlyBears:
        return 400.0;
    case CardId::IronrootTreefolk:
        return 700.0;
    case CardId::FireElemental:
    case CardId::WaterElemental:
        return 900.0;
    case CardId::LightningBolt:
        return 800.0;
    case CardId::Counterspell:
        return 1'000.0;
    case CardId::Tsunami:
        return 700.0;
    case CardId::Millstone:
        return 1'100.0;
    case CardId::Moat:
        return 1'300.0;
    }
    return 0.0;
}

LearnedModel::FeatureVector learned_features(const GameState& state,
                                             std::size_t perspective) {
    const std::size_t opponent = opponent_of(perspective);
    const auto& self = state.players[perspective];
    const auto& enemy = state.players[opponent];
    const auto untapped_lands = [](const PlayerState& player) {
        return std::count_if(
            player.lands.begin(), player.lands.end(),
            [](const LandPermanent& land) { return !land.tapped; });
    };
    const auto total_power = [](const PlayerState& player) {
        int total = 0;
        for (const auto& creature : player.creatures) {
            total += card_definition(creature.card).power;
        }
        return total;
    };
    const auto total_toughness = [](const PlayerState& player) {
        int total = 0;
        for (const auto& creature : player.creatures) {
            total += std::max(
                0, card_definition(creature.card).toughness -
                       creature.damage);
        }
        return total;
    };
    const auto available_power = [](const PlayerState& player) {
        int total = 0;
        for (const auto& creature : player.creatures) {
            if (!creature.tapped && !creature.summoning_sick) {
                total += card_definition(creature.card).power;
            }
        }
        return total;
    };
    const auto permanent_count = [](const PlayerState& player) {
        return player.artifacts.size() + player.enchantments.size();
    };
    int stack_advantage = 0;
    for (const auto& object : state.stack) {
        stack_advantage += object.controller == perspective ? 1 : -1;
    }
    LearnedModel::FeatureVector features = {
        static_cast<double>(self.life) / 20.0,
        static_cast<double>(enemy.life) / 20.0,
        static_cast<double>(self.library.size()) / 40.0,
        static_cast<double>(enemy.library.size()) / 40.0,
        static_cast<double>(self.hand.size()) / 10.0,
        static_cast<double>(enemy.hand.size()) / 10.0,
        static_cast<double>(self.lands.size()) / 10.0,
        static_cast<double>(enemy.lands.size()) / 10.0,
        static_cast<double>(untapped_lands(self)) / 10.0,
        static_cast<double>(untapped_lands(enemy)) / 10.0,
        static_cast<double>(self.creatures.size()) / 10.0,
        static_cast<double>(enemy.creatures.size()) / 10.0,
        static_cast<double>(total_power(self)) / 20.0,
        static_cast<double>(total_power(enemy)) / 20.0,
        static_cast<double>(total_toughness(self)) / 20.0,
        static_cast<double>(total_toughness(enemy)) / 20.0,
        static_cast<double>(available_power(self)) / 20.0,
        static_cast<double>(available_power(enemy)) / 20.0,
        static_cast<double>(permanent_count(self)) / 5.0,
        static_cast<double>(permanent_count(enemy)) / 5.0,
        static_cast<double>(state.stack.size()) / 5.0,
        static_cast<double>(stack_advantage) / 5.0 +
            (state.active_player == perspective ? 0.25 : -0.25) +
            std::min(1.0, static_cast<double>(state.turn_number) / 80.0) *
                0.1,
    };

    using CardPlane = std::array<double, kLearnedCardCount>;
    CardPlane own_library{};
    CardPlane own_hand{};
    CardPlane own_battlefield{};
    CardPlane enemy_battlefield{};
    CardPlane own_tapped{};
    CardPlane enemy_tapped{};
    CardPlane own_summoning_sick{};
    CardPlane enemy_summoning_sick{};
    CardPlane own_creature_damage{};
    CardPlane enemy_creature_damage{};
    CardPlane own_graveyard{};
    CardPlane enemy_graveyard{};
    CardPlane own_stack{};
    CardPlane enemy_stack{};
    const auto card_index = [](CardId card) {
        return static_cast<std::size_t>(card);
    };
    for (const CardId card : self.library) {
        ++own_library[card_index(card)];
    }
    for (const CardId card : self.hand) {
        ++own_hand[card_index(card)];
    }
    const auto add_battlefield =
        [&](const PlayerState& player, CardPlane& battlefield,
            CardPlane& tapped, CardPlane& summoning_sick,
            CardPlane& creature_damage) {
            for (const auto& land : player.lands) {
                ++battlefield[card_index(land.card)];
                if (land.tapped) {
                    ++tapped[card_index(land.card)];
                }
            }
            for (const auto& creature : player.creatures) {
                ++battlefield[card_index(creature.card)];
                if (creature.tapped) {
                    ++tapped[card_index(creature.card)];
                }
                if (creature.summoning_sick) {
                    ++summoning_sick[card_index(creature.card)];
                }
                creature_damage[card_index(creature.card)] +=
                    static_cast<double>(creature.damage);
            }
            for (const auto& artifact : player.artifacts) {
                ++battlefield[card_index(artifact.card)];
                if (artifact.tapped) {
                    ++tapped[card_index(artifact.card)];
                }
            }
            for (const CardId enchantment : player.enchantments) {
                ++battlefield[card_index(enchantment)];
            }
        };
    add_battlefield(self, own_battlefield, own_tapped,
                    own_summoning_sick, own_creature_damage);
    add_battlefield(enemy, enemy_battlefield, enemy_tapped,
                    enemy_summoning_sick, enemy_creature_damage);
    for (const CardId card : self.graveyard) {
        ++own_graveyard[card_index(card)];
    }
    for (const CardId card : enemy.graveyard) {
        ++enemy_graveyard[card_index(card)];
    }
    for (const auto& object : state.stack) {
        auto& plane =
            object.controller == perspective ? own_stack : enemy_stack;
        ++plane[card_index(object.card)];
    }

    std::size_t feature = LearnedModel::kScalarFeatureCount;
    const auto append_plane =
        [&](const CardPlane& plane, double normalization) {
            for (const double value : plane) {
                features[feature++] = value / normalization;
            }
        };
    append_plane(own_library, 20.0);
    append_plane(own_hand, 10.0);
    append_plane(own_battlefield, 10.0);
    append_plane(enemy_battlefield, 10.0);
    append_plane(own_tapped, 10.0);
    append_plane(enemy_tapped, 10.0);
    append_plane(own_summoning_sick, 10.0);
    append_plane(enemy_summoning_sick, 10.0);
    append_plane(own_creature_damage, 10.0);
    append_plane(enemy_creature_damage, 10.0);
    append_plane(own_graveyard, 20.0);
    append_plane(enemy_graveyard, 20.0);
    append_plane(own_stack, 5.0);
    append_plane(enemy_stack, 5.0);
    return features;
}

std::vector<std::vector<PermanentId>> learned_attack_candidates(
    const std::vector<PermanentId>& legal_attackers,
    std::mt19937_64& random) {
    constexpr std::size_t kExhaustiveLimit = 256;
    std::vector<std::vector<PermanentId>> candidates;
    if (legal_attackers.size() < 63 &&
        (std::uint64_t{1} << legal_attackers.size()) <=
            kExhaustiveLimit) {
        const std::uint64_t combinations =
            std::uint64_t{1} << legal_attackers.size();
        candidates.reserve(static_cast<std::size_t>(combinations));
        for (std::uint64_t mask = 0; mask < combinations; ++mask) {
            std::vector<PermanentId> candidate;
            for (std::size_t index = 0;
                 index < legal_attackers.size(); ++index) {
                if ((mask & (std::uint64_t{1} << index)) != 0) {
                    candidate.push_back(legal_attackers[index]);
                }
            }
            candidates.push_back(std::move(candidate));
        }
        return candidates;
    }

    candidates = {{}, legal_attackers};
    for (const PermanentId attacker : legal_attackers) {
        candidates.push_back({attacker});
    }
    std::uniform_int_distribution<int> include_attacker(0, 1);
    constexpr int kRandomCandidates = 48;
    for (int sample = 0; sample < kRandomCandidates; ++sample) {
        std::vector<PermanentId> candidate;
        for (const PermanentId attacker : legal_attackers) {
            if (include_attacker(random) == 1) {
                candidate.push_back(attacker);
            }
        }
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

std::vector<std::vector<std::pair<PermanentId, PermanentId>>>
learned_block_candidates(
    const std::vector<PermanentId>& attackers,
    const std::vector<PermanentId>& available_blockers,
    std::mt19937_64& random, std::size_t exhaustive_limit,
    int random_samples) {
    using Block = std::pair<PermanentId, PermanentId>;
    std::vector<std::vector<Block>> candidates;
    const std::size_t choices = attackers.size() + 1;
    std::size_t combinations = 1;
    bool exhaustive = !attackers.empty();
    for (std::size_t blocker = 0;
         blocker < available_blockers.size(); ++blocker) {
        if (combinations > exhaustive_limit / choices) {
            exhaustive = false;
            break;
        }
        combinations *= choices;
    }
    if (exhaustive) {
        candidates.reserve(combinations);
        for (std::size_t encoding = 0; encoding < combinations;
             ++encoding) {
            std::size_t remaining = encoding;
            std::vector<Block> candidate;
            for (const PermanentId blocker : available_blockers) {
                const std::size_t choice = remaining % choices;
                remaining /= choices;
                if (choice != 0) {
                    candidate.emplace_back(attackers[choice - 1],
                                           blocker);
                }
            }
            candidates.push_back(std::move(candidate));
        }
        return candidates;
    }

    candidates.push_back({});
    for (const PermanentId attacker : attackers) {
        std::vector<Block> all_on_attacker;
        for (const PermanentId blocker : available_blockers) {
            all_on_attacker.emplace_back(attacker, blocker);
        }
        candidates.push_back(std::move(all_on_attacker));
    }
    for (int sample = 0; sample < random_samples; ++sample) {
        std::vector<Block> candidate;
        std::uniform_int_distribution<std::size_t> choose_block(
            0, attackers.size());
        for (const PermanentId blocker : available_blockers) {
            const std::size_t choice = choose_block(random);
            if (choice != 0) {
                candidate.emplace_back(attackers[choice - 1],
                                       blocker);
            }
        }
        std::shuffle(candidate.begin(), candidate.end(), random);
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

} // namespace

const CardDefinition& card_definition(CardId card) {
    const auto index = static_cast<std::size_t>(card);
    if (index >= kCardDefinitions.size()) {
        throw std::out_of_range("unknown card ID");
    }
    return kCardDefinitions[index];
}

std::vector<CardId> green_alpha_deck() {
    std::vector<CardId> deck(18, CardId::Forest);
    deck.insert(deck.end(), 9, CardId::GrizzlyBears);
    deck.insert(deck.end(), 12, CardId::IronrootTreefolk);
    deck.insert(deck.end(), 1, CardId::Tsunami);
    return deck;
}

std::vector<CardId> red_alpha_deck() {
    std::vector<CardId> deck(18, CardId::Mountain);
    deck.insert(deck.end(), 10, CardId::LightningBolt);
    deck.insert(deck.end(), 12, CardId::FireElemental);
    return deck;
}

std::vector<CardId> blue_alpha_deck() {
    std::vector<CardId> deck(18, CardId::Island);
    deck.insert(deck.end(), 14, CardId::Counterspell);
    deck.insert(deck.end(), 8, CardId::WaterElemental);
    return deck;
}

std::vector<CardId> white_control_deck() {
    std::vector<CardId> deck(22, CardId::Plains);
    deck.insert(deck.end(), 3, CardId::Millstone);
    deck.insert(deck.end(), 15, CardId::Moat);
    return deck;
}

Target Target::player_target(std::size_t player_index) {
    return {.player = player_index, .creature = std::nullopt};
}

Target Target::creature_target(std::size_t controller,
                               PermanentId creature_id) {
    return {.player = controller, .creature = creature_id};
}

PriorityAction PriorityAction::pass() {
    return {};
}

PriorityAction PriorityAction::play_land(CardId land) {
    return {.kind = PriorityActionKind::PlayLand, .card = land};
}

PriorityAction PriorityAction::cast_creature(CardId creature) {
    return {.kind = PriorityActionKind::CastCreature, .card = creature};
}

PriorityAction PriorityAction::cast_sorcery(CardId sorcery) {
    return {.kind = PriorityActionKind::CastSorcery, .card = sorcery};
}

PriorityAction PriorityAction::cast_artifact(CardId artifact) {
    return {.kind = PriorityActionKind::CastArtifact, .card = artifact};
}

PriorityAction
PriorityAction::cast_enchantment(CardId enchantment) {
    return {
        .kind = PriorityActionKind::CastEnchantment,
        .card = enchantment,
    };
}

PriorityAction PriorityAction::cast_lightning_bolt(Target bolt_target) {
    return {.kind = PriorityActionKind::CastLightningBolt,
            .card = CardId::LightningBolt,
            .target = bolt_target};
}

PriorityAction
PriorityAction::cast_counterspell(StackObjectId target_spell) {
    return {
        .kind = PriorityActionKind::CastCounterspell,
        .card = CardId::Counterspell,
        .target = std::nullopt,
        .spell_target = target_spell,
    };
}

PriorityAction
PriorityAction::activate_millstone(PermanentId millstone,
                                   Target mill_target) {
    return {
        .kind = PriorityActionKind::ActivateMillstone,
        .card = CardId::Millstone,
        .target = mill_target,
        .spell_target = std::nullopt,
        .source_permanent = millstone,
    };
}

std::vector<PriorityAction>
legal_priority_actions(const GameState& state, std::size_t player,
                       bool sorcery_actions) {
    if (player >= state.players.size()) {
        return {};
    }

    const auto& player_state = state.players[player];
    std::vector<PriorityAction> actions = {PriorityAction::pass()};
    const bool has_sorcery_timing =
        sorcery_actions && player == state.active_player &&
        state.stack.empty();

    if (has_sorcery_timing && !player_state.land_played_this_turn) {
        for (const CardId land :
             {CardId::Forest, CardId::Mountain, CardId::Island,
              CardId::Plains}) {
            if (has_card(player_state.hand, land)) {
                actions.push_back(PriorityAction::play_land(land));
            }
        }
    }

    if (has_sorcery_timing) {
        for (const CardId creature : kCreatureCards) {
            const auto& definition = card_definition(creature);
            if (has_card(player_state.hand, creature) &&
                can_pay(player_state, definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_creature(creature));
            }
        }
        for (const CardId sorcery : kSorceryCards) {
            const auto& definition = card_definition(sorcery);
            if (has_card(player_state.hand, sorcery) &&
                can_pay(player_state, definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_sorcery(sorcery));
            }
        }
        for (const CardId artifact : kArtifactCards) {
            const auto& definition = card_definition(artifact);
            if (has_card(player_state.hand, artifact) &&
                can_pay(player_state, definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_artifact(artifact));
            }
        }
        for (const CardId enchantment : kEnchantmentCards) {
            const auto& definition = card_definition(enchantment);
            if (has_card(player_state.hand, enchantment) &&
                can_pay(player_state, definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_enchantment(enchantment));
            }
        }
    }

    const auto& bolt = card_definition(CardId::LightningBolt);
    if (has_card(player_state.hand, CardId::LightningBolt) &&
        can_pay(player_state, bolt.cost)) {
        for (std::size_t controller = 0; controller < state.players.size();
             ++controller) {
            actions.push_back(PriorityAction::cast_lightning_bolt(
                Target::player_target(controller)));
            for (const auto& creature : state.players[controller].creatures) {
                actions.push_back(PriorityAction::cast_lightning_bolt(
                    Target::creature_target(controller, creature.id)));
            }
        }
    }

    const auto& counterspell = card_definition(CardId::Counterspell);
    if (has_card(player_state.hand, CardId::Counterspell) &&
        can_pay(player_state, counterspell.cost)) {
        for (const auto& spell : state.stack) {
            if (spell.kind == StackObjectKind::Spell) {
                actions.push_back(
                    PriorityAction::cast_counterspell(spell.id));
            }
        }
    }

    if (can_pay(player_state, kMillstoneActivationCost)) {
        for (const auto& artifact : player_state.artifacts) {
            if (artifact.card != CardId::Millstone ||
                artifact.tapped) {
                continue;
            }
            for (std::size_t target = 0;
                 target < state.players.size(); ++target) {
                actions.push_back(PriorityAction::activate_millstone(
                    artifact.id, Target::player_target(target)));
            }
        }
    }

    return actions;
}

bool apply_priority_action(GameState& state, std::size_t player,
                           const PriorityAction& action,
                           bool sorcery_actions) {
    const auto actions =
        legal_priority_actions(state, player, sorcery_actions);
    if (!contains_action(actions, action)) {
        return false;
    }

    auto& player_state = state.players[player];
    switch (action.kind) {
    case PriorityActionKind::Pass:
        return true;

    case PriorityActionKind::PlayLand:
        if (!is_land(action.card) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        player_state.lands.push_back({.card = action.card, .tapped = false});
        player_state.land_played_this_turn = true;
        ++state.stats[player].lands_played;
        return true;

    case PriorityActionKind::CastCreature: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Creature) {
            return false;
        }
        if (!pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastSorcery: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Sorcery ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastArtifact: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Artifact ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastEnchantment: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Enchantment ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastLightningBolt: {
        if (!action.target.has_value()) {
            return false;
        }
        const auto& definition = card_definition(CardId::LightningBolt);
        if (!pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::LightningBolt)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::LightningBolt,
            .controller = player,
            .target = action.target,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastCounterspell: {
        const auto& definition = card_definition(CardId::Counterspell);
        if (!action.spell_target.has_value() ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::Counterspell)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::Counterspell,
            .controller = player,
            .target = std::nullopt,
            .spell_target = action.spell_target,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::ActivateMillstone: {
        if (!action.source_permanent.has_value() ||
            !action.target.has_value() ||
            action.target->creature.has_value()) {
            return false;
        }
        auto* millstone =
            find_artifact(player_state, *action.source_permanent);
        if (millstone == nullptr || millstone->tapped ||
            millstone->card != CardId::Millstone ||
            !pay_mana(player_state, kMillstoneActivationCost)) {
            return false;
        }
        millstone->tapped = true;
        state.stack.push_back({
            .kind = StackObjectKind::ActivatedAbility,
            .id = state.next_stack_object_id++,
            .card = CardId::Millstone,
            .controller = player,
            .target = action.target,
            .spell_target = std::nullopt,
        });
        return true;
    }
    }

    return false;
}

bool resolve_top_of_stack(GameState& state) {
    if (state.stack.empty()) {
        return false;
    }

    const StackObject spell = state.stack.back();
    state.stack.pop_back();
    auto& controller = state.players[spell.controller];
    const auto& definition = card_definition(spell.card);

    if (spell.kind == StackObjectKind::ActivatedAbility) {
        if (spell.card != CardId::Millstone ||
            !spell.target.has_value() ||
            spell.target->creature.has_value()) {
            return false;
        }
        auto& target = state.players[spell.target->player];
        for (int card = 0; card < 2 && !target.library.empty();
             ++card) {
            target.graveyard.push_back(target.library.back());
            target.library.pop_back();
            ++state.stats[spell.controller].cards_milled;
        }
        return true;
    }

    if (definition.type == CardType::Creature) {
        controller.creatures.push_back(
            {.id = state.next_permanent_id++,
             .card = spell.card,
             .tapped = false,
             .summoning_sick = true,
             .damage = 0});
        return true;
    }

    if (definition.type == CardType::Artifact) {
        controller.artifacts.push_back(
            {.id = state.next_permanent_id++,
             .card = spell.card,
             .tapped = false});
        return true;
    }

    if (definition.type == CardType::Enchantment) {
        controller.enchantments.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::LightningBolt) {
        if (spell.target.has_value()) {
            const Target& target = *spell.target;
            if (target.creature.has_value()) {
                auto* creature = find_creature(
                    state.players[target.player], *target.creature);
                if (creature != nullptr) {
                    creature->damage += definition.effect_damage;
                    remove_dead_creatures(state.players[target.player]);
                }
            } else {
                state.players[target.player].life -=
                    definition.effect_damage;
                if (target.player == opponent_of(spell.controller)) {
                    state.stats[spell.controller].damage_to_opponent +=
                        static_cast<std::size_t>(
                            definition.effect_damage);
                }
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::Counterspell) {
        if (spell.spell_target.has_value()) {
            const auto target = std::find_if(
                state.stack.begin(), state.stack.end(),
                [&](const StackObject& candidate) {
                    return candidate.id == *spell.spell_target;
                });
            if (target != state.stack.end()) {
                state.players[target->controller].graveyard.push_back(
                    target->card);
                state.stack.erase(target);
                ++state.stats[spell.controller].spells_countered;
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::Tsunami) {
        for (auto& player : state.players) {
            auto land = player.lands.begin();
            while (land != player.lands.end()) {
                if (land->card == CardId::Island) {
                    player.graveyard.push_back(land->card);
                    land = player.lands.erase(land);
                } else {
                    ++land;
                }
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    return false;
}

PriorityPassResult pass_priority(GameState& state,
                                 PriorityState& priority) {
    if (priority.player >= state.players.size()) {
        throw std::out_of_range("priority player must be 0 or 1");
    }

    ++priority.consecutive_passes;
    if (priority.consecutive_passes < 2) {
        priority.player = opponent_of(priority.player);
        return PriorityPassResult::Passed;
    }

    if (state.stack.empty()) {
        return PriorityPassResult::WindowEnded;
    }
    if (!resolve_top_of_stack(state)) {
        throw std::logic_error("failed to resolve the stack");
    }

    priority.player = state.active_player;
    priority.consecutive_passes = 0;
    return PriorityPassResult::StackObjectResolved;
}

bool resolve_combat(
    GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& attackers,
    const std::vector<std::pair<PermanentId, PermanentId>>& blocks) {
    if (attacking_player >= state.players.size()) {
        return false;
    }
    const std::size_t defending_player = opponent_of(attacking_player);

    std::unordered_set<PermanentId> attacker_ids;
    for (const PermanentId attacker_id : attackers) {
        const auto* creature =
            find_creature(state.players[attacking_player], attacker_id);
        if (creature == nullptr || creature->tapped ||
            creature->summoning_sick ||
            !can_attack_through_moat(state, *creature) ||
            !attacker_ids.insert(attacker_id).second) {
            return false;
        }
    }

    std::unordered_set<PermanentId> blocker_ids;
    for (const auto& [attacker_id, blocker_id] : blocks) {
        if (!attacker_ids.contains(attacker_id)) {
            return false;
        }
        const auto* blocker =
            find_creature(state.players[defending_player], blocker_id);
        if (blocker == nullptr || blocker->tapped ||
            !blocker_ids.insert(blocker_id).second) {
            return false;
        }
    }

    std::unordered_map<PermanentId, std::vector<PermanentId>>
        blockers_by_attacker;
    for (const auto& [attacker_id, blocker_id] : blocks) {
        blockers_by_attacker[attacker_id].push_back(blocker_id);
    }

    for (const PermanentId attacker_id : attackers) {
        auto* attacker =
            find_creature(state.players[attacking_player], attacker_id);
        attacker->tapped = true;
        const auto& attacker_definition = card_definition(attacker->card);
        const auto blocker_group = blockers_by_attacker.find(attacker_id);

        if (blocker_group == blockers_by_attacker.end()) {
            state.players[defending_player].life -=
                attacker_definition.power;
            state.stats[attacking_player].damage_to_opponent +=
                static_cast<std::size_t>(attacker_definition.power);
            continue;
        }

        int attacker_damage = 0;
        for (const PermanentId blocker_id : blocker_group->second) {
            const auto* blocker =
                find_creature(state.players[defending_player], blocker_id);
            attacker_damage += card_definition(blocker->card).power;
        }
        attacker->damage += attacker_damage;

        int damage_remaining = attacker_definition.power;
        for (const PermanentId blocker_id : blocker_group->second) {
            auto* blocker =
                find_creature(state.players[defending_player], blocker_id);
            const int lethal_damage =
                std::max(0, card_definition(blocker->card).toughness -
                                blocker->damage);
            const int assigned_damage =
                std::min(damage_remaining, lethal_damage);
            blocker->damage += assigned_damage;
            damage_remaining -= assigned_damage;
        }
    }

    remove_dead_creatures(state.players[attacking_player]);
    remove_dead_creatures(state.players[defending_player]);
    return true;
}

void begin_turn(GameState& state, std::size_t player) {
    auto& player_state = state.players.at(player);
    player_state.land_played_this_turn = false;
    for (auto& land : player_state.lands) {
        land.tapped = false;
    }
    for (auto& creature : player_state.creatures) {
        creature.tapped = false;
        creature.summoning_sick = false;
    }
    for (auto& artifact : player_state.artifacts) {
        artifact.tapped = false;
    }
}

void cleanup_turn(GameState& state) {
    for (auto& player : state.players) {
        for (auto& creature : player.creatures) {
            creature.damage = 0;
        }
    }
}

Game::Game(std::vector<CardId> player_zero_deck,
           std::vector<CardId> player_one_deck, std::uint64_t seed,
           GameConfig config)
    : decks_({std::move(player_zero_deck), std::move(player_one_deck)}),
      random_(seed), config_(config) {
    if (config_.starting_player.has_value() &&
        *config_.starting_player >= state_.players.size()) {
        throw std::invalid_argument("starting player must be 0 or 1");
    }
    if (config_.max_turns == 0) {
        throw std::invalid_argument("maximum turns must be positive");
    }
    for (const auto& bot : config_.bots) {
        if ((bot.kind == BotKind::MonteCarlo ||
             bot.kind == BotKind::DeepMonteCarlo) &&
            bot.rollouts_per_action == 0) {
            throw std::invalid_argument(
                "Monte Carlo rollouts per action must be positive");
        }
    }
    const bool uses_learned =
        std::any_of(config_.bots.begin(), config_.bots.end(),
                    [](const BotConfig& bot) {
                        return bot.kind == BotKind::Learned;
                    });
    if (uses_learned && !config_.learned_model) {
        throw std::invalid_argument(
            "Learned bot requires a trained value model");
    }
}

void Game::initialize() {
    state_ = GameState{};
    setup_result_.reset();

    if (config_.starting_player.has_value()) {
        state_.starting_player = *config_.starting_player;
    } else {
        std::uniform_int_distribution<std::size_t> choose_player(0, 1);
        state_.starting_player = choose_player(random_);
    }

    for (std::size_t player = 0; player < state_.players.size(); ++player) {
        state_.players[player].library = decks_[player];
        std::shuffle(state_.players[player].library.begin(),
                     state_.players[player].library.end(), random_);
    }

    for (int card = 0; card < 7; ++card) {
        for (std::size_t player = 0; player < state_.players.size(); ++player) {
            if (!draw_card(player)) {
                setup_result_ =
                    make_result(static_cast<int>(opponent_of(player)),
                                EndReason::EmptyLibrary);
                return;
            }
        }
    }
}

bool Game::draw_card(std::size_t player) {
    auto& player_state = state_.players[player];
    if (player_state.library.empty()) {
        return false;
    }
    player_state.hand.push_back(player_state.library.back());
    player_state.library.pop_back();
    ++state_.stats[player].cards_drawn;
    return true;
}

GameResult Game::make_result(int winner, EndReason reason) const {
    return {
        .winner = winner,
        .reason = reason,
        .turns = state_.turn_number,
        .starting_player = state_.starting_player,
        .ending_life = {
            state_.players[0].life,
            state_.players[1].life,
        },
        .player_stats = state_.stats,
        .bots = {
            config_.bots[0].kind,
            config_.bots[1].kind,
        },
    };
}

std::optional<GameResult> Game::life_total_result() const {
    const bool player_zero_lost = state_.players[0].life <= 0;
    const bool player_one_lost = state_.players[1].life <= 0;
    if (!player_zero_lost && !player_one_lost) {
        return std::nullopt;
    }

    int winner = -1;
    if (player_zero_lost != player_one_lost) {
        winner = player_zero_lost ? 1 : 0;
    }
    return make_result(winner, EndReason::LifeTotal);
}

std::optional<GameResult>
Game::play_priority_window(bool sorcery_actions) {
    PriorityState priority = {
        .player = state_.active_player,
        .consecutive_passes = 0,
    };
    return continue_priority_window(sorcery_actions, priority);
}

std::optional<GameResult>
Game::continue_priority_window(bool sorcery_actions,
                               PriorityState priority) {
    while (true) {
        const auto actions =
            legal_priority_actions(state_, priority.player,
                                   sorcery_actions);
        const PriorityAction action =
            choose_priority_action(actions, priority.player,
                                   sorcery_actions);

        if (action.kind == PriorityActionKind::Pass) {
            const PriorityPassResult pass =
                pass_priority(state_, priority);
            if (pass == PriorityPassResult::Passed) {
                continue;
            }
            if (pass == PriorityPassResult::WindowEnded) {
                return std::nullopt;
            }
            if (const auto result = life_total_result();
                result.has_value()) {
                return result;
            }
            continue;
        }

        if (!apply_priority_action(state_, priority.player, action,
                                   sorcery_actions)) {
            throw std::logic_error("bot policy selected an illegal action");
        }
        // The player who acted receives priority again.
        priority.consecutive_passes = 0;
    }
}

PriorityAction Game::choose_priority_action(
    const std::vector<PriorityAction>& actions, std::size_t player,
    bool sorcery_actions) {
    if (actions.empty()) {
        throw std::logic_error("priority window has no pass action");
    }
    if (trace_ != nullptr && actions.size() > 1) {
        const bool stack_choice = !state_.stack.empty();
        const bool activated_ability_choice = std::any_of(
            actions.begin(), actions.end(),
            [](const PriorityAction& action) {
                return action.kind ==
                       PriorityActionKind::ActivateMillstone;
            });
        if (stack_choice || activated_ability_choice) {
            trace_->push_back(state_);
        }
    }
    if (actions.size() == 1) {
        return actions.front();
    }

    ++state_.stats[player].decisions;
    const auto& bot = config_.bots[player];
    if (bot.kind == BotKind::Random) {
        std::uniform_int_distribution<std::size_t> choose_action(
            0, actions.size() - 1);
        return actions[choose_action(random_)];
    }
    if (bot.kind == BotKind::Handcrafted) {
        return choose_handcrafted_action(actions, player);
    }
    if (bot.kind == BotKind::Learned) {
        if (bot.exploration_rate > 0.0) {
            std::bernoulli_distribution explore(
                bot.exploration_rate);
            if (explore(random_)) {
                std::uniform_int_distribution<std::size_t>
                    choose_action(0, actions.size() - 1);
                return actions[choose_action(random_)];
            }
        }
        return choose_learned_action(actions, player,
                                     sorcery_actions);
    }

    std::vector<double> scores(actions.size(), 0.0);
    for (std::size_t action_index = 0; action_index < actions.size();
         ++action_index) {
        for (std::size_t rollout = 0;
             rollout < bot.rollouts_per_action; ++rollout) {
            scores[action_index] +=
                rollout_action(actions[action_index], player,
                               sorcery_actions, random_());
        }
    }
    state_.stats[player].monte_carlo_rollouts +=
        actions.size() * bot.rollouts_per_action;

    const double best_score =
        *std::max_element(scores.begin(), scores.end());
    std::vector<std::size_t> best_actions;
    for (std::size_t action_index = 0; action_index < scores.size();
         ++action_index) {
        if (scores[action_index] == best_score) {
            best_actions.push_back(action_index);
        }
    }
    std::uniform_int_distribution<std::size_t> break_tie(
        0, best_actions.size() - 1);
    return actions[best_actions[break_tie(random_)]];
}

PriorityAction Game::choose_learned_action(
    const std::vector<PriorityAction>& actions, std::size_t player,
    bool sorcery_actions) {
    if (!config_.learned_model) {
        throw std::logic_error("Learned bot has no value model");
    }

    std::vector<double> scores;
    scores.reserve(actions.size());
    for (const auto& action : actions) {
        Game successor = *this;
        successor.trace_ = nullptr;
        if (action.kind != PriorityActionKind::Pass &&
            !apply_priority_action(successor.state_, player, action,
                                   sorcery_actions)) {
            scores.push_back(
                -std::numeric_limits<double>::infinity());
            continue;
        }

        bool terminal = false;
        double terminal_score = 0.5;
        while (!successor.state_.stack.empty()) {
            if (!resolve_top_of_stack(successor.state_)) {
                throw std::logic_error(
                    "Learned bot failed to resolve successor stack");
            }
            if (const auto result = successor.life_total_result();
                result.has_value()) {
                terminal = true;
                terminal_score =
                    result->winner < 0
                        ? 0.5
                        : (result->winner == static_cast<int>(player)
                               ? 1.0
                               : 0.0);
                break;
            }
        }
        double score =
            terminal
                ? terminal_score
                : config_.learned_model->predict(
                      learned_features(successor.state_, player));
        const std::size_t search_rollouts =
            config_.bots[player].rollouts_per_action;
        for (std::size_t rollout = 0; rollout < search_rollouts;
             ++rollout) {
            score += learned_rollout_action(
                action, player, sorcery_actions, random_());
        }
        scores.push_back(
            score / static_cast<double>(search_rollouts + 1));
    }
    state_.stats[player].monte_carlo_rollouts +=
        actions.size() *
        config_.bots[player].rollouts_per_action;

    const double best_score =
        *std::max_element(scores.begin(), scores.end());
    std::vector<std::size_t> best_actions;
    for (std::size_t index = 0; index < scores.size(); ++index) {
        if (scores[index] == best_score) {
            best_actions.push_back(index);
        }
    }
    std::uniform_int_distribution<std::size_t> break_tie(
        0, best_actions.size() - 1);
    return actions[best_actions[break_tie(random_)]];
}

double Game::learned_rollout_action(const PriorityAction& action,
                                    std::size_t player,
                                    bool sorcery_actions,
                                    std::uint64_t seed) const {
    Game rollout = *this;
    rollout.random_.seed(seed);
    rollout.trace_ = nullptr;
    rollout.config_.bots = {
        BotConfig{
            .kind = BotKind::Learned,
            .rollouts_per_action = 0,
        },
        BotConfig{
            .kind = BotKind::Learned,
            .rollouts_per_action = 0,
        },
    };

    for (auto& player_state : rollout.state_.players) {
        std::shuffle(player_state.library.begin(),
                     player_state.library.end(), rollout.random_);
    }
    PriorityState priority;
    if (action.kind == PriorityActionKind::Pass) {
        priority = {
            .player = opponent_of(player),
            .consecutive_passes = 1,
        };
    } else {
        if (!apply_priority_action(rollout.state_, player, action,
                                   sorcery_actions)) {
            return 0.0;
        }
        priority = {
            .player = player,
            .consecutive_passes = 0,
        };
    }
    if (const auto result = rollout.continue_priority_window(
            sorcery_actions, priority);
        result.has_value()) {
        return result->winner < 0
                   ? 0.5
                   : (result->winner == static_cast<int>(player)
                          ? 1.0
                          : 0.0);
    }

    cleanup_turn(rollout.state_);
    constexpr std::size_t kSearchHorizonTurns = 4;
    rollout.config_.max_turns =
        std::min(rollout.config_.max_turns,
                 rollout.state_.turn_number + kSearchHorizonTurns);
    const GameResult result =
        rollout.run_from_turn(rollout.state_.turn_number + 1);
    if (result.reason != EndReason::TurnLimit) {
        return result.winner < 0
                   ? 0.5
                   : (result.winner == static_cast<int>(player)
                          ? 1.0
                          : 0.0);
    }
    return rollout.config_.learned_model->predict(
        learned_features(rollout.state_, player));
}

PriorityAction Game::choose_handcrafted_action(
    const std::vector<PriorityAction>& actions, std::size_t player) {
    std::vector<double> scores;
    scores.reserve(actions.size());
    for (const auto& action : actions) {
        scores.push_back(handcrafted_action_score(action, player));
    }

    const double best_score =
        *std::max_element(scores.begin(), scores.end());
    std::vector<std::size_t> best_actions;
    for (std::size_t index = 0; index < scores.size(); ++index) {
        if (scores[index] == best_score) {
            best_actions.push_back(index);
        }
    }
    std::uniform_int_distribution<std::size_t> break_tie(
        0, best_actions.size() - 1);
    return actions[best_actions[break_tie(random_)]];
}

double Game::handcrafted_action_score(const PriorityAction& action,
                                      std::size_t player) const {
    const auto& player_state = state_.players[player];
    const std::size_t opponent = opponent_of(player);
    const auto& opponent_state = state_.players[opponent];

    switch (action.kind) {
    case PriorityActionKind::Pass:
        if (!state_.stack.empty() &&
            state_.stack.back().controller == player) {
            return 5'000.0;
        }
        return state_.stack.empty() ? -10.0 : 0.0;

    case PriorityActionKind::PlayLand:
        return 4'000.0;

    case PriorityActionKind::CastCreature: {
        double score = 1'200.0 + handcrafted_card_value(action.card);
        const bool holding_counterspell =
            has_card(player_state.hand, CardId::Counterspell);
        const int untapped_lands = static_cast<int>(std::count_if(
            player_state.lands.begin(), player_state.lands.end(),
            [](const LandPermanent& land) { return !land.tapped; }));
        const auto& cost = card_definition(action.card).cost;
        const int total_cost = cost.generic + cost.green + cost.red +
                               cost.blue + cost.white;
        if (holding_counterspell && untapped_lands < total_cost + 2) {
            score -= 1'500.0;
        }
        return score;
    }

    case PriorityActionKind::CastSorcery: {
        const auto count_islands = [](const PlayerState& state) {
            return std::count_if(
                state.lands.begin(), state.lands.end(),
                [](const LandPermanent& land) {
                    return land.card == CardId::Island;
                });
        };
        const auto enemy_islands = count_islands(opponent_state);
        const auto own_islands = count_islands(player_state);
        return 700.0 + 800.0 * static_cast<double>(enemy_islands) -
               800.0 * static_cast<double>(own_islands);
    }

    case PriorityActionKind::CastArtifact:
        return 1'500.0;

    case PriorityActionKind::CastEnchantment: {
        const double battlefield_swing =
            250.0 * static_cast<double>(opponent_state.creatures.size()) -
            150.0 * static_cast<double>(player_state.creatures.size());
        return 1'800.0 + battlefield_swing;
    }

    case PriorityActionKind::CastLightningBolt:
        if (!action.target.has_value()) {
            return -10'000.0;
        }
        if (!action.target->creature.has_value()) {
            if (action.target->player == player) {
                return -10'000.0;
            }
            if (opponent_state.life <= 3) {
                return 10'000.0;
            }
            return 900.0 +
                   10.0 * static_cast<double>(20 - opponent_state.life);
        } else {
            if (action.target->player == player) {
                return -10'000.0;
            }
            const auto target = std::find_if(
                opponent_state.creatures.begin(),
                opponent_state.creatures.end(),
                [&](const CreaturePermanent& creature) {
                    return creature.id == *action.target->creature;
                });
            if (target == opponent_state.creatures.end()) {
                return -10'000.0;
            }
            const auto& definition = card_definition(target->card);
            const bool lethal =
                target->damage + card_definition(CardId::LightningBolt)
                                     .effect_damage >=
                definition.toughness;
            return (lethal ? 2'000.0 : 500.0) +
                   handcrafted_card_value(target->card);
        }

    case PriorityActionKind::CastCounterspell: {
        if (!action.spell_target.has_value()) {
            return -10'000.0;
        }
        const auto target = std::find_if(
            state_.stack.begin(), state_.stack.end(),
            [&](const StackObject& object) {
                return object.id == *action.spell_target;
            });
        if (target == state_.stack.end() ||
            target->controller == player) {
            return -10'000.0;
        }
        return 3'000.0 + handcrafted_card_value(target->card);
    }

    case PriorityActionKind::ActivateMillstone:
        if (!action.target.has_value() ||
            action.target->player == player) {
            return -10'000.0;
        }
        if (opponent_state.library.size() <= 2) {
            return 10'000.0;
        }
        return 1'600.0 +
               static_cast<double>(40 - opponent_state.library.size()) *
                   10.0;
    }
    return -10'000.0;
}

double Game::rollout_action(const PriorityAction& action,
                            std::size_t player, bool sorcery_actions,
                            std::uint64_t seed) const {
    Game rollout = *this;
    rollout.random_.seed(seed);
    rollout.config_.bots = {
        BotConfig{.kind = BotKind::Random},
        BotConfig{.kind = BotKind::Random},
    };

    // The order of each library is hidden information. Re-randomizing it makes
    // each rollout a separate determinization instead of letting the bot peek
    // at the already-shuffled future.
    for (auto& player_state : rollout.state_.players) {
        std::shuffle(player_state.library.begin(),
                     player_state.library.end(), rollout.random_);
    }

    if (action.kind != PriorityActionKind::Pass &&
        !apply_priority_action(rollout.state_, player, action,
                               sorcery_actions)) {
        return -std::numeric_limits<double>::infinity();
    }

    // Resolve the candidate and anything already below it, then use a complete
    // random continuation from the following turn. This keeps rollout cost
    // bounded while the real game still uses normal stack priority.
    while (!rollout.state_.stack.empty()) {
        if (!resolve_top_of_stack(rollout.state_)) {
            throw std::logic_error("rollout failed to resolve the stack");
        }
        if (const auto result = rollout.life_total_result();
            result.has_value()) {
            if (result->winner < 0) {
                return 0.5;
            }
            return result->winner == static_cast<int>(player) ? 1.0 : 0.0;
        }
    }

    cleanup_turn(rollout.state_);
    const GameResult result =
        rollout.run_from_turn(rollout.state_.turn_number + 1);
    if (result.winner < 0) {
        return 0.5;
    }
    return result.winner == static_cast<int>(player) ? 1.0 : 0.0;
}

std::optional<GameResult> Game::play_combat() {
    if (const auto result = play_priority_window(false);
        result.has_value()) {
        return result;
    }

    auto& attacking_state = state_.players[state_.active_player];
    const std::size_t defending_player = opponent_of(state_.active_player);
    auto& defending_state = state_.players[defending_player];

    std::vector<PermanentId> attackers;
    const bool handcrafted_attacker =
        config_.bots[state_.active_player].kind == BotKind::Handcrafted;
    const bool learned_attacker =
        config_.bots[state_.active_player].kind == BotKind::Learned;
    if (handcrafted_attacker) {
        std::vector<const CreaturePermanent*> legal_attackers;
        int total_power = 0;
        for (const auto& creature : attacking_state.creatures) {
            if (!creature.tapped && !creature.summoning_sick &&
                can_attack_through_moat(state_, creature)) {
                legal_attackers.push_back(&creature);
                total_power += card_definition(creature.card).power;
            }
        }

        const bool attack_for_lethal =
            total_power >= defending_state.life;
        for (const auto* creature : legal_attackers) {
            bool favorable_attack = defending_state.creatures.empty();
            const auto& attacker = card_definition(creature->card);
            for (const auto& blocker : defending_state.creatures) {
                if (blocker.tapped) {
                    continue;
                }
                const auto& blocker_definition =
                    card_definition(blocker.card);
                if (attacker.power >= blocker_definition.toughness) {
                    favorable_attack = true;
                    break;
                }
            }
            if (attack_for_lethal || favorable_attack) {
                attackers.push_back(creature->id);
            }
        }
    } else if (learned_attacker) {
        std::vector<PermanentId> legal_attackers;
        for (const auto& creature : attacking_state.creatures) {
            if (!creature.tapped && !creature.summoning_sick &&
                can_attack_through_moat(state_, creature)) {
                legal_attackers.push_back(creature.id);
            }
        }

        const auto candidates =
            learned_attack_candidates(legal_attackers, random_);

        double best_score =
            -std::numeric_limits<double>::infinity();
        for (const auto& candidate : candidates) {
            const auto block_candidates = learned_block_candidates(
                candidate,
                [&] {
                    std::vector<PermanentId> blockers;
                    for (const auto& blocker :
                         defending_state.creatures) {
                        if (!blocker.tapped) {
                            blockers.push_back(blocker.id);
                        }
                    }
                    return blockers;
                }(),
                random_, 64, 48);
            double total_score = 0.0;
            for (const auto& sampled_blocks : block_candidates) {
                GameState successor = state_;
                if (!resolve_combat(successor, state_.active_player,
                                    candidate, sampled_blocks)) {
                    throw std::logic_error(
                        "Learned bot sampled illegal combat");
                }
                double sample_score = 0.0;
                if (successor.players[defending_player].life <= 0) {
                    sample_score = 1.0;
                } else if (
                    successor.players[state_.active_player].life <= 0) {
                    sample_score = 0.0;
                } else {
                    sample_score = config_.learned_model->predict(
                        learned_features(successor,
                                         state_.active_player));
                }
                total_score += sample_score;
            }
            const double expected_score =
                total_score /
                static_cast<double>(block_candidates.size());
            if (expected_score > best_score) {
                best_score = expected_score;
                attackers = candidate;
            }
        }
    } else {
        std::uniform_int_distribution<int> attack_or_not(0, 1);
        for (const auto& creature : attacking_state.creatures) {
            if (!creature.tapped && !creature.summoning_sick &&
                can_attack_through_moat(state_, creature) &&
                attack_or_not(random_) == 1) {
                attackers.push_back(creature.id);
            }
        }
    }

    if (attackers.empty()) {
        return play_priority_window(false);
    }

    std::vector<PermanentId> available_blockers;
    for (const auto& creature : defending_state.creatures) {
        if (!creature.tapped) {
            available_blockers.push_back(creature.id);
        }
    }
    std::unordered_map<PermanentId, std::vector<PermanentId>>
        blockers_by_attacker;
    const bool handcrafted_defender =
        config_.bots[defending_player].kind == BotKind::Handcrafted;
    const bool learned_defender =
        config_.bots[defending_player].kind == BotKind::Learned;
    if (handcrafted_defender) {
        std::vector<PermanentId> unassigned = available_blockers;
        std::vector<PermanentId> ordered_attackers = attackers;
        std::sort(ordered_attackers.begin(), ordered_attackers.end(),
                  [&](PermanentId left, PermanentId right) {
                      const auto* left_creature =
                          find_creature(attacking_state, left);
                      const auto* right_creature =
                          find_creature(attacking_state, right);
                      return card_definition(left_creature->card).power >
                             card_definition(right_creature->card).power;
                  });

        int incoming_power = 0;
        for (const PermanentId attacker_id : attackers) {
            const auto* attacker =
                find_creature(attacking_state, attacker_id);
            incoming_power += card_definition(attacker->card).power;
        }
        const bool must_block = incoming_power >= defending_state.life;

        for (const PermanentId attacker_id : ordered_attackers) {
            const auto* attacker =
                find_creature(attacking_state, attacker_id);
            const auto& attacker_definition =
                card_definition(attacker->card);
            auto best_blocker = unassigned.end();
            double best_score = must_block ? 1.0 : 0.0;

            for (auto blocker = unassigned.begin();
                 blocker != unassigned.end(); ++blocker) {
                const auto* blocker_creature =
                    find_creature(defending_state, *blocker);
                const auto& blocker_definition =
                    card_definition(blocker_creature->card);
                const bool kills_attacker =
                    blocker_definition.power >=
                    attacker_definition.toughness;
                const bool survives =
                    blocker_definition.toughness >
                    attacker_definition.power;
                double score =
                    20.0 * static_cast<double>(attacker_definition.power);
                if (kills_attacker) {
                    score += 1'000.0 +
                             handcrafted_card_value(attacker->card);
                }
                if (survives) {
                    score += 500.0;
                } else {
                    score -= handcrafted_card_value(
                        blocker_creature->card);
                }
                if (score > best_score) {
                    best_score = score;
                    best_blocker = blocker;
                }
            }
            if (best_blocker != unassigned.end()) {
                blockers_by_attacker[attacker_id].push_back(
                    *best_blocker);
                unassigned.erase(best_blocker);
            }
        }
    } else if (learned_defender) {
        const auto candidates = learned_block_candidates(
            attackers, available_blockers, random_, 512, 96);

        double best_score =
            -std::numeric_limits<double>::infinity();
        std::vector<std::pair<PermanentId, PermanentId>> best_blocks;
        for (const auto& candidate : candidates) {
            GameState successor = state_;
            if (!resolve_combat(successor, state_.active_player,
                                attackers, candidate)) {
                throw std::logic_error(
                    "Learned bot sampled illegal blocks");
            }
            double score = 0.0;
            if (successor.players[defending_player].life <= 0) {
                score = 0.0;
            } else if (
                successor.players[state_.active_player].life <= 0) {
                score = 1.0;
            } else {
                score = config_.learned_model->predict(
                    learned_features(successor, defending_player));
            }
            if (score > best_score) {
                best_score = score;
                best_blocks = candidate;
            }
        }
        for (const auto& [attacker, blocker] : best_blocks) {
            blockers_by_attacker[attacker].push_back(blocker);
        }
    } else {
        std::shuffle(available_blockers.begin(),
                     available_blockers.end(), random_);
        for (const PermanentId blocker : available_blockers) {
            // Zero means no block; other values select an attacker. Multiple
            // blockers may legally select the same attacker.
            std::uniform_int_distribution<std::size_t> choose_block(
                0, attackers.size());
            const std::size_t choice = choose_block(random_);
            if (choice != 0) {
                blockers_by_attacker[attackers[choice - 1]].push_back(
                    blocker);
            }
        }
    }

    std::vector<std::pair<PermanentId, PermanentId>> blocks;
    for (const PermanentId attacker : attackers) {
        auto& blockers = blockers_by_attacker[attacker];
        // The attacking player chooses damage assignment order. The
        // handcrafted
        // policy orders the easiest creatures to kill first.
        if (handcrafted_attacker) {
            std::sort(blockers.begin(), blockers.end(),
                      [&](PermanentId left, PermanentId right) {
                          const auto* left_creature =
                              find_creature(defending_state, left);
                          const auto* right_creature =
                              find_creature(defending_state, right);
                          return card_definition(left_creature->card)
                                     .toughness <
                                 card_definition(right_creature->card)
                                     .toughness;
                      });
        } else {
            std::shuffle(blockers.begin(), blockers.end(), random_);
        }
        for (const PermanentId blocker : blockers) {
            blocks.emplace_back(attacker, blocker);
        }
    }

    if (!resolve_combat(state_, state_.active_player, attackers, blocks)) {
        throw std::logic_error("random policy declared illegal combat");
    }
    if (const auto result = life_total_result(); result.has_value()) {
        return result;
    }
    return play_priority_window(false);
}

GameResult Game::run() {
    initialize();
    if (setup_result_.has_value()) {
        return *setup_result_;
    }
    return run_from_turn(1);
}

GameResult Game::run_with_trace(std::vector<GameState>& trace) {
    trace.clear();
    trace_ = &trace;
    const GameResult result = run();
    trace_ = nullptr;
    return result;
}

GameResult Game::run_from_turn(std::size_t first_turn) {
    for (std::size_t turn = first_turn; turn <= config_.max_turns;
         ++turn) {
        state_.turn_number = turn;
        state_.active_player = (state_.starting_player + turn - 1) % 2;
        begin_turn(state_, state_.active_player);

        const bool starting_player_first_turn =
            turn == 1 && state_.active_player == state_.starting_player;
        if (!starting_player_first_turn &&
            !draw_card(state_.active_player)) {
            return make_result(
                static_cast<int>(opponent_of(state_.active_player)),
                EndReason::EmptyLibrary);
        }
        if (trace_ != nullptr) {
            trace_->push_back(state_);
        }

        if (const auto result = play_priority_window(true);
            result.has_value()) {
            return *result;
        }
        if (const auto result = play_combat(); result.has_value()) {
            return *result;
        }
        if (const auto result = play_priority_window(true);
            result.has_value()) {
            return *result;
        }
        cleanup_turn(state_);
    }

    return make_result(-1, EndReason::TurnLimit);
}

const GameState& Game::state() const {
    return state_;
}

double SimulationSummary::average_turns() const {
    return games == 0 ? 0.0
                      : static_cast<double>(total_turns) /
                            static_cast<double>(games);
}

namespace {

double percentage(std::size_t numerator, std::size_t denominator) {
    return denominator == 0
               ? 0.0
               : 100.0 * static_cast<double>(numerator) /
                     static_cast<double>(denominator);
}

double average(std::int64_t total, std::size_t count) {
    return count == 0
               ? 0.0
               : static_cast<double>(total) / static_cast<double>(count);
}

} // namespace

double DeckSimulationStats::win_rate() const {
    return percentage(wins, games);
}

double DeckSimulationStats::on_play_win_rate() const {
    return percentage(on_play_wins, on_play_games);
}

double DeckSimulationStats::on_draw_win_rate() const {
    return percentage(on_draw_wins, on_draw_games);
}

double DeckSimulationStats::average_ending_life() const {
    return average(total_ending_life, games);
}

double DeckSimulationStats::average_cards_drawn() const {
    return average(static_cast<std::int64_t>(total_cards_drawn), games);
}

double DeckSimulationStats::average_lands_played() const {
    return average(static_cast<std::int64_t>(total_lands_played), games);
}

double DeckSimulationStats::average_spells_cast() const {
    return average(static_cast<std::int64_t>(total_spells_cast), games);
}

double DeckSimulationStats::average_spells_countered() const {
    return average(static_cast<std::int64_t>(total_spells_countered),
                   games);
}

double DeckSimulationStats::average_damage_to_opponent() const {
    return average(static_cast<std::int64_t>(total_damage_to_opponent),
                   games);
}

double DeckSimulationStats::average_cards_milled() const {
    return average(static_cast<std::int64_t>(total_cards_milled), games);
}

double BotSimulationStats::win_rate() const {
    return percentage(wins, games);
}

double BotSimulationStats::average_decisions() const {
    return average(static_cast<std::int64_t>(total_decisions), games);
}

double BotSimulationStats::average_rollouts() const {
    return average(static_cast<std::int64_t>(total_rollouts), games);
}

double BotSimulationStats::average_rollouts_per_decision() const {
    return average(static_cast<std::int64_t>(total_rollouts),
                   total_decisions);
}

double BotMatchupStats::first_win_rate() const {
    return percentage(first_wins, games);
}

double BotMatchupStats::second_win_rate() const {
    return percentage(second_wins, games);
}

namespace {

std::array<BotMatchupStats, kBotMatchupCount>
empty_bot_matchups() {
    std::array<BotMatchupStats, kBotMatchupCount> matchups;
    std::size_t matchup = 0;
    for (std::size_t first = 0; first < kBotKindCount; ++first) {
        for (std::size_t second = first + 1;
             second < kBotKindCount; ++second) {
            matchups[matchup++] = {
                .first_bot = static_cast<BotKind>(first),
                .second_bot = static_cast<BotKind>(second),
            };
        }
    }
    return matchups;
}

std::size_t bot_matchup_index(BotKind first, BotKind second) {
    const auto low = std::min(static_cast<std::size_t>(first),
                              static_cast<std::size_t>(second));
    const auto high = std::max(static_cast<std::size_t>(first),
                               static_cast<std::size_t>(second));
    std::size_t matchup = 0;
    for (std::size_t candidate_low = 0;
         candidate_low < kBotKindCount; ++candidate_low) {
        for (std::size_t candidate_high = candidate_low + 1;
             candidate_high < kBotKindCount;
             ++candidate_high, ++matchup) {
            if (low == candidate_low && high == candidate_high) {
                return matchup;
            }
        }
    }
    throw std::logic_error("bot matchup requires two different bots");
}

void configure_bots(GameConfig& game_config, std::size_t game_index,
                    const TournamentConfig& tournament_config) {
    const BotConfig random = {
        .kind = BotKind::Random,
        .rollouts_per_action =
            tournament_config.monte_carlo_rollouts,
    };
    const BotConfig monte_carlo = {
        .kind = BotKind::MonteCarlo,
        .rollouts_per_action =
            tournament_config.monte_carlo_rollouts,
    };
    const BotConfig deep_monte_carlo = {
        .kind = BotKind::DeepMonteCarlo,
        .rollouts_per_action =
            tournament_config.deep_monte_carlo_rollouts,
    };
    const BotConfig handcrafted = {
        .kind = BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    const BotConfig learned = {
        .kind = BotKind::Learned,
        .rollouts_per_action = 2,
        .training_games = tournament_config.learned_training_games,
    };

    switch (tournament_config.bot_field) {
    case BotField::Random:
        game_config.bots = {random, random};
        break;
    case BotField::MonteCarlo:
        game_config.bots = {monte_carlo, monte_carlo};
        break;
    case BotField::DeepMonteCarlo:
        game_config.bots = {deep_monte_carlo, deep_monte_carlo};
        break;
    case BotField::Handcrafted:
        game_config.bots = {handcrafted, handcrafted};
        break;
    case BotField::Learned:
        game_config.bots = {learned, learned};
        break;
    case BotField::Mixed:
        // The square rotation covers all ordered pairings, including mirrors.
        // Every bot has equal exposure in both seats.
        {
            const std::array<BotConfig, kBotKindCount> bots = {
                random,
                monte_carlo,
                deep_monte_carlo,
                handcrafted,
                learned,
            };
            const std::size_t pairing =
                game_index % (kBotKindCount * kBotKindCount);
            game_config.bots = {
                bots[pairing / kBotKindCount],
                bots[pairing % kBotKindCount],
            };
        }
        break;
    }
}

void record_deck_result(DeckSimulationStats& deck,
                        const GameResult& result,
                        std::size_t player) {
    ++deck.games;
    if (result.winner < 0) {
        ++deck.draws;
    } else if (result.winner == static_cast<int>(player)) {
        ++deck.wins;
    } else {
        ++deck.losses;
    }

    if (result.starting_player == player) {
        ++deck.on_play_games;
        if (result.winner == static_cast<int>(player)) {
            ++deck.on_play_wins;
        }
    } else {
        ++deck.on_draw_games;
        if (result.winner == static_cast<int>(player)) {
            ++deck.on_draw_wins;
        }
    }

    deck.total_ending_life += result.ending_life[player];
    deck.total_cards_drawn += result.player_stats[player].cards_drawn;
    deck.total_lands_played += result.player_stats[player].lands_played;
    deck.total_spells_cast += result.player_stats[player].spells_cast;
    deck.total_spells_countered +=
        result.player_stats[player].spells_countered;
    deck.total_damage_to_opponent +=
        result.player_stats[player].damage_to_opponent;
    deck.total_cards_milled += result.player_stats[player].cards_milled;
}

void record_bot_result(BotSimulationStats& bot,
                       const GameResult& result,
                       std::size_t player) {
    ++bot.games;
    if (result.winner < 0) {
        ++bot.draws;
    } else if (result.winner == static_cast<int>(player)) {
        ++bot.wins;
    } else {
        ++bot.losses;
    }
    bot.total_decisions += result.player_stats[player].decisions;
    bot.total_rollouts +=
        result.player_stats[player].monte_carlo_rollouts;
}

SimulationSummary run_matchup(const std::vector<CardId>& first_deck,
                              const std::vector<CardId>& second_deck,
                              std::size_t games, std::uint64_t seed,
                              GameConfig game_config,
                              std::optional<TournamentConfig>
                                  tournament_config = std::nullopt,
                              std::size_t schedule_offset = 0) {
    SimulationSummary summary;
    summary.games = games;
    summary.bot_matchups = empty_bot_matchups();
    std::mt19937_64 seed_generator(seed);

    std::vector<GameConfig> configs(games, game_config);
    std::vector<std::uint64_t> game_seeds(games);
    std::size_t current_matrix =
        std::numeric_limits<std::size_t>::max();
    std::uint64_t matrix_seed = 0;
    for (std::size_t game_index = 0; game_index < games; ++game_index) {
        if (tournament_config.has_value()) {
            configure_bots(configs[game_index],
                           schedule_offset + game_index,
                           *tournament_config);
        }
        if (tournament_config.has_value() &&
            tournament_config->bot_field == BotField::Mixed) {
            const std::size_t scheduled_game =
                schedule_offset + game_index;
            const std::size_t matrix =
                scheduled_game /
                (kBotKindCount * kBotKindCount);
            if (matrix != current_matrix) {
                current_matrix = matrix;
                matrix_seed = seed_generator();
            }
            game_seeds[game_index] = matrix_seed;
            if (!game_config.starting_player.has_value()) {
                const std::size_t pairing =
                    scheduled_game %
                    (kBotKindCount * kBotKindCount);
                configs[game_index].starting_player =
                    (pairing + matrix) % 2;
            }
        } else {
            game_seeds[game_index] = seed_generator();
        }
    }

    std::vector<GameResult> results(games);
    std::atomic_size_t next_game = 0;
    const std::size_t worker_count = std::min<std::size_t>(
        games, std::max(1U, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t game_index =
                    next_game.fetch_add(1, std::memory_order_relaxed);
                if (game_index >= games) {
                    return;
                }
                Game game(first_deck, second_deck,
                          game_seeds[game_index],
                          configs[game_index]);
                results[game_index] = game.run();
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    for (const GameResult& result : results) {
        summary.total_turns += result.turns;

        for (std::size_t player = 0; player < summary.decks.size();
             ++player) {
            auto& deck = summary.decks[player];
            const auto bot_index =
                static_cast<std::size_t>(result.bots[player]);
            record_deck_result(deck, result, player);
            record_deck_result(
                summary.deck_bots[player][bot_index], result, player);

            record_bot_result(summary.bots[bot_index], result, player);
        }

        if (result.bots[0] != result.bots[1]) {
            auto& matchup = summary.bot_matchups[bot_matchup_index(
                result.bots[0], result.bots[1])];
            ++matchup.games;
            if (result.winner < 0) {
                ++matchup.draws;
            } else if (result.bots[static_cast<std::size_t>(
                           result.winner)] == matchup.first_bot) {
                ++matchup.first_wins;
            } else {
                ++matchup.second_wins;
            }
        }

        if (result.winner < 0) {
            ++summary.draws;
        }

        switch (result.reason) {
        case EndReason::LifeTotal:
            ++summary.life_total_finishes;
            break;
        case EndReason::EmptyLibrary:
            ++summary.empty_library_finishes;
            break;
        case EndReason::TurnLimit:
            ++summary.turn_limit_draws;
            break;
        }
    }

    return summary;
}

std::vector<CardId> deck_cards(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return green_alpha_deck();
    case DeckId::Red:
        return red_alpha_deck();
    case DeckId::Blue:
        return blue_alpha_deck();
    case DeckId::White:
        return white_control_deck();
    }
    throw std::out_of_range("unknown deck ID");
}

void merge_deck_stats(DeckSimulationStats& destination,
                      const DeckSimulationStats& source) {
    destination.games += source.games;
    destination.wins += source.wins;
    destination.losses += source.losses;
    destination.draws += source.draws;
    destination.on_play_games += source.on_play_games;
    destination.on_play_wins += source.on_play_wins;
    destination.on_draw_games += source.on_draw_games;
    destination.on_draw_wins += source.on_draw_wins;
    destination.total_ending_life += source.total_ending_life;
    destination.total_cards_drawn += source.total_cards_drawn;
    destination.total_lands_played += source.total_lands_played;
    destination.total_spells_cast += source.total_spells_cast;
    destination.total_spells_countered +=
        source.total_spells_countered;
    destination.total_damage_to_opponent +=
        source.total_damage_to_opponent;
    destination.total_cards_milled += source.total_cards_milled;
}

void merge_bot_stats(BotSimulationStats& destination,
                     const BotSimulationStats& source) {
    destination.games += source.games;
    destination.wins += source.wins;
    destination.losses += source.losses;
    destination.draws += source.draws;
    destination.total_decisions += source.total_decisions;
    destination.total_rollouts += source.total_rollouts;
}

void merge_bot_matchup_stats(BotMatchupStats& destination,
                             const BotMatchupStats& source) {
    if (destination.first_bot != source.first_bot ||
        destination.second_bot != source.second_bot) {
        throw std::logic_error("cannot merge different bot matchups");
    }
    destination.games += source.games;
    destination.first_wins += source.first_wins;
    destination.second_wins += source.second_wins;
    destination.draws += source.draws;
}

} // namespace

SimulationSummary run_simulation(std::size_t games, std::uint64_t seed,
                                 GameConfig game_config) {
    return run_matchup(green_alpha_deck(), red_alpha_deck(), games, seed,
                       game_config);
}

std::string_view deck_name(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return "Green";
    case DeckId::Red:
        return "Red";
    case DeckId::Blue:
        return "Blue";
    case DeckId::White:
        return "White";
    }
    return "Unknown";
}

std::string_view deck_list(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return "18 Forest / 9 Grizzly Bears / 12 Ironroot Treefolk / "
               "1 Tsunami";
    case DeckId::Red:
        return "18 Mountain / 10 Lightning Bolt / 12 Fire Elemental";
    case DeckId::Blue:
        return "18 Island / 14 Counterspell / 8 Water Elemental";
    case DeckId::White:
        return "22 Plains / 3 Millstone / 15 Moat";
    }
    return "Unknown";
}

std::string_view bot_name(BotKind bot) {
    switch (bot) {
    case BotKind::Random:
        return "Random";
    case BotKind::MonteCarlo:
        return "Monte Carlo";
    case BotKind::DeepMonteCarlo:
        return "Deep Monte Carlo";
    case BotKind::Handcrafted:
        return "Handcrafted Policy";
    case BotKind::Learned:
        return "Learned Value";
    }
    return "Unknown";
}

double TournamentSummary::average_turns() const {
    return total_games == 0
               ? 0.0
               : static_cast<double>(total_turns) /
                     static_cast<double>(total_games);
}

bool LearnedDeckLiftSummary::complete() const {
    return std::all_of(
        decks.begin(), decks.end(),
        [](const DeckLiftComparison& deck) { return deck.available; });
}

bool LearnedDeckLiftSummary::learned_is_best_on_every_deck() const {
    return complete() &&
           std::all_of(
               decks.begin(), decks.end(),
               [](const DeckLiftComparison& deck) {
                   return deck.learned_is_best;
               });
}

LearnedDeckLiftSummary
compare_learned_deck_lifts(const TournamentSummary& summary) {
    constexpr std::array<BotKind, 3> comparison_bots = {
        BotKind::MonteCarlo,
        BotKind::DeepMonteCarlo,
        BotKind::Handcrafted,
    };
    constexpr double comparison_tolerance = 1.0e-12;
    const auto random_index =
        static_cast<std::size_t>(BotKind::Random);
    const auto learned_index =
        static_cast<std::size_t>(BotKind::Learned);

    LearnedDeckLiftSummary result;
    for (std::size_t deck = 0; deck < result.decks.size(); ++deck) {
        DeckLiftComparison& comparison = result.decks[deck];
        comparison.deck = static_cast<DeckId>(deck);
        const auto& random = summary.deck_bots[deck][random_index];
        const auto& learned = summary.deck_bots[deck][learned_index];
        if (random.games == 0 || learned.games == 0) {
            continue;
        }

        bool has_every_comparison = true;
        bool has_best_other = false;
        double best_other_rate = 0.0;
        for (const BotKind bot : comparison_bots) {
            const auto& stats = summary.deck_bots[deck][
                static_cast<std::size_t>(bot)];
            if (stats.games == 0) {
                has_every_comparison = false;
                break;
            }
            if (!has_best_other ||
                stats.win_rate() > best_other_rate) {
                has_best_other = true;
                best_other_rate = stats.win_rate();
                comparison.best_other = bot;
            }
        }
        if (!has_every_comparison || !has_best_other) {
            continue;
        }

        comparison.available = true;
        comparison.learned_lift =
            learned.win_rate() - random.win_rate();
        comparison.best_other_lift =
            best_other_rate - random.win_rate();
        comparison.learned_is_best =
            comparison.learned_lift + comparison_tolerance >=
            comparison.best_other_lift;
    }
    return result;
}

TournamentSummary run_tournament(std::size_t games_per_matchup,
                                 std::uint64_t seed,
                                 GameConfig game_config,
                                 TournamentConfig tournament_config) {
    const bool uses_monte_carlo =
        tournament_config.bot_field == BotField::MonteCarlo ||
        tournament_config.bot_field == BotField::Mixed;
    const bool uses_deep_monte_carlo =
        tournament_config.bot_field == BotField::DeepMonteCarlo ||
        tournament_config.bot_field == BotField::Mixed;
    const bool uses_learned =
        tournament_config.bot_field == BotField::Learned ||
        tournament_config.bot_field == BotField::Mixed;
    if (uses_monte_carlo &&
        tournament_config.monte_carlo_rollouts == 0) {
        throw std::invalid_argument(
            "Monte Carlo rollouts per action must be positive");
    }
    if (uses_deep_monte_carlo &&
        tournament_config.deep_monte_carlo_rollouts == 0) {
        throw std::invalid_argument(
            "deep Monte Carlo rollouts per action must be positive");
    }
    if (tournament_config.bot_field == BotField::Mixed &&
        tournament_config.deep_monte_carlo_rollouts <=
            tournament_config.monte_carlo_rollouts) {
        throw std::invalid_argument(
            "deep Monte Carlo must use more rollouts than Monte Carlo");
    }
    if (uses_learned &&
        tournament_config.learned_training_games == 0 &&
        !game_config.learned_model) {
        throw std::invalid_argument(
            "Learned bot training games must be positive");
    }
    if (uses_learned && !game_config.learned_model) {
        game_config.learned_model = train_learned_model(
            tournament_config.learned_training_games,
            seed ^ 0x4C4541524E454455ULL);
    }

    TournamentSummary summary;
    summary.games_per_matchup = games_per_matchup;
    summary.total_games = games_per_matchup * summary.matchups.size();
    summary.bot_matchups = empty_bot_matchups();

    constexpr std::array<std::pair<DeckId, DeckId>, 6> pairings = {{
        {DeckId::Green, DeckId::Red},
        {DeckId::Green, DeckId::Blue},
        {DeckId::Green, DeckId::White},
        {DeckId::Red, DeckId::Blue},
        {DeckId::Red, DeckId::White},
        {DeckId::Blue, DeckId::White},
    }};
    std::mt19937_64 seed_generator(seed);

    for (std::size_t index = 0; index < pairings.size(); ++index) {
        const auto [first, second] = pairings[index];
        SimulationSummary matchup =
            run_matchup(deck_cards(first), deck_cards(second),
                        games_per_matchup, seed_generator(), game_config,
                        tournament_config,
                        index * games_per_matchup);
        summary.matchups[index] = {
            .first_deck = first,
            .second_deck = second,
            .result = matchup,
        };

        merge_deck_stats(
            summary.decks[static_cast<std::size_t>(first)],
            matchup.decks[0]);
        merge_deck_stats(
            summary.decks[static_cast<std::size_t>(second)],
            matchup.decks[1]);
        for (std::size_t bot = 0; bot < summary.bots.size(); ++bot) {
            merge_bot_stats(summary.bots[bot], matchup.bots[bot]);
            merge_deck_stats(
                summary
                    .deck_bots[static_cast<std::size_t>(first)][bot],
                matchup.deck_bots[0][bot]);
            merge_deck_stats(
                summary
                    .deck_bots[static_cast<std::size_t>(second)][bot],
                matchup.deck_bots[1][bot]);
        }
        for (std::size_t bot_matchup = 0;
             bot_matchup < summary.bot_matchups.size();
             ++bot_matchup) {
            merge_bot_matchup_stats(
                summary.bot_matchups[bot_matchup],
                matchup.bot_matchups[bot_matchup]);
        }
        summary.draws += matchup.draws;
        summary.life_total_finishes += matchup.life_total_finishes;
        summary.empty_library_finishes +=
            matchup.empty_library_finishes;
        summary.turn_limit_draws += matchup.turn_limit_draws;
        summary.total_turns += matchup.total_turns;
    }

    return summary;
}

std::shared_ptr<const LearnedModel>
train_learned_model(std::size_t training_games, std::uint64_t seed) {
    if (training_games == 0) {
        throw std::invalid_argument(
            "Learned model training games must be positive");
    }

    std::mt19937_64 random(seed);
    std::uniform_int_distribution<std::size_t> choose_deck(0, 3);
    const auto choose_distinct_decks = [&] {
        const std::size_t first = choose_deck(random);
        std::size_t second = choose_deck(random);
        while (second == first) {
            second = choose_deck(random);
        }
        return std::pair{
            static_cast<DeckId>(first),
            static_cast<DeckId>(second),
        };
    };
    std::vector<LearnedModel::TrainingExample> examples;
    examples.reserve(training_games * 120);
    const auto add_trace =
        [&](const std::vector<GameState>& trace,
            const GameResult& result,
            std::vector<LearnedModel::TrainingExample>& destination) {
        for (const auto& state : trace) {
            for (std::size_t perspective = 0; perspective < 2;
                 ++perspective) {
                double target = 0.5;
                if (result.winner >= 0) {
                    const double discounted_outcome =
                        0.5 * std::pow(
                                  0.985,
                                  static_cast<double>(result.turns));
                    target =
                        result.winner ==
                                static_cast<int>(perspective)
                            ? 0.5 + discounted_outcome
                            : 0.5 - discounted_outcome;
                }
                destination.emplace_back(
                    learned_features(state, perspective), target);
            }
        }
    };

    for (std::size_t game_index = 0; game_index < training_games;
         ++game_index) {
        const auto [first_deck, second_deck] =
            choose_distinct_decks();
        Game game(deck_cards(first_deck), deck_cards(second_deck),
                  random());
        std::vector<GameState> trace;
        const GameResult result = game.run_with_trace(trace);
        add_trace(trace, result, examples);
    }

    constexpr std::size_t kEnsembleMembers = 2;
    std::array<std::shared_ptr<LearnedModel>, kEnsembleMembers>
        members;
    for (std::size_t member = 0; member < members.size(); ++member) {
        members[member] = std::make_shared<LearnedModel>(
            seed ^ (0x4D4F44454C000000ULL + member));
    }
    {
        std::array<std::thread, kEnsembleMembers> trainers;
        for (std::size_t member = 0; member < members.size();
             ++member) {
            trainers[member] = std::thread([&, member] {
                members[member]->train(
                    examples, 8, 0.015,
                    seed ^ (0x545241494E000000ULL + member));
            });
        }
        for (auto& trainer : trainers) {
            trainer.join();
        }
    }
    const auto make_ensemble = [&] {
        std::vector<std::shared_ptr<const LearnedModel>>
            ensemble_members;
        ensemble_members.reserve(members.size());
        for (const auto& member : members) {
            ensemble_members.push_back(member);
        }
        return std::make_shared<LearnedModel>(
            std::move(ensemble_members));
    };
    std::shared_ptr<const LearnedModel> model = make_ensemble();

    // Two fitted self-play iterations move the value function toward states
    // produced by its own policy while retaining the random-play replay set.
    for (std::size_t generation = 0; generation < 2; ++generation) {
        std::vector<LearnedModel::TrainingExample> self_play_examples;
        const std::size_t generation_games =
            std::max<std::size_t>(1, training_games / 2);
        self_play_examples.reserve(generation_games * 60);
        for (std::size_t game_index = 0;
             game_index < generation_games; ++game_index) {
            GameConfig config;
            config.learned_model = model;
            config.bots = {
                BotConfig{
                    .kind = BotKind::Learned,
                    .rollouts_per_action = 0,
                    .exploration_rate =
                        generation == 0 ? 0.10 : 0.05,
                },
                BotConfig{
                    .kind = BotKind::Learned,
                    .rollouts_per_action = 0,
                    .exploration_rate =
                        generation == 0 ? 0.10 : 0.05,
                },
            };
            const auto [first_deck, second_deck] =
                choose_distinct_decks();
            Game game(deck_cards(first_deck), deck_cards(second_deck),
                      random(), config);
            std::vector<GameState> trace;
            const GameResult result = game.run_with_trace(trace);
            add_trace(trace, result, self_play_examples);
        }
        examples.insert(examples.end(), self_play_examples.begin(),
                        self_play_examples.end());
        std::array<std::thread, kEnsembleMembers> trainers;
        for (std::size_t member = 0; member < members.size();
             ++member) {
            trainers[member] = std::thread([&, member] {
                members[member]->train(
                    examples, 3, 0.006,
                    seed ^ (0x53454C4600000000ULL +
                            0x100ULL * generation + member));
            });
        }
        for (auto& trainer : trainers) {
            trainer.join();
        }
        model = make_ensemble();
    }

    return model;
}

double BotBenchmarkSummary::challenger_win_rate() const {
    return challenger_stats.win_rate();
}

namespace {

std::pair<double, double> wilson_interval_95(std::size_t wins,
                                             std::size_t games) {
    if (games == 0) {
        return {0.0, 0.0};
    }
    constexpr double z = 1.959963984540054;
    constexpr double z_squared = z * z;
    const double count = static_cast<double>(games);
    const double proportion = static_cast<double>(wins) / count;
    const double denominator = 1.0 + z_squared / count;
    const double center =
        (proportion + z_squared / (2.0 * count)) / denominator;
    const double margin =
        z * std::sqrt((proportion * (1.0 - proportion) +
                       z_squared / (4.0 * count)) /
                      count) /
        denominator;
    return {
        100.0 * std::max(0.0, center - margin),
        100.0 * std::min(1.0, center + margin),
    };
}

} // namespace

double BotBenchmarkSummary::confidence_low_95() const {
    return wilson_interval_95(challenger_stats.wins,
                              challenger_stats.games)
        .first;
}

double BotBenchmarkSummary::confidence_high_95() const {
    return wilson_interval_95(challenger_stats.wins,
                              challenger_stats.games)
        .second;
}

bool BotBenchmarkSummary::challenger_is_better_95() const {
    return confidence_low_95() > 50.0;
}

BotBenchmarkSummary
run_bot_benchmark(std::size_t repetitions_per_deck_pairing,
                  std::uint64_t seed, BotConfig challenger,
                  BotConfig baseline, GameConfig game_config) {
    if (repetitions_per_deck_pairing == 0) {
        throw std::invalid_argument(
            "benchmark repetitions must be positive");
    }
    if (challenger.kind == baseline.kind &&
        challenger.rollouts_per_action ==
            baseline.rollouts_per_action) {
        throw std::invalid_argument(
            "benchmark bots must use different policies or rollout counts");
    }
    const bool uses_learned =
        challenger.kind == BotKind::Learned ||
        baseline.kind == BotKind::Learned;
    if (uses_learned && !game_config.learned_model) {
        const std::size_t training_games = std::max(
            challenger.kind == BotKind::Learned
                ? challenger.training_games
                : std::size_t{0},
            baseline.kind == BotKind::Learned
                ? baseline.training_games
                : std::size_t{0});
        game_config.learned_model = train_learned_model(
            training_games, seed ^ 0x42454E43484E4EULL);
    }

    BotBenchmarkSummary summary = {
        .challenger = challenger,
        .baseline = baseline,
        .repetitions_per_deck_pairing =
            repetitions_per_deck_pairing,
    };
    std::mt19937_64 seed_generator(seed);
    struct BenchmarkTask {
        std::size_t first_deck;
        std::size_t second_deck;
        std::size_t challenger_player;
        std::size_t baseline_player;
        std::size_t challenger_deck;
        std::size_t baseline_deck;
        std::size_t starting_player;
        std::uint64_t seed;
    };
    std::vector<BenchmarkTask> tasks;
    tasks.reserve(repetitions_per_deck_pairing * 40);
    for (std::size_t first_deck = 0; first_deck < 4; ++first_deck) {
        for (std::size_t second_deck = first_deck;
             second_deck < 4; ++second_deck) {
            for (std::size_t repetition = 0;
                 repetition < repetitions_per_deck_pairing;
                 ++repetition) {
                const std::uint64_t game_seed = seed_generator();
                for (std::size_t assignment = 0; assignment < 2;
                     ++assignment) {
                    const std::size_t challenger_player = assignment;
                    const std::size_t baseline_player =
                        opponent_of(challenger_player);
                    const std::size_t challenger_deck =
                        challenger_player == 0 ? first_deck
                                               : second_deck;
                    const std::size_t baseline_deck =
                        baseline_player == 0 ? first_deck
                                            : second_deck;

                    for (std::size_t starting_player = 0;
                         starting_player < 2; ++starting_player) {
                        tasks.push_back({
                            .first_deck = first_deck,
                            .second_deck = second_deck,
                            .challenger_player = challenger_player,
                            .baseline_player = baseline_player,
                            .challenger_deck = challenger_deck,
                            .baseline_deck = baseline_deck,
                            .starting_player = starting_player,
                            .seed = game_seed,
                        });
                    }
                }
            }
        }
    }

    const std::array<std::vector<CardId>, 4> decks = {
        deck_cards(DeckId::Green),
        deck_cards(DeckId::Red),
        deck_cards(DeckId::Blue),
        deck_cards(DeckId::White),
    };
    std::vector<GameResult> results(tasks.size());
    std::atomic_size_t next_task = 0;
    const std::size_t worker_count = std::min<std::size_t>(
        tasks.size(),
        std::max(1U, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t task_index =
                    next_task.fetch_add(1, std::memory_order_relaxed);
                if (task_index >= tasks.size()) {
                    return;
                }
                const auto& task = tasks[task_index];
                GameConfig current_config = game_config;
                current_config.starting_player =
                    task.starting_player;
                current_config.bots[task.challenger_player] =
                    challenger;
                current_config.bots[task.baseline_player] = baseline;
                Game game(decks[task.first_deck],
                          decks[task.second_deck], task.seed,
                          current_config);
                results[task_index] = game.run();
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    for (std::size_t task_index = 0; task_index < tasks.size();
         ++task_index) {
        const auto& task = tasks[task_index];
        const auto& result = results[task_index];
        ++summary.total_games;
        record_bot_result(summary.challenger_stats, result,
                          task.challenger_player);
        record_bot_result(summary.baseline_stats, result,
                          task.baseline_player);
        record_deck_result(
            summary.challenger_decks[task.challenger_deck], result,
            task.challenger_player);
        record_deck_result(
            summary.baseline_decks[task.baseline_deck], result,
            task.baseline_player);
    }

    return summary;
}

DeckEvolutionSummary evolve_deck(DeckEvolutionConfig config,
                                 std::uint64_t seed,
                                 GameConfig game_config) {
    if (config.generations == 0) {
        throw std::invalid_argument(
            "deck evolution generations must be positive");
    }
    if (config.population < 4) {
        throw std::invalid_argument(
            "deck evolution population must be at least four");
    }
    if (config.repetitions_per_opponent == 0) {
        throw std::invalid_argument(
            "deck evolution repetitions must be positive");
    }
    if ((config.pilot.kind == BotKind::MonteCarlo ||
         config.pilot.kind == BotKind::DeepMonteCarlo) &&
        config.pilot.rollouts_per_action == 0) {
        throw std::invalid_argument(
            "deck evolution Monte Carlo rollouts must be positive");
    }
    if (config.pilot.kind == BotKind::Learned &&
        !game_config.learned_model) {
        game_config.learned_model = train_learned_model(
            config.pilot.training_games,
            seed ^ 0x45564F4C56454E4EULL);
    }
    game_config.bots = {config.pilot, config.pilot};

    const std::array<std::vector<CardId>, 4> metagame = {
        deck_cards(DeckId::Green),
        deck_cards(DeckId::Red),
        deck_cards(DeckId::Blue),
        deck_cards(DeckId::White),
    };
    std::vector<CardId> card_pool;
    std::array<bool, kLearnedCardCount> seen_cards{};
    for (const auto& deck : metagame) {
        for (const CardId card : deck) {
            const auto index = static_cast<std::size_t>(card);
            if (!seen_cards[index]) {
                seen_cards[index] = true;
                card_pool.push_back(card);
            }
        }
    }

    std::mt19937_64 random(seed);
    std::uniform_int_distribution<std::size_t> choose_card(
        0, card_pool.size() - 1);
    const auto mutate = [&](std::vector<CardId>& deck,
                            std::size_t mutations) {
        std::uniform_int_distribution<std::size_t> choose_slot(
            0, deck.size() - 1);
        for (std::size_t mutation = 0; mutation < mutations;
             ++mutation) {
            deck[choose_slot(random)] = card_pool[choose_card(random)];
        }
    };

    std::vector<std::vector<CardId>> population;
    population.reserve(config.population);
    for (const auto& deck : metagame) {
        population.push_back(deck);
    }
    while (population.size() < config.population) {
        std::vector<CardId> candidate =
            metagame[population.size() % metagame.size()];
        mutate(candidate, 1 + population.size() % 8);
        population.push_back(std::move(candidate));
    }

    const std::uint64_t evaluation_seed =
        seed ^ 0x4556414C55415445ULL;
    const auto evaluate_population =
        [&](const std::vector<std::vector<CardId>>& candidates) {
            std::vector<EvolvedDeck> evaluations(candidates.size());
            std::atomic_size_t next_candidate = 0;
            const std::size_t worker_count =
                std::min<std::size_t>(
                    candidates.size(),
                    std::max(
                        1U, std::thread::hardware_concurrency()));
            std::vector<std::thread> workers;
            workers.reserve(worker_count);
            for (std::size_t worker = 0; worker < worker_count;
                 ++worker) {
                workers.emplace_back([&] {
                    while (true) {
                        const std::size_t candidate_index =
                            next_candidate.fetch_add(
                                1, std::memory_order_relaxed);
                        if (candidate_index >= candidates.size()) {
                            return;
                        }
                        auto& evaluation =
                            evaluations[candidate_index];
                        evaluation.cards =
                            candidates[candidate_index];
                        std::mt19937_64 game_seeds(
                            evaluation_seed);
                        for (std::size_t opponent = 0;
                             opponent < metagame.size(); ++opponent) {
                            for (std::size_t repetition = 0;
                                 repetition <
                                 config.repetitions_per_opponent;
                                 ++repetition) {
                                const std::uint64_t game_seed =
                                    game_seeds();
                                for (std::size_t candidate_player = 0;
                                     candidate_player < 2;
                                     ++candidate_player) {
                                    for (std::size_t starting_player = 0;
                                         starting_player < 2;
                                         ++starting_player) {
                                        GameConfig current_config =
                                            game_config;
                                        current_config.starting_player =
                                            starting_player;
                                        const bool candidate_first =
                                            candidate_player == 0;
                                        Game game(
                                            candidate_first
                                                ? candidates[candidate_index]
                                                : metagame[opponent],
                                            candidate_first
                                                ? metagame[opponent]
                                                : candidates[candidate_index],
                                            game_seed, current_config);
                                        const GameResult result =
                                            game.run();
                                        record_deck_result(
                                            evaluation.total, result,
                                            candidate_player);
                                        record_deck_result(
                                            evaluation
                                                .by_opponent[opponent],
                                            result, candidate_player);
                                    }
                                }
                            }
                        }
                    }
                });
            }
            for (auto& worker : workers) {
                worker.join();
            }
            return evaluations;
        };

    DeckEvolutionSummary summary;
    for (std::size_t generation = 0;
         generation < config.generations; ++generation) {
        auto evaluations = evaluate_population(population);
        std::vector<std::size_t> order(evaluations.size());
        for (std::size_t index = 0; index < order.size(); ++index) {
            order[index] = index;
        }
        std::sort(
            order.begin(), order.end(),
            [&](std::size_t left, std::size_t right) {
                const double left_rate =
                    evaluations[left].total.win_rate();
                const double right_rate =
                    evaluations[right].total.win_rate();
                if (left_rate != right_rate) {
                    return left_rate > right_rate;
                }
                return evaluations[left].cards <
                       evaluations[right].cards;
            });
        const EvolvedDeck& generation_best =
            evaluations[order.front()];
        summary.generation_best_win_rates.push_back(
            generation_best.total.win_rate());
        if (summary.best.cards.empty() ||
            generation_best.total.win_rate() >
                summary.best.total.win_rate()) {
            summary.best = generation_best;
        }

        const std::size_t elite_count =
            std::max<std::size_t>(2, config.population / 4);
        std::vector<std::vector<CardId>> next_population;
        next_population.reserve(config.population);
        for (std::size_t elite = 0; elite < elite_count;
             ++elite) {
            next_population.push_back(
                evaluations[order[elite]].cards);
        }
        std::uniform_int_distribution<std::size_t> choose_elite(
            0, elite_count - 1);
        std::uniform_int_distribution<std::size_t> mutation_count(1, 4);
        while (next_population.size() < config.population) {
            auto child =
                next_population[choose_elite(random)];
            mutate(child, mutation_count(random));
            next_population.push_back(std::move(child));
        }
        population = std::move(next_population);
    }
    return summary;
}

} // namespace alpha
