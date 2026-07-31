// Combo probe: lotus-combo mirror self-play under the deployed champion
// configuration, logging every combo-relevant decision. Answers: does the
// bot cast Channel, does it fire lethal Disintegrates, and where does it
// leave wins on the table?

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

struct ComboStats {
    std::size_t games = 0;
    std::size_t decisions = 0;
    std::size_t lotus_casts = 0;
    std::size_t channel_casts = 0;
    std::size_t channel_castable_decisions = 0;
    std::size_t lethal_line_open = 0;
    std::size_t lethal_line_taken_now = 0;
    // channel_active with a lethal X already payable: the kill is one
    // action away.
    std::size_t kill_available = 0;
    std::size_t kill_taken = 0;
    std::map<int, std::size_t> disintegrate_x;
    std::size_t wins_by_life = 0;
    std::size_t wins_by_mill = 0;
    std::size_t draws = 0;
    double total_turns = 0.0;
};

// A lethal Channel line is open when the seat holds Channel + Disintegrate
// and, with Channel active, could pay GG then X+R with X at or above the
// opponent's life while surviving on at least one life.
bool lethal_channel_line_open(const PublicPlayerState& me, int my_life,
                              const std::vector<CardId>& hand,
                              int opponent_life) {
    bool channel = false, disintegrate = false;
    for (const CardId card : hand) {
        channel = channel || card == CardId::Channel;
        disintegrate = disintegrate || card == CardId::Disintegrate;
    }
    if (!channel || !disintegrate) {
        return false;
    }
    PlayerState projected;
    projected.life = my_life;
    projected.channel_active = true;
    projected.lands = me.lands;
    projected.artifacts = me.artifacts;
    projected.mana_pool = me.mana_pool;
    // Channel's GG plus Disintegrate's red leave the rest as X.
    return maximum_available_mana(projected) - 3 >= opponent_life;
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t games = 20;
    std::uint64_t seed = 20260741;
    std::string model = "data/spz-champion-v7.txt";
    std::string advantage_path = "data/spz-advantage-v2.txt";
    bool use_advantage = true;
    std::size_t top_k = 5;
    bool verbose = false;
    for (int arg = 1; arg < argc; ++arg) {
        const std::string flag = argv[arg];
        if (flag == "--games") {
            games = std::stoul(argv[++arg]);
        } else if (flag == "--seed") {
            seed = std::stoull(argv[++arg]);
        } else if (flag == "--model") {
            model = argv[++arg];
        } else if (flag == "--top-k") {
            top_k = std::stoul(argv[++arg]);
        } else if (flag == "--advantage-path") {
            advantage_path = argv[++arg];
        } else if (flag == "--no-advantage") {
            use_advantage = false;
        } else if (flag == "--verbose") {
            verbose = true;
        } else {
            std::cerr << "unknown flag " << flag << "\n";
            return 1;
        }
    }

    const auto net =
        std::make_shared<const spz::SpzNet>(spz::load_spz_net(model));
    std::shared_ptr<const spz::SpzAdvantageNet> advantage;
    if (use_advantage) {
        std::ifstream probe(advantage_path);
        if (probe) {
            advantage = std::make_shared<const spz::SpzAdvantageNet>(
                spz::load_spz_advantage_net(advantage_path));
        }
    }

    const auto deck = lotus_combo_deck();
    const std::array<std::vector<CardId>, 2> game_decks = {deck, deck};
    ComboStats stats;

    for (std::size_t game_index = 0; game_index < games; ++game_index) {
        GameConfig config;
        for (std::size_t seat = 0; seat < 2; ++seat) {
            spz::SpzPolicyConfig policy;
            policy.worlds = 4;
            policy.block_prediction_worlds = 4;
            policy.rollout = true;
            policy.rollout_top_k = top_k;
            policy.gamma_per_turn = 0.98;
            policy.seed = seed + 31 * game_index + seat;
            if (advantage) {
                policy.pass_dominance_prune = false;
                policy.advantage_scale = 0.6;
            }
            HumanController controller = spz::make_spz_controller(
                net, game_decks, seat, policy, nullptr, nullptr,
                advantage);
            const auto inner = controller.choose_priority_action;
            controller.choose_priority_action =
                [&stats, inner, seat, game_index, verbose](
                    const PlayerObservation& observation,
                    TurnPhase phase,
                    const std::vector<PriorityAction>& actions) {
                    const std::size_t chosen =
                        inner(observation, phase, actions);
                    stats.decisions += 1;
                    const auto& me =
                        observation.players[observation.observer];
                    const auto& opponent =
                        observation.players[1 - observation.observer];
                    bool channel_castable = false;
                    for (const PriorityAction& action : actions) {
                        channel_castable =
                            channel_castable ||
                            (action.kind ==
                                 PriorityActionKind::CastSorcery &&
                             action.card == CardId::Channel);
                    }
                    stats.channel_castable_decisions +=
                        channel_castable ? 1 : 0;
                    const bool lethal_open = lethal_channel_line_open(
                        me, me.life, observation.hand, opponent.life);
                    stats.lethal_line_open += lethal_open ? 1 : 0;
                    bool kill_now = false;
                    for (const PriorityAction& candidate : actions) {
                        kill_now =
                            kill_now ||
                            (candidate.kind ==
                                 PriorityActionKind::CastDisintegrate &&
                             candidate.target.has_value() &&
                             !candidate.target->creature.has_value() &&
                             candidate.target->player !=
                                 observation.observer &&
                             candidate.x_value >= opponent.life);
                    }
                    stats.kill_available += kill_now ? 1 : 0;

                    const PriorityAction& action = actions[chosen];
                    if (action.kind == PriorityActionKind::CastArtifact &&
                        action.card == CardId::BlackLotus) {
                        stats.lotus_casts += 1;
                    }
                    if (action.kind == PriorityActionKind::CastSorcery &&
                        action.card == CardId::Channel) {
                        stats.channel_casts += 1;
                        stats.lethal_line_taken_now +=
                            lethal_open ? 1 : 0;
                    }
                    if (action.kind ==
                        PriorityActionKind::CastDisintegrate) {
                        stats.disintegrate_x[action.x_value] += 1;
                        stats.kill_taken +=
                            (kill_now &&
                             action.x_value >= opponent.life)
                                ? 1
                                : 0;
                    }
                    if (verbose && kill_now &&
                        !(action.kind ==
                              PriorityActionKind::CastDisintegrate &&
                          action.x_value >= opponent.life)) {
                        std::cout
                            << "game " << game_index << " seat " << seat
                            << " turn " << observation.turn_number
                            << " KILL AVAILABLE, chose kind "
                            << static_cast<int>(action.kind) << " x "
                            << action.x_value << "\n";
                    }
                    if (verbose && lethal_open) {
                        std::cout
                            << "game " << game_index << " seat " << seat
                            << " turn " << observation.turn_number
                            << " life " << me.life << "/"
                            << opponent.life << " lethal-line OPEN -> "
                            << (action.kind ==
                                        PriorityActionKind::CastSorcery &&
                                    action.card == CardId::Channel
                                    ? "CAST CHANNEL"
                                : action.kind == PriorityActionKind::
                                                     CastDisintegrate
                                    ? "disintegrate X=" +
                                          std::to_string(action.x_value)
                                : action.kind == PriorityActionKind::Pass
                                    ? "PASS"
                                    : "other")
                            << "\n";
                    }
                    return chosen;
                };
            config.human_controllers[seat] = std::move(controller);
        }
        Game game(game_decks[0], game_decks[1],
                  seed + 977 * game_index, std::move(config));
        const GameResult result = game.run();
        stats.games += 1;
        stats.total_turns += static_cast<double>(result.turns);
        if (result.winner < 0) {
            stats.draws += 1;
        } else if (result.reason == EndReason::LifeTotal) {
            stats.wins_by_life += 1;
        } else if (result.reason == EndReason::EmptyLibrary) {
            stats.wins_by_mill += 1;
        }
    }

    std::cout << "games " << stats.games << " avg-turns "
              << stats.total_turns / static_cast<double>(stats.games)
              << " wins-by-life " << stats.wins_by_life
              << " wins-by-mill " << stats.wins_by_mill << " draws "
              << stats.draws << "\n";
    std::cout << "decisions " << stats.decisions << " lotus-casts "
              << stats.lotus_casts << " channel-castable "
              << stats.channel_castable_decisions << " channel-casts "
              << stats.channel_casts << "\n";
    std::cout << "lethal-line-open-decisions " << stats.lethal_line_open
              << " channel-cast-when-lethal "
              << stats.lethal_line_taken_now << "\n";
    std::cout << "kill-available-decisions " << stats.kill_available
              << " kill-taken " << stats.kill_taken << "\n";
    std::cout << "disintegrate X histogram:";
    for (const auto& [x, count] : stats.disintegrate_x) {
        std::cout << " X=" << x << ":" << count;
    }
    std::cout << "\n";
    return 0;
}
