// Pilot comparison: the same deck piloted by SPZ and by Handcrafted on
// identical seeds against the Handcrafted-piloted field. Aggregates the
// behavioral gap and narrates divergent games (seeds the rules pilot wins
// but the learned pilot loses) decision by decision from the SPZ side.

#include "old_school/game.hpp"
#include "old_school/selfplay_zero.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace old_school;
namespace spz = old_school::selfplay_zero;

namespace {

struct PilotStats {
    std::size_t games = 0;
    std::size_t wins = 0;
    double turns = 0.0;
    double lands = 0.0;
    double spells = 0.0;
    double damage = 0.0;
    double opp_graveyard = 0.0;
    double creatures_cast = 0.0;
    double attacks_declared = 0.0;
    double attackers_total = 0.0;
    std::map<int, std::size_t> disintegrate_x;
    std::size_t bolts_at_face = 0;
    std::size_t bolts_at_creatures = 0;
};

std::string action_label(const PriorityAction& action) {
    switch (action.kind) {
    case PriorityActionKind::Pass: return "pass";
    case PriorityActionKind::PlayLand: return "play land";
    case PriorityActionKind::CastCreature:
        return std::string("cast ") +
               std::string(card_definition(action.card).name);
    case PriorityActionKind::CastSorcery:
    case PriorityActionKind::CastArtifact:
    case PriorityActionKind::CastEnchantment:
        return std::string("cast ") +
               std::string(card_definition(action.card).name);
    case PriorityActionKind::CastLightningBolt:
        return action.target.has_value() &&
                       !action.target->creature.has_value()
                   ? "bolt FACE"
                   : "bolt creature";
    case PriorityActionKind::CastDisintegrate: return "disintegrate";
    case PriorityActionKind::CastGiantGrowth: return "giant growth";
    case PriorityActionKind::CastCounterspell: return "counterspell";
    case PriorityActionKind::CastAncestralRecall: return "ancestral";
    case PriorityActionKind::CastBraingeyser: return "braingeyser";
    case PriorityActionKind::CastForceSpike: return "force spike";
    case PriorityActionKind::ActivateMillstone: return "millstone";
    }
    return "other";
}

struct SeatTracker {
    std::size_t seat = 0;
    std::size_t creatures_cast = 0;
    std::size_t attacks_declared = 0;
    std::size_t attackers_total = 0;
    std::map<int, std::size_t> disintegrate_x;
    std::size_t bolts_at_face = 0;
    std::size_t bolts_at_creatures = 0;
    bool narrate = false;
    std::vector<std::string> lines;

    void on_event(const PlayerObservation& obs, const GameEvent& ev) {
        if (ev.kind == GameEventKind::PriorityActionSelected &&
            ev.player == seat && ev.priority_action.has_value()) {
            const PriorityAction& action = *ev.priority_action;
            if (action.kind == PriorityActionKind::CastCreature) {
                creatures_cast += 1;
            }
            if (action.kind == PriorityActionKind::CastDisintegrate) {
                disintegrate_x[action.x_value] += 1;
            }
            if (action.kind == PriorityActionKind::CastLightningBolt &&
                action.target.has_value()) {
                if (action.target->creature.has_value()) {
                    bolts_at_creatures += 1;
                } else {
                    bolts_at_face += 1;
                }
            }
            if (narrate &&
                action.kind != PriorityActionKind::Pass) {
                lines.push_back(
                    "  turn " + std::to_string(obs.turn_number) +
                    " life " +
                    std::to_string(obs.players[seat].life) + "/" +
                    std::to_string(obs.players[1 - seat].life) +
                    " hand " + std::to_string(obs.hand.size()) +
                    ": " + action_label(action) +
                    (action.kind ==
                             PriorityActionKind::CastDisintegrate
                         ? " X=" + std::to_string(action.x_value)
                         : ""));
            }
        }
        if (ev.kind == GameEventKind::AttackersDeclared &&
            ev.player == seat) {
            attacks_declared += ev.attackers.empty() ? 0 : 1;
            attackers_total += ev.attackers.size();
            if (narrate) {
                lines.push_back(
                    "  turn " + std::to_string(obs.turn_number) +
                    " life " +
                    std::to_string(obs.players[seat].life) + "/" +
                    std::to_string(obs.players[1 - seat].life) +
                    ": ATTACK with " +
                    std::to_string(ev.attackers.size()) + " of " +
                    std::to_string(
                        obs.players[seat].creatures.size()) +
                    " creatures");
            }
        }
    }
};


}  // namespace

