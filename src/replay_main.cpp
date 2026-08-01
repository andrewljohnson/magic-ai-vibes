// Replay generator: plays one bot-vs-bot game at a fixed seed and dumps
// every observed step (event + full public board from the watched seat)
// as JSON for the telemetry site's step-through viewer. --web reproduces
// the arena bridge exactly: SPZ on seat 1 with the bridge's policy seed.

#include "old_school/game.hpp"
#include "old_school/selfplay_zero.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace old_school;
namespace spz = old_school::selfplay_zero;

namespace {

std::string card_name(CardId card) {
    return std::string(card_definition(card).name);
}

std::string action_text(const PriorityAction& action) {
    switch (action.kind) {
    case PriorityActionKind::Pass: return "Pass priority";
    case PriorityActionKind::PlayLand:
        return "Play " + card_name(action.card);
    default: break;
    }
    std::string text = "Cast " + card_name(action.card);
    if (action.kind == PriorityActionKind::CastDisintegrate ||
        action.kind == PriorityActionKind::CastBraingeyser) {
        text += " (X=" + std::to_string(action.x_value) + ")";
    }
    if (action.target.has_value()) {
        text += action.target->creature.has_value()
                    ? " at creature #" +
                          std::to_string(*action.target->creature)
                    : (action.target->player == 0 ? " at player 0"
                                                  : " at player 1");
    }
    return text;
}

void write_string(std::ostream& out, const std::string& value) {
    out << '"';
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            out << '\\';
        }
        out << c;
    }
    out << '"';
}

