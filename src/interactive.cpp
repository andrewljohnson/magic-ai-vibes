#include "old_school/interactive.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <istream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school {
namespace {

class InteractiveQuit : public std::exception {
  public:
    explicit InteractiveQuit(bool input_closed)
        : input_closed_(input_closed) {}

    bool input_closed() const { return input_closed_; }

    const char* what() const noexcept override {
        return input_closed_ ? "interactive input closed"
                             : "interactive game abandoned";
    }

  private:
    bool input_closed_ = false;
};

std::string_view trim(std::string_view text) {
    const auto whitespace = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    while (!text.empty() &&
           whitespace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           whitespace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::string lowercase(std::string_view text) {
    std::string result(text);
    std::transform(
        result.begin(), result.end(), result.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return result;
}

std::string_view phase_name(TurnPhase phase) {
    switch (phase) {
    case TurnPhase::FirstMain:
        return "first main";
    case TurnPhase::BeginCombat:
        return "beginning of combat";
    case TurnPhase::DeclareAttackers:
        return "declare attackers";
    case TurnPhase::DeclareBlockers:
        return "declare blockers";
    case TurnPhase::DamageOrder:
        return "combat damage order";
    case TurnPhase::EndCombat:
        return "end of combat";
    case TurnPhase::SecondMain:
        return "second main";
    }
    throw std::logic_error("unknown turn phase");
}

std::string_view end_reason_name(EndReason reason) {
    switch (reason) {
    case EndReason::LifeTotal:
        return "life total";
    case EndReason::EmptyLibrary:
        return "empty library";
    case EndReason::TurnLimit:
        return "turn limit";
    }
    throw std::logic_error("unknown game end reason");
}

std::string player_name(const PlayerObservation& observation,
                        std::size_t player) {
    return player == observation.observer ? "You" : "Learned";
}

struct LocatedCreature {
    std::size_t controller = 0;
    const CreaturePermanent* creature = nullptr;
};

std::optional<LocatedCreature>
find_creature(const PlayerObservation& observation,
              PermanentId permanent) {
    for (std::size_t player = 0;
         player < observation.players.size(); ++player) {
        const auto& creatures =
            observation.players[player].creatures;
        const auto found = std::find_if(
            creatures.begin(), creatures.end(),
            [permanent](const CreaturePermanent& creature) {
                return creature.id == permanent;
            });
        if (found != creatures.end()) {
            return LocatedCreature{
                .controller = player,
                .creature = &*found,
            };
        }
    }
    return std::nullopt;
}

std::string creature_name(const PlayerObservation& observation,
                          PermanentId permanent,
                          bool include_controller = true) {
    const auto located = find_creature(observation, permanent);
    if (!located.has_value()) {
        return "creature #" + std::to_string(permanent);
    }
    std::string result;
    if (include_controller) {
        result += located->controller == observation.observer
                      ? "your "
                      : "Learned's ";
    }
    result += card_definition(located->creature->card).name;
    result += " #" + std::to_string(permanent);
    return result;
}

std::string target_name(const PlayerObservation& observation,
                        const Target& target) {
    if (target.creature.has_value()) {
        return creature_name(
            observation, *target.creature);
    }
    return target.player == observation.observer
               ? "you"
               : "Learned";
}

std::string stack_object_name(
    const PlayerObservation& observation,
    const StackObject& object) {
    std::string result =
        player_name(observation, object.controller) + "'s " +
        std::string(card_definition(object.card).name);
    if (object.card == CardId::Disintegrate) {
        result += " (X=" + std::to_string(object.x_value) + ")";
    }
    if (object.target.has_value()) {
        result += " -> " +
                  target_name(observation, *object.target);
    }
    if (object.spell_target.has_value()) {
        result += " -> spell #" +
                  std::to_string(*object.spell_target);
    }
    if (object.kind == StackObjectKind::ActivatedAbility) {
        result += " ability";
    }
    result += " [stack #" + std::to_string(object.id) + "]";
    return result;
}

std::string action_name(const PlayerObservation& observation,
                        const PriorityAction& action) {
    const std::string card(card_definition(action.card).name);
    switch (action.kind) {
    case PriorityActionKind::Pass:
        return "Pass priority";
    case PriorityActionKind::PlayLand:
        return "Play " + card;
    case PriorityActionKind::CastCreature:
    case PriorityActionKind::CastSorcery:
    case PriorityActionKind::CastArtifact:
    case PriorityActionKind::CastEnchantment:
        return "Cast " + card;
    case PriorityActionKind::CastLightningBolt:
    case PriorityActionKind::CastGiantGrowth:
        return "Cast " + card + " -> " +
               target_name(observation, *action.target);
    case PriorityActionKind::CastDisintegrate:
        return "Cast " + card + " (X=" +
               std::to_string(action.x_value) + ") -> " +
               target_name(observation, *action.target);
    case PriorityActionKind::CastCounterspell:
        return "Cast " + card + " -> spell #" +
               std::to_string(*action.spell_target);
    case PriorityActionKind::ActivateMillstone:
        return "Activate " + card + " #" +
               std::to_string(*action.source_permanent) + " -> " +
               target_name(observation, *action.target);
    }
    throw std::logic_error("unknown priority action");
}

void print_card_list(std::ostream& output,
                     const std::vector<CardId>& cards,
                     std::string_view empty_label = "(none)") {
    if (cards.empty()) {
        output << empty_label;
        return;
    }
    std::array<std::size_t, kCardCount> counts{};
    for (const CardId card : cards) {
        ++counts[static_cast<std::size_t>(card)];
    }
    bool first = true;
    for (std::size_t card = 0; card < counts.size(); ++card) {
        if (counts[card] == 0) {
            continue;
        }
        if (!first) {
            output << ", ";
        }
        output << card_definition(static_cast<CardId>(card)).name;
        if (counts[card] > 1) {
            output << " x" << counts[card];
        }
        first = false;
    }
}

void print_lands(std::ostream& output,
                 const std::vector<LandPermanent>& lands) {
    if (lands.empty()) {
        output << "(none)";
        return;
    }
    for (std::size_t index = 0; index < lands.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << card_definition(lands[index].card).name
               << (lands[index].tapped ? " [tapped]"
                                       : " [untapped]");
    }
}

void print_creatures(std::ostream& output,
                     const std::vector<CreaturePermanent>& creatures) {
    if (creatures.empty()) {
        output << "(none)";
        return;
    }
    for (std::size_t index = 0; index < creatures.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        const auto& creature = creatures[index];
        const auto& definition = card_definition(creature.card);
        output << definition.name << " #" << creature.id << ' '
               << definition.power +
                      creature.temporary_power_bonus
               << '/'
               << definition.toughness +
                      creature.temporary_toughness_bonus;
        if (creature.tapped) {
            output << " [tapped]";
        }
        if (creature.summoning_sick) {
            output << " [summoning sick]";
        }
        if (creature.damage != 0) {
            output << " [damage " << creature.damage << ']';
        }
        if (creature.temporary_power_bonus != 0 ||
            creature.temporary_toughness_bonus != 0) {
            output << " [temporary "
                   << (creature.temporary_power_bonus >= 0 ? "+" : "")
                   << creature.temporary_power_bonus << '/'
                   << (creature.temporary_toughness_bonus >= 0 ? "+" : "")
                   << creature.temporary_toughness_bonus << ']';
        }
    }
}

void print_artifacts(std::ostream& output,
                     const std::vector<ArtifactPermanent>& artifacts) {
    if (artifacts.empty()) {
        output << "(none)";
        return;
    }
    for (std::size_t index = 0; index < artifacts.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << card_definition(artifacts[index].card).name
               << " #" << artifacts[index].id;
        if (artifacts[index].tapped) {
            output << " [tapped]";
        }
    }
}

void print_player(std::ostream& output,
                  const PlayerObservation& observation,
                  std::size_t player) {
    const auto& state = observation.players[player];
    output << (player == observation.observer ? "You" : "Learned")
           << " — life " << state.life
           << ", library " << state.library_size
           << ", hand ";
    if (player == observation.observer) {
        output << state.hand_size << ": ";
        print_card_list(output, observation.hand);
    } else {
        output << state.hand_size << " cards (hidden)";
    }
    output << "\n  Lands: ";
    print_lands(output, state.lands);
    output << "\n  Creatures: ";
    print_creatures(output, state.creatures);
    output << "\n  Artifacts: ";
    print_artifacts(output, state.artifacts);
    output << "\n  Enchantments: ";
    print_card_list(output, state.enchantments);
    output << "\n  Graveyard: ";
    print_card_list(output, state.graveyard);
    output << "\n  Exile: ";
    print_card_list(output, state.exile);
    output << '\n';
}

void print_state(std::ostream& output,
                 const PlayerObservation& observation) {
    output << "\nState\n";
    print_player(output, observation, observation.observer);
    print_player(output, observation, 1 - observation.observer);
    output << "Stack (top first): ";
    if (observation.stack.empty()) {
        output << "(empty)\n";
        return;
    }
    output << '\n';
    for (auto object = observation.stack.rbegin();
         object != observation.stack.rend(); ++object) {
        output << "  " << stack_object_name(observation, *object)
               << '\n';
    }
}

class TerminalSession {
  public:
    TerminalSession(std::istream& input, std::ostream& output)
        : input_(input), output_(output) {}

    HumanController controller() {
        return {
            .choose_priority_action =
                [this](const PlayerObservation& observation,
                       TurnPhase phase,
                       const std::vector<PriorityAction>& actions) {
                    return choose_priority(
                        observation, phase, actions);
                },
            .choose_attackers =
                [this](const PlayerObservation& observation,
                       const std::vector<PermanentId>& attackers) {
                    return choose_attackers(
                        observation, attackers);
                },
            .choose_blockers =
                [this](
                    const PlayerObservation& observation,
                    const std::vector<PermanentId>& attackers,
                    const std::vector<LegalBlockerChoice>& blockers) {
                    return choose_blockers(
                        observation, attackers, blockers);
                },
            .choose_damage_order =
                [this](const PlayerObservation& observation,
                       PermanentId attacker,
                       const std::vector<PermanentId>& blockers) {
                    return choose_damage_order(
                        observation, attacker, blockers);
                },
            .observe =
                [this](const PlayerObservation& observation,
                       const GameEvent& event) {
                    observe(observation, event);
                },
        };
    }

    void print_final(const PlayerObservation& observation,
                     const GameResult& result) {
        output_ << "\n=== Game over ===\n";
        if (result.winner < 0) {
            output_ << "Result: Draw\n";
        } else if (result.winner ==
                   static_cast<int>(observation.observer)) {
            output_ << "Result: You win\n";
        } else {
            output_ << "Result: Learned wins\n";
        }
        output_ << "Reason: " << end_reason_name(result.reason)
                << "\nIndividual turns: " << result.turns << '\n';
        print_state(output_, observation);
    }

  private:
    std::size_t read_index(std::string_view prompt,
                           std::size_t maximum) {
        while (true) {
            output_ << prompt << std::flush;
            std::string line;
            if (!std::getline(input_, line)) {
                throw InteractiveQuit(true);
            }
            const std::string_view value = trim(line);
            const std::string lowered = lowercase(value);
            if (lowered == "q" || lowered == "quit") {
                throw InteractiveQuit(false);
            }

            std::size_t chosen = 0;
            const auto parsed = std::from_chars(
                value.data(), value.data() + value.size(),
                chosen);
            if (parsed.ec == std::errc{} &&
                parsed.ptr == value.data() + value.size() &&
                chosen <= maximum) {
                return chosen;
            }
            output_ << "Please enter a number from 0 to "
                    << maximum << ", or q to quit.\n";
        }
    }

    void render_decision_state(
        const PlayerObservation& observation) {
        if (!last_rendered_.has_value() ||
            *last_rendered_ != observation) {
            print_state(output_, observation);
            last_rendered_ = observation;
        }
    }

    std::size_t choose_priority(
        const PlayerObservation& observation, TurnPhase phase,
        const std::vector<PriorityAction>& actions) {
        render_decision_state(observation);
        output_ << "\n" << phase_name(phase)
                << " — you have priority\nActions\n";
        for (std::size_t index = 0; index < actions.size(); ++index) {
            output_ << "  " << index << ". "
                    << action_name(observation, actions[index])
                    << '\n';
        }
        return read_index("choice> ", actions.size() - 1);
    }

    std::vector<PermanentId> choose_attackers(
        const PlayerObservation& observation,
        const std::vector<PermanentId>& legal_attackers) {
        render_decision_state(observation);
        if (legal_attackers.empty()) {
            output_ << "You have no legal attackers.\n";
            return {};
        }
        output_ << "\nDeclare attackers\n";
        std::vector<PermanentId> selected;
        for (const PermanentId attacker : legal_attackers) {
            output_ << "  " << creature_name(
                observation, attacker, false)
                    << "\n  0. Stay home\n  1. Attack\n";
            if (read_index("choice> ", 1) == 1) {
                selected.push_back(attacker);
            }
        }
        return selected;
    }

    std::vector<std::pair<PermanentId, PermanentId>>
    choose_blockers(
        const PlayerObservation& observation,
        const std::vector<PermanentId>& attackers,
        const std::vector<LegalBlockerChoice>& blockers) {
        render_decision_state(observation);
        output_ << "\nChoose blockers against: ";
        for (std::size_t index = 0; index < attackers.size(); ++index) {
            if (index != 0) {
                output_ << ", ";
            }
            output_ << creature_name(
                observation, attackers[index], false);
        }
        output_ << '\n';
        if (blockers.empty()) {
            output_ << "You have no legal blockers.\n";
            return {};
        }

        std::vector<std::pair<PermanentId, PermanentId>> selected;
        for (const auto& blocker : blockers) {
            output_ << "\nBlock with "
                    << creature_name(
                           observation, blocker.blocker, false)
                    << "\n  0. Do not block\n";
            for (std::size_t index = 0;
                 index < blocker.legal_attackers.size(); ++index) {
                output_ << "  " << index + 1 << ". Block "
                        << creature_name(
                               observation,
                               blocker.legal_attackers[index],
                               false)
                        << '\n';
            }
            const std::size_t chosen = read_index(
                "choice> ", blocker.legal_attackers.size());
            if (chosen != 0) {
                selected.emplace_back(
                    blocker.legal_attackers[chosen - 1],
                    blocker.blocker);
            }
        }
        return selected;
    }

    std::vector<PermanentId> choose_damage_order(
        const PlayerObservation& observation, PermanentId attacker,
        const std::vector<PermanentId>& blockers) {
        render_decision_state(observation);
        output_ << "\nChoose damage order for "
                << creature_name(observation, attacker, false)
                << " (first creature receives damage first)\n";
        std::vector<PermanentId> remaining = blockers;
        std::vector<PermanentId> ordered;
        ordered.reserve(blockers.size());
        while (!remaining.empty()) {
            for (std::size_t index = 0;
                 index < remaining.size(); ++index) {
                output_ << "  " << index << ". "
                        << creature_name(
                               observation, remaining[index], false)
                        << '\n';
            }
            const std::size_t chosen =
                read_index("choice> ", remaining.size() - 1);
            ordered.push_back(remaining[chosen]);
            remaining.erase(
                remaining.begin() +
                static_cast<std::ptrdiff_t>(chosen));
        }
        return ordered;
    }

    void observe(const PlayerObservation& observation,
                 const GameEvent& event) {
        switch (event.kind) {
        case GameEventKind::TurnStarted:
            output_ << "\n=== Turn " << observation.turn_number
                    << " — "
                    << (event.player == observation.observer
                            ? "your turn"
                            : "Learned's turn")
                    << " ===\n";
            print_state(output_, observation);
            last_rendered_ = observation;
            return;
        case GameEventKind::PriorityActionSelected:
            if (event.priority_action.has_value() &&
                event.priority_action->kind !=
                    PriorityActionKind::Pass) {
                output_ << player_name(observation, event.player)
                        << ": "
                        << action_name(
                               observation,
                               *event.priority_action)
                        << '\n';
            }
            return;
        case GameEventKind::StackObjectResolved:
            output_ << "Resolved: "
                    << stack_object_name(
                           observation, *event.stack_object)
                    << '\n';
            return;
        case GameEventKind::AttackersDeclared:
            if (event.attackers.empty()) {
                if (event.player != observation.observer) {
                    output_ << "Learned declares no attackers.\n";
                }
                return;
            }
            output_ << player_name(observation, event.player)
                    << (event.player == observation.observer
                            ? " declare attackers: "
                            : " declares attackers: ");
            for (std::size_t index = 0;
                 index < event.attackers.size(); ++index) {
                if (index != 0) {
                    output_ << ", ";
                }
                output_ << creature_name(
                    observation, event.attackers[index], false);
            }
            output_ << '\n';
            return;
        case GameEventKind::BlockersDeclared:
            if (event.blocks.empty()) {
                output_ << player_name(observation, event.player)
                        << (event.player == observation.observer
                                ? " declare no blockers.\n"
                                : " declares no blockers.\n");
                return;
            }
            output_ << player_name(observation, event.player)
                    << (event.player == observation.observer
                            ? " declare blockers: "
                            : " declares blockers: ");
            for (std::size_t index = 0;
                 index < event.blocks.size(); ++index) {
                if (index != 0) {
                    output_ << ", ";
                }
                output_ << creature_name(
                               observation,
                               event.blocks[index].second, false)
                        << " blocks "
                        << creature_name(
                               observation,
                               event.blocks[index].first, false);
            }
            output_ << '\n';
            return;
        case GameEventKind::DamageOrderChosen:
            for (std::size_t index = 0;
                 index < event.blocks.size();) {
                const PermanentId attacker =
                    event.blocks[index].first;
                std::size_t end = index + 1;
                while (end < event.blocks.size() &&
                       event.blocks[end].first == attacker) {
                    ++end;
                }
                if (end - index > 1) {
                    output_ << "Damage order for "
                            << creature_name(
                                   observation, attacker, false)
                            << ": ";
                    for (std::size_t blocker = index;
                         blocker < end; ++blocker) {
                        if (blocker != index) {
                            output_ << ", ";
                        }
                        output_ << creature_name(
                            observation,
                            event.blocks[blocker].second, false);
                    }
                    output_ << '\n';
                }
                index = end;
            }
            return;
        case GameEventKind::CombatResolved:
            output_ << "Combat resolves.\n";
            return;
        }
        throw std::logic_error("unknown interactive game event");
    }

    std::istream& input_;
    std::ostream& output_;
    std::optional<PlayerObservation> last_rendered_;
};

} // namespace

InteractiveMatchResult run_interactive_match(
    std::istream& input, std::ostream& output, std::uint64_t seed,
    std::shared_ptr<const LearnedModel> learned_model) {
    if (!learned_model) {
        throw std::invalid_argument(
            "interactive match requires a frozen Learned model");
    }
    TerminalSession terminal(input, output);
    GameConfig config;
    config.bots[0] = {
        .kind = BotKind::Random,
        .learned_variant = LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 1,
    };
    config.bots[1] = {
        .kind = BotKind::Learned,
        .learned_variant = LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 2,
        .learned_model = learned_model,
    };
    config.learned_model = learned_model;
    config.human_controllers[0] = terminal.controller();

    Game game(ru_aggro_deck(), ru_aggro_deck(), seed, config);
    try {
        const GameResult result = game.run();
        const auto observation = observe_game_state(game.state(), 0);
        terminal.print_final(observation, result);
        return {
            .abandoned = false,
            .game = result,
        };
    } catch (const InteractiveQuit& quit) {
        output << (quit.input_closed() ? "\nInput closed; game abandoned.\n"
                                      : "Game abandoned.\n");
        return {
            .abandoned = true,
            .game = std::nullopt,
        };
    }
}

} // namespace old_school