int main(int argc, char** argv) {
    std::size_t deck_index = 4;  // RU Aggro
    std::size_t games_per_opponent = 12;
    std::uint64_t seed = 20260798;
    std::size_t narrations = 3;
    std::string model = "data/spz-champion-v9.txt";
    std::string advantage_path = "data/spz-advantage-v6.txt";
    for (int arg = 1; arg < argc; ++arg) {
        const std::string flag = argv[arg];
        if (flag == "--deck") {
            deck_index = std::stoul(argv[++arg]);
        } else if (flag == "--games") {
            games_per_opponent = std::stoul(argv[++arg]);
        } else if (flag == "--seed") {
            seed = std::stoull(argv[++arg]);
        } else if (flag == "--narrations") {
            narrations = std::stoul(argv[++arg]);
        } else if (flag == "--model") {
            model = argv[++arg];
        } else if (flag == "--advantage-path") {
            advantage_path = argv[++arg];
        } else {
            std::cerr << "unknown flag " << flag << "\n";
            return 1;
        }
    }

    const auto net =
        std::make_shared<const spz::SpzNet>(spz::load_spz_net(model));
    std::shared_ptr<const spz::SpzAdvantageNet> advantage;
    {
        std::ifstream probe(advantage_path);
        if (probe) {
            advantage = std::make_shared<const spz::SpzAdvantageNet>(
                spz::load_spz_advantage_net(advantage_path));
        }
    }
    const auto& decks = spz::spz_decks();

    PilotStats learned, rules;
    struct Divergence {
        std::size_t opponent_deck;
        std::uint64_t game_seed;
        std::size_t focus_seat;
    };
    std::vector<Divergence> divergences;

    std::size_t opponent_graveyard = 0;
    const auto play = [&](bool spz_pilots, std::size_t opponent_deck,
                          std::uint64_t game_seed,
                          std::size_t focus_seat,
                          SeatTracker* tracker) {
        const std::array<std::vector<CardId>, 2> game_decks =
            focus_seat == 0
                ? std::array<std::vector<CardId>, 2>{
                      decks[deck_index], decks[opponent_deck]}
                : std::array<std::vector<CardId>, 2>{
                      decks[opponent_deck], decks[deck_index]};
        GameConfig config;
        config.starting_player = game_seed % 2;
        config.bots = {
            BotConfig{.kind = BotKind::Handcrafted,
                      .rollouts_per_action = 1},
            BotConfig{.kind = BotKind::Handcrafted,
                      .rollouts_per_action = 1},
        };
        if (spz_pilots) {
            spz::SpzPolicyConfig policy;
            policy.worlds = 4;
            policy.block_prediction_worlds = 4;
            policy.rollout = true;
            policy.gamma_per_turn = 0.98;
            policy.seed = game_seed ^ 0xF0CA;
            HumanController controller = spz::make_spz_controller(
                net, game_decks, focus_seat, policy, nullptr, nullptr,
                advantage);
            if (tracker != nullptr) {
                const auto inner = controller.observe;
                controller.observe =
                    [tracker, inner](const PlayerObservation& obs,
                                     const GameEvent& ev) {
                        tracker->on_event(obs, ev);
                        if (inner) {
                            inner(obs, ev);
                        }
                    };
            }
            config.human_controllers[focus_seat] =
                std::move(controller);
        }
        Game game(game_decks[0], game_decks[1], game_seed, config);
        const GameResult result = game.run();
        opponent_graveyard = game.state()
                                 .players[1 - focus_seat]
                                 .graveyard.size();
        return result;
    };

    for (std::size_t opponent_deck = 0; opponent_deck < decks.size();
         ++opponent_deck) {
        if (opponent_deck == deck_index) {
            continue;
        }
        for (std::size_t game_index = 0;
             game_index < games_per_opponent; ++game_index) {
            const std::uint64_t game_seed =
                seed + 1000 * opponent_deck + game_index;
            const std::size_t focus_seat = game_index % 2;

            SeatTracker spz_track;
            spz_track.seat = focus_seat;
            const GameResult with_spz =
                play(true, opponent_deck, game_seed, focus_seat,
                     &spz_track);
            const std::size_t spz_opp_graveyard = opponent_graveyard;
            const GameResult with_rules =
                play(false, opponent_deck, game_seed, focus_seat,
                     nullptr);
            const std::size_t rules_opp_graveyard = opponent_graveyard;

            const auto absorb = [&](PilotStats& stats,
                                    const GameResult& result,
                                    const SeatTracker* track) {
                stats.games += 1;
                stats.wins +=
                    result.winner == static_cast<int>(focus_seat) ? 1
                                                                  : 0;
                stats.turns += static_cast<double>(result.turns);
                const auto& mine = result.player_stats[focus_seat];
                stats.lands += static_cast<double>(mine.lands_played);
                stats.spells += static_cast<double>(mine.spells_cast);
                stats.damage +=
                    static_cast<double>(mine.damage_to_opponent);
                stats.opp_graveyard += static_cast<double>(
                    track != nullptr ? spz_opp_graveyard
                                     : rules_opp_graveyard);
                if (track != nullptr) {
                    stats.creatures_cast +=
                        static_cast<double>(track->creatures_cast);
                    stats.attacks_declared +=
                        static_cast<double>(track->attacks_declared);
                    stats.attackers_total +=
                        static_cast<double>(track->attackers_total);
                    for (const auto& [x, count] :
                         track->disintegrate_x) {
                        stats.disintegrate_x[x] += count;
                    }
                    stats.bolts_at_face += track->bolts_at_face;
                    stats.bolts_at_creatures +=
                        track->bolts_at_creatures;
                }
            };
            absorb(learned, with_spz, &spz_track);
            absorb(rules, with_rules, nullptr);

            const bool spz_lost =
                with_spz.winner != static_cast<int>(focus_seat);
            const bool rules_won =
                with_rules.winner == static_cast<int>(focus_seat);
            if (spz_lost && rules_won) {
                divergences.push_back(
                    {opponent_deck, game_seed, focus_seat});
            }
        }
    }

    const auto report = [&](const char* label,
                            const PilotStats& stats) {
        const double games = static_cast<double>(stats.games);
        std::cout << label << ": wins " << stats.wins << "/"
                  << stats.games << " avg-turns "
                  << stats.turns / games << " lands "
                  << stats.lands / games << " spells "
                  << stats.spells / games << " damage-dealt "
                  << stats.damage / games << " opp-graveyard "
                  << stats.opp_graveyard / games << "\n";
    };
    report("spz-pilots ", learned);
    report("rules-pilot", rules);
    const double games = static_cast<double>(learned.games);
    std::cout << "spz combat: creature-casts/game "
              << learned.creatures_cast / games
              << " attacking-combats/game "
              << learned.attacks_declared / games
              << " attackers/combat "
              << (learned.attacks_declared > 0
                      ? learned.attackers_total /
                            learned.attacks_declared
                      : 0.0)
              << "\n";
    std::cout << "spz bolts: face " << learned.bolts_at_face
              << " creatures " << learned.bolts_at_creatures
              << "; disintegrate X:";
    for (const auto& [x, count] : learned.disintegrate_x) {
        std::cout << " X=" << x << ":" << count;
    }
    std::cout << "\n" << divergences.size()
              << " divergent games (rules won, spz lost)\n";

    for (std::size_t index = 0;
         index < divergences.size() && index < narrations; ++index) {
        const Divergence& div = divergences[index];
        std::cout << "\n--- divergence " << index << ": vs "
                  << spz::spz_deck_name(div.opponent_deck) << " seed "
                  << div.game_seed << " seat " << div.focus_seat
                  << " ---\n";
        SeatTracker narrator;
        narrator.seat = div.focus_seat;
        narrator.narrate = true;
        (void)play(true, div.opponent_deck, div.game_seed,
                   div.focus_seat, &narrator);
        for (const auto& line : narrator.lines) {
            std::cout << line << "\n";
        }
    }
    return 0;
}