void write_board(std::ostream& out, const PlayerObservation& obs) {
    out << "\"players\":[";
    for (std::size_t seat = 0; seat < 2; ++seat) {
        const auto& player = obs.players[seat];
        if (seat != 0) {
            out << ',';
        }
        out << "{\"life\":" << player.life << ",\"handSize\":"
            << player.hand_size << ",\"librarySize\":"
            << player.library_size << ",\"graveyard\":"
            << player.graveyard.size() << ",\"lands\":[";
        for (std::size_t i = 0; i < player.lands.size(); ++i) {
            if (i != 0) out << ',';
            out << "{\"name\":";
            write_string(out, card_name(player.lands[i].card));
            out << ",\"tapped\":"
                << (player.lands[i].tapped ? "true" : "false") << '}';
        }
        out << "],\"creatures\":[";
        for (std::size_t i = 0; i < player.creatures.size(); ++i) {
            const auto& creature = player.creatures[i];
            const auto& definition = card_definition(creature.card);
            if (i != 0) out << ',';
            out << "{\"id\":" << creature.id << ",\"name\":";
            write_string(out, card_name(creature.card));
            out << ",\"power\":"
                << definition.power + creature.temporary_power_bonus
                << ",\"toughness\":"
                << definition.toughness +
                       creature.temporary_toughness_bonus
                << ",\"damage\":" << creature.damage << ",\"tapped\":"
                << (creature.tapped ? "true" : "false") << ",\"sick\":"
                << (creature.summoning_sick ? "true" : "false") << '}';
        }
        out << "],\"artifacts\":[";
        for (std::size_t i = 0; i < player.artifacts.size(); ++i) {
            if (i != 0) out << ',';
            out << "{\"name\":";
            write_string(out, card_name(player.artifacts[i].card));
            out << ",\"tapped\":"
                << (player.artifacts[i].tapped ? "true" : "false")
                << '}';
        }
        out << "],\"enchantments\":[";
        for (std::size_t i = 0; i < player.enchantments.size(); ++i) {
            if (i != 0) out << ',';
            write_string(out, card_name(player.enchantments[i]));
        }
        out << "]}";
    }
    out << "],\"observerHand\":[";
    for (std::size_t i = 0; i < obs.hand.size(); ++i) {
        if (i != 0) out << ',';
        write_string(out, card_name(obs.hand[i]));
    }
    out << "],\"stack\":[";
    for (std::size_t i = 0; i < obs.stack.size(); ++i) {
        if (i != 0) out << ',';
        out << "{\"name\":";
        write_string(out, card_name(obs.stack[i].card));
        out << ",\"controller\":" << obs.stack[i].controller << '}';
    }
    out << ']';
}

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t seed = 1;
    std::size_t spz_deck = 1;
    std::size_t opp_deck = 0;
    bool mirror = false;
    bool swap_pilots = false;
    bool web = true;
    std::string name = "replay";
    for (int arg = 1; arg < argc; ++arg) {
        const std::string flag = argv[arg];
        if (flag == "--seed") {
            seed = std::stoull(argv[++arg]);
        } else if (flag == "--spz-deck") {
            spz_deck = std::stoul(argv[++arg]);
        } else if (flag == "--opp-deck") {
            opp_deck = std::stoul(argv[++arg]);
        } else if (flag == "--mirror") {
            mirror = true;
        } else if (flag == "--swap") {
            swap_pilots = true;
        } else if (flag == "--name") {
            name = argv[++arg];
        } else {
            std::cerr << "unknown flag " << flag << "\n";
            return 1;
        }
    }
    (void)web;

    const auto net = std::make_shared<const spz::SpzNet>(
        spz::load_spz_net("data/spz-champion-v11.txt"));
    std::shared_ptr<const spz::SpzAdvantageNet> advantage;
    {
        std::ifstream probe("data/spz-advantage-v8.txt");
        if (probe) {
            advantage = std::make_shared<const spz::SpzAdvantageNet>(
                spz::load_spz_advantage_net(
                    "data/spz-advantage-v8.txt"));
        }
    }
    const auto& decks = spz::spz_decks();
    const std::array<std::vector<CardId>, 2> game_decks = {
        decks[opp_deck], decks[spz_deck]};

    std::ostringstream steps;
    std::size_t step_count = 0;
    const auto observe = [&](const PlayerObservation& obs,
                             const GameEvent& event) {
        std::string message;
        const std::string actor =
            event.player == 0 ? "player 0" : "player 1";
        switch (event.kind) {
        case GameEventKind::TurnStarted:
            message = actor + " starts turn " +
                      std::to_string(obs.turn_number);
            break;
        case GameEventKind::PriorityActionSelected:
            message = actor + ": " +
                      (event.priority_action.has_value()
                           ? action_text(*event.priority_action)
                           : std::string("(unknown)"));
            break;
        case GameEventKind::StackObjectResolved:
            message = "Resolved " +
                      (event.stack_object.has_value()
                           ? card_name(event.stack_object->card)
                           : std::string("stack object"));
            break;
        case GameEventKind::AttackersDeclared: {
            message = actor + " attacks with " +
                      std::to_string(event.attackers.size()) +
                      " creature(s)";
            break;
        }
        case GameEventKind::BlockersDeclared:
            message = actor + " declares " +
                      std::to_string(event.blocks.size()) + " block(s)";
            break;
        case GameEventKind::DamageOrderChosen:
            message = "Combat damage assigned";
            break;
        case GameEventKind::CombatResolved:
            message = "Combat resolved";
            break;
        case GameEventKind::CardsDiscarded:
            message = actor + " discards " +
                      std::to_string(event.cards.size()) + " card(s)";
            break;
        case GameEventKind::MulliganTaken:
            message = actor + " takes a mulligan";
            break;
        }
        if (step_count != 0) {
            steps << ",\n";
        }
        steps << "{\"turn\":" << obs.turn_number << ",\"active\":"
              << obs.active_player << ",\"message\":";
        write_string(steps, message);
        steps << ',';
        write_board(steps, obs);
        steps << '}';
        step_count += 1;
    };

    GameConfig config;
    config.max_turns = 200;
    const std::size_t watched_seat = swap_pilots ? 0 : 1;
    for (std::size_t seat = 0; seat < 2; ++seat) {
        const bool spz_seat = seat == watched_seat || mirror;
        if (!spz_seat) {
            config.bots[seat] = {.kind = BotKind::Handcrafted,
                                 .rollouts_per_action = 1};
            continue;
        }
        spz::SpzPolicyConfig policy;
        policy.worlds = 4;
        policy.block_prediction_worlds = 4;
        policy.rollout = true;
        policy.gamma_per_turn = 0.98;
        if (advantage) {
        }
        // Match the arena bridge exactly for seat 1.
        policy.seed = seat == 1 ? (seed ^ 0x53505AULL)
                                : (seed ^ 0x53505AULL) + 1;
        HumanController controller = spz::make_spz_controller(
            net, game_decks, seat, policy, nullptr, nullptr, advantage);
        if (seat == watched_seat) {
            controller.observe = observe;
        }
        config.human_controllers[seat] = std::move(controller);
    }
    Game game(game_decks[0], game_decks[1], seed, config);
    const GameResult result = game.run();

    std::ofstream out("build/telemetry/replays/" + name + ".json");
    if (!out) {
        std::cerr << "cannot write replay (mkdir "
                     "build/telemetry/replays?)\n";
        return 1;
    }
    out << "{\"seed\":" << seed << ",\"observerSeat\":"
        << watched_seat << ",\"decks\":[";
    write_string(out, std::string(spz::spz_deck_name(opp_deck)));
    out << ',';
    write_string(out, std::string(spz::spz_deck_name(spz_deck)));
    out << "],\"pilots\":[";
    write_string(out, mirror || swap_pilots ? "spz" : "handcrafted");
    out << ',';
    write_string(out, mirror || !swap_pilots ? "spz" : "handcrafted");
    out << "],\"winner\":" << result.winner
        << ",\"turns\":" << result.turns << ",\"steps\":[\n"
        << steps.str() << "\n]}\n";
    // Maintain the static-site index of available replays.
    {
        std::vector<std::string> names;
        std::ifstream index_in("build/telemetry/replays/index.json");
        if (index_in) {
            std::string all((std::istreambuf_iterator<char>(index_in)),
                            std::istreambuf_iterator<char>());
            std::size_t at = 0;
            while ((at = all.find('"', at)) != std::string::npos) {
                const std::size_t end = all.find('"', at + 1);
                if (end == std::string::npos) {
                    break;
                }
                const std::string entry =
                    all.substr(at + 1, end - at - 1);
                if (entry != "replays" && entry != name) {
                    names.push_back(entry);
                }
                at = end + 1;
            }
        }
        names.push_back(name);
        std::ofstream index_out(
            "build/telemetry/replays/index.json");
        index_out << "{\"replays\":[";
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i != 0) {
                index_out << ',';
            }
            write_string(index_out, names[i]);
        }
        index_out << "]}\n";
    }
    std::cout << "wrote build/telemetry/replays/" << name << ".json ("
              << step_count << " steps, winner " << result.winner
              << ", " << result.turns << " turns)\n";
    return 0;
}
