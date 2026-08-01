// Plays the deployed champion against Handcrafted across seeds and flags
// objectively questionable plays from the public event stream.
#include "old_school/game.hpp"
#include "old_school/selfplay_zero.hpp"

#include <iostream>
#include <algorithm>
#include <map>
#include <tuple>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

using namespace old_school;
using namespace old_school::selfplay_zero;

namespace {

struct Snapshot {
    GameEvent event;
    PlayerObservation observation;  // from the SPZ seat's perspective
};

int power_of(const CreaturePermanent& c) {
    return card_definition(c.card).power + c.temporary_power_bonus;
}
int toughness_of(const CreaturePermanent& c) {
    return card_definition(c.card).toughness + c.temporary_toughness_bonus;
}

const CreaturePermanent* by_id(const std::vector<CreaturePermanent>& v,
                               PermanentId id) {
    for (const auto& c : v)
        if (c.id == id) return &c;
    return nullptr;
}

std::string cname(const std::vector<CreaturePermanent>& v, PermanentId id) {
    const auto* c = by_id(v, id);
    return c ? std::string(card_definition(c->card).name) + "#" +
                   std::to_string(id)
             : "#" + std::to_string(id);
}

struct Auditor {
    std::size_t spz_seat;
    std::uint64_t seed;
    std::vector<Snapshot> trail;
    std::vector<std::string> flags;
    std::optional<std::tuple<std::size_t, PermanentId, bool>>
        pending_growth;
    bool cast_a_spell_this_turn = false;

    void flag(std::size_t turn, const std::string& what) {
        std::ostringstream line;
        line << "seed " << seed << " seat " << spz_seat << " turn "
             << turn << ": " << what;
        flags.push_back(line.str());
    }

    void on_event(const PlayerObservation& obs, const GameEvent& ev) {
        analyze(obs, ev);
        trail.push_back({ev, obs});
    }

    // Fired at each event with the observation AFTER the public change.
    void analyze(const PlayerObservation& obs, const GameEvent& ev) {
        const auto& mine = obs.players[spz_seat].creatures;
        const auto& theirs = obs.players[1 - spz_seat].creatures;

        if (ev.kind == GameEventKind::AttackersDeclared &&
            ev.player == spz_seat) {
            // Missed lethal with no possible blockers.
            int ready = 0;
            std::vector<PermanentId> ready_ids;
            for (const auto& c : mine) {
                if (!c.tapped && !c.summoning_sick) {
                    ready += power_of(c);
                    ready_ids.push_back(c.id);
                }
            }
            bool moat = false;
            for (std::size_t p = 0; p < 2; ++p)
                for (const CardId e : obs.players[p].enchantments)
                    if (e == CardId::Moat) moat = true;
            bool blockers_possible = false;
            for (const auto& c : theirs)
                if (!c.tapped) blockers_possible = true;
            const int opp_life = obs.players[1 - spz_seat].life;
            if (!moat && !blockers_possible && ready >= opp_life &&
                opp_life > 0) {
                int declared_power = 0;
                for (const PermanentId id : ev.attackers) {
                    if (const auto* c = by_id(mine, id))
                        declared_power += power_of(c ? *c : mine[0]);
                }
                if (declared_power < opp_life) {
                    std::ostringstream detail;
                    detail << "MISSED LETHAL: ready power " << ready
                           << " vs life " << opp_life
                           << ", declared " << declared_power
                           << " | my creatures:";
                    for (const auto& c : mine) {
                        detail << ' '
                               << card_definition(c.card).name << '#'
                               << c.id << '('
                               << power_of(c) << '/'
                               << toughness_of(c) - c.damage
                               << (c.tapped ? ",T" : "")
                               << (c.summoning_sick ? ",S" : "") << ')';
                    }
                    detail << " | their creatures:";
                    for (const auto& c : theirs) {
                        detail << ' '
                               << card_definition(c.card).name << '#'
                               << c.id << '('
                               << power_of(c) << '/'
                               << toughness_of(c) - c.damage
                               << (c.tapped ? ",T" : "")
                               << (c.summoning_sick ? ",S" : "") << ')';
                    }
                    detail << " | my life "
                           << obs.players[spz_seat].life
                           << " their hand "
                           << obs.players[1 - spz_seat].hand_size;
                    flag(obs.turn_number, detail.str());
                }
            }
        }

        if (ev.kind == GameEventKind::CombatResolved) {
            // Compare with pre-combat trail snapshot (blockers declared).
            const Snapshot* declared = nullptr;
            for (auto it = trail.rbegin(); it != trail.rend(); ++it) {
                if (it->event.kind == GameEventKind::BlockersDeclared) {
                    declared = &*it;
                    break;
                }
                if (it->event.kind == GameEventKind::AttackersDeclared) {
                    declared = &*it;
                    break;
                }
            }
            if (!declared) return;
            const auto& pre = declared->observation;
            const auto& pre_mine = pre.players[spz_seat].creatures;
            const auto& pre_theirs = pre.players[1 - spz_seat].creatures;
            const bool spz_attacking = ev.player == spz_seat ||
                (declared->event.kind == GameEventKind::AttackersDeclared
                     ? declared->event.player == spz_seat
                     : declared->event.player != spz_seat);
            // Losing attacks: an SPZ attacker died to its blocker while
            // the blocker survived.
            for (const auto& [attacker_id, blocker_id] :
                 declared->event.blocks) {
                const bool attacker_is_mine =
                    by_id(pre_mine, attacker_id) != nullptr;
                if (attacker_is_mine && spz_attacking) {
                    const bool attacker_died =
                        by_id(mine, attacker_id) == nullptr;
                    const bool blocker_survived =
                        by_id(theirs, blocker_id) != nullptr;
                    if (attacker_died && blocker_survived) {
                        flag(obs.turn_number,
                             "LOSING ATTACK: " +
                                 cname(pre_mine, attacker_id) +
                                 " died to surviving blocker " +
                                 cname(pre_theirs, blocker_id));
                    }
                }
                const bool blocker_is_mine =
                    by_id(pre_mine, blocker_id) != nullptr;
                if (blocker_is_mine && !spz_attacking) {
                    const bool blocker_died =
                        by_id(mine, blocker_id) == nullptr;
                    const bool attacker_survived =
                        by_id(theirs, attacker_id) != nullptr;
                    const auto* attacker =
                        by_id(pre_theirs, attacker_id);
                    const int my_pre_life =
                        pre.players[spz_seat].life;
                    if (blocker_died && attacker_survived && attacker &&
                        my_pre_life - power_of(*attacker) > 8) {
                        std::ostringstream detail;
                        detail << "NEEDLESS CHUMP: "
                               << cname(pre_mine, blocker_id)
                               << " chumped "
                               << cname(pre_theirs, attacker_id)
                               << " at life " << my_pre_life
                               << " | attackers that combat:";
                        for (const auto& [a2, b2] :
                             declared->event.blocks) {
                            detail << ' ' << cname(pre_theirs, a2)
                                   << "<-" << cname(pre_mine, b2);
                        }
                        detail << " | unblocked incoming:";
                        for (const PermanentId aid :
                             declared->event.attackers) {
                            bool blocked = false;
                            for (const auto& [a2, b2] :
                                 declared->event.blocks) {
                                blocked |= a2 == aid;
                            }
                            if (!blocked) {
                                detail << ' '
                                       << cname(pre_theirs, aid);
                            }
                        }
                        flag(obs.turn_number, detail.str());
                    }
                }
            }
        }

        if (ev.kind == GameEventKind::PriorityActionSelected &&
            ev.player == spz_seat && ev.priority_action.has_value() &&
            ev.priority_action->kind != PriorityActionKind::Pass &&
            ev.priority_action->kind != PriorityActionKind::PlayLand) {
            cast_a_spell_this_turn = true;
        }
        if (ev.kind == GameEventKind::TurnStarted) {
            // Look back: did SPZ end its just-finished turn without a free
            // land drop?
            if (!trail.empty()) {
                const Snapshot& last = trail.back();
                const auto& prev_obs = last.observation;
                if (prev_obs.active_player == spz_seat &&
                    prev_obs.turn_number >= 1 &&
                    !cast_a_spell_this_turn) {
                    // Failure to develop: turn ended with a castable
                    // creature and nothing cast. Holding is excused when
                    // an instant in hand needed the open mana.
                    PlayerState projected;
                    const auto& me = prev_obs.players[spz_seat];
                    projected.life = me.life;
                    projected.channel_active = me.channel_active;
                    projected.lands = me.lands;
                    projected.artifacts = me.artifacts;
                    projected.mana_pool = me.mana_pool;
                    int instant_reserve = 0;
                    for (const CardId card : prev_obs.hand) {
                        const auto& definition = card_definition(card);
                        if (definition.type == CardType::Instant) {
                            const auto& cost = definition.cost;
                            const int total = cost.generic +
                                              cost.green + cost.red +
                                              cost.blue + cost.white;
                            instant_reserve =
                                instant_reserve == 0
                                    ? total
                                    : std::min(instant_reserve, total);
                        }
                    }
                    // Under an enemy Moat, holding ground creatures is
                    // discipline, not passivity: they cannot attack.
                    const auto& their_enchantments =
                        prev_obs.players[1 - spz_seat].enchantments;
                    const bool moat_locked =
                        std::find(their_enchantments.begin(),
                                  their_enchantments.end(),
                                  CardId::Moat) !=
                        their_enchantments.end();
                    for (const CardId card : prev_obs.hand) {
                        const auto& definition = card_definition(card);
                        if (definition.type != CardType::Creature ||
                            !can_pay(projected, definition.cost)) {
                            continue;
                        }
                        if (moat_locked && !definition.flying) {
                            continue;
                        }
                        const auto& cost = definition.cost;
                        const int total = cost.generic + cost.green +
                                          cost.red + cost.blue +
                                          cost.white;
                        if (maximum_available_mana(projected) <
                            total + instant_reserve) {
                            continue;
                        }
                        flag(prev_obs.turn_number,
                             std::string("SKIPPED DEVELOPMENT: ended "
                                         "turn with castable ") +
                                 std::string(definition.name) +
                                 " and cast nothing");
                        break;
                    }
                }
                if (prev_obs.active_player == spz_seat &&
                    prev_obs.turn_number >= 1 &&
                    !prev_obs.players[spz_seat].land_played_this_turn &&
                    prev_obs.players[spz_seat].lands.size() < 6) {
                    bool holds_land = false;
                    for (const CardId card : prev_obs.hand) {
                        if (card_definition(card).type == CardType::Land)
                            holds_land = true;
                    }
                    if (holds_land) {
                        flag(prev_obs.turn_number,
                             "SKIPPED LAND DROP with " +
                                 std::to_string(prev_obs.players[spz_seat]
                                                    .lands.size()) +
                                 " lands in play");
                    }
                }
            }
        }

        if (ev.kind == GameEventKind::CardsDiscarded &&
            ev.player == spz_seat) {
            std::size_t lands_in_hand = 0;
            for (const CardId card : obs.hand)
                if (card_definition(card).type == CardType::Land)
                    lands_in_hand++;
            for (const CardId card : ev.cards) {
                if (card_definition(card).type != CardType::Land &&
                    lands_in_hand >= 3) {
                    flag(obs.turn_number,
                         std::string("DISCARDED SPELL (") +
                             std::string(card_definition(card).name) +
                             ") while holding " +
                             std::to_string(lands_in_hand) + " lands");
                }
            }
        }

        if (ev.kind == GameEventKind::PriorityActionSelected &&
            ev.player == spz_seat && ev.priority_action.has_value() &&
            ev.priority_action->kind ==
                PriorityActionKind::CastGiantGrowth &&
            ev.priority_action->target.has_value() &&
            ev.priority_action->target->creature.has_value() &&
            ev.priority_action->target->player != spz_seat) {
            flag(obs.turn_number,
                 "ENEMY GROWTH: pumped an opponent creature");
        }
        if (ev.kind == GameEventKind::PriorityActionSelected &&
            ev.player == spz_seat && ev.priority_action.has_value() &&
            ev.priority_action->kind ==
                PriorityActionKind::CastGiantGrowth &&
            (ev.phase == TurnPhase::FirstMain ||
             ev.phase == TurnPhase::SecondMain) &&
            ev.priority_action->target.has_value() &&
            ev.priority_action->target->creature.has_value() &&
            ev.priority_action->target->player == spz_seat) {
            pending_growth = {obs.turn_number,
                              *ev.priority_action->target->creature,
                              obs.active_player == spz_seat};
        }
        if (ev.kind == GameEventKind::AttackersDeclared &&
            pending_growth.has_value() &&
            obs.turn_number == std::get<0>(*pending_growth)) {
            const PermanentId pumped = std::get<1>(*pending_growth);
            const bool attacked =
                std::find(ev.attackers.begin(), ev.attackers.end(),
                          pumped) != ev.attackers.end();
            const bool my_combat = ev.player == spz_seat;
            if (my_combat && !attacked) {
                flag(obs.turn_number,
                     "WASTED GROWTH: pumped own creature in main phase, "
                     "then did not attack with it");
            }
            pending_growth.reset();
        }
        if (ev.kind == GameEventKind::BlockersDeclared &&
            pending_growth.has_value() &&
            !std::get<2>(*pending_growth)) {
            const PermanentId pumped = std::get<1>(*pending_growth);
            bool blocked_with_it = false;
            for (const auto& [attacker, blocker] : ev.blocks) {
                blocked_with_it |= blocker == pumped;
            }
            if (!blocked_with_it) {
                flag(obs.turn_number,
                     "WASTED GROWTH: pumped own creature during "
                     "opponent's main, then did not block with it");
            }
            pending_growth.reset();
        }
        if (ev.kind == GameEventKind::PriorityActionSelected &&
            ev.player == spz_seat &&
            obs.active_player == spz_seat &&
            ev.priority_action.has_value()) {
            const auto kind = ev.priority_action->kind;
            if (kind == PriorityActionKind::PlayLand &&
                cast_a_spell_this_turn) {
                // Harmful only when the earlier cast taxed a mana
                // creature: a tapped, battle-ready creature during the
                // owner's own precombat main means it paid for a spell
                // the land could have covered.
                bool taxed_creature = false;
                for (const auto& creature :
                     obs.players[spz_seat].creatures) {
                    taxed_creature =
                        taxed_creature ||
                        (creature.tapped && !creature.summoning_sick);
                }
                if (taxed_creature) {
                    flag(obs.turn_number,
                         "LAND AFTER SPELL: land drop sequenced after "
                         "a cast that taxed a mana creature");
                }
            }
        }
        if (ev.kind == GameEventKind::TurnStarted) {
            pending_growth.reset();
            cast_a_spell_this_turn = false;
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    const bool mirror = argc > 2 && std::string(argv[2]) == "--mirror";
    const bool random_pilot =
        argc > 2 && std::string(argv[2]) == "--random-pilot";
    double gamma = 1.0;
    double tie_band = 0.02;
    bool web_mode = false;
    std::uint64_t seed_base = 424900;
    std::optional<std::size_t> spz_deck;
    std::optional<std::size_t> opp_deck;
    std::string advantage_path;
    bool guardrails = true;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--gamma" && i + 1 < argc) {
            gamma = std::stod(argv[i + 1]);
        }
        if (std::string(argv[i]) == "--advantage" && i + 1 < argc) {
            advantage_path = argv[i + 1];
        }
        if (std::string(argv[i]) == "--no-guardrails") {
            guardrails = false;
        }
        if (std::string(argv[i]) == "--web") {
            web_mode = true;
        }
        if (std::string(argv[i]) == "--seed-base" && i + 1 < argc) {
            seed_base = std::stoull(argv[i + 1]);
        }
        if (std::string(argv[i]) == "--tie-band" && i + 1 < argc) {
            tie_band = std::stod(argv[i + 1]);
        }
        if (std::string(argv[i]) == "--spz-deck" && i + 1 < argc) {
            spz_deck = std::stoul(argv[i + 1]);
        }
        if (std::string(argv[i]) == "--opp-deck" && i + 1 < argc) {
            opp_deck = std::stoul(argv[i + 1]);
        }
    }
    const auto advantage =
        advantage_path.empty()
            ? std::shared_ptr<const SpzAdvantageNet>{}
            : std::make_shared<const SpzAdvantageNet>(
                  load_spz_advantage_net(advantage_path));
    if (argc > 1 && std::string(argv[1]) == "--self-test") {
        // Fabricated events must trip every detector.
        std::size_t passed = 0;
        {   // MISSED LETHAL
            Auditor a; a.spz_seat = 0; a.seed = 1;
            PlayerObservation obs;
            obs.observer = 0; obs.turn_number = 9;
            obs.players[0].creatures.push_back(
                {.id = 1, .card = CardId::HillGiant,
                 .summoning_sick = false});
            obs.players[1].life = 3;
            GameEvent ev{.kind = GameEventKind::AttackersDeclared,
                         .player = 0,
                         .phase = TurnPhase::DeclareAttackers};
            a.on_event(obs, ev);
            passed += !a.flags.empty() &&
                      a.flags[0].find("MISSED LETHAL") !=
                          std::string::npos;
        }
        {   // DISCARDED SPELL
            Auditor a; a.spz_seat = 0; a.seed = 2;
            PlayerObservation obs;
            obs.observer = 0; obs.turn_number = 5;
            obs.hand = {CardId::Forest, CardId::Forest, CardId::Forest,
                        CardId::GrizzlyBears};
            GameEvent ev{.kind = GameEventKind::CardsDiscarded,
                         .player = 0,
                         .phase = TurnPhase::SecondMain,
                         .cards = {CardId::GiantGrowth}};
            a.on_event(obs, ev);
            passed += !a.flags.empty() &&
                      a.flags[0].find("DISCARDED SPELL") !=
                          std::string::npos;
        }
        std::cout << "self-test: " << passed << "/2 detectors fired\n";
        return passed == 2 ? 0 : 1;
    }
    const std::size_t games = argc > 1 ? std::stoul(argv[1]) : 16;
    const auto net = std::make_shared<const SpzNet>(
        load_spz_net("data/spz-champion-v10.txt"));
    const auto& decks = spz_decks();
    std::size_t total_flags = 0, wins = 0;
    for (std::size_t g = 0; g < games; ++g) {
        const std::size_t spz_seat = web_mode ? 1 : g % 2;
        std::size_t d0 = (g * 7 + 1) % kSpzDeckCount;
        std::size_t d1 = (g * 3 + 2) % kSpzDeckCount;
        if (spz_deck.has_value()) {
            (spz_seat == 0 ? d0 : d1) = *spz_deck;
        }
        if (opp_deck.has_value()) {
            (spz_seat == 0 ? d1 : d0) = *opp_deck;
        }
        const std::array<std::vector<CardId>, 2> game_decks = {
            decks[d0], decks[d1]};
        auto auditor = std::make_shared<Auditor>();
        auditor->spz_seat = spz_seat;
        auditor->seed = seed_base + g;
        auto mirror_auditor = std::make_shared<Auditor>();
        mirror_auditor->spz_seat = 1 - spz_seat;
        mirror_auditor->seed = auditor->seed;
        GameConfig config;
        config.max_turns = 150;
        const auto attach = [&](std::size_t seat,
                                std::shared_ptr<Auditor> seat_auditor) {
            SpzPolicyConfig policy;
            policy.worlds = random_pilot ? 1 : 4;
            policy.block_prediction_worlds = random_pilot ? 1 : 4;
            policy.rollout = !random_pilot;
            policy.epsilon = random_pilot ? 1.0 : 0.0;
            policy.pass_dominance_prune = !random_pilot && guardrails;
            policy.gamma_per_turn = gamma;
            policy.advantage_tie_band = tie_band;
            policy.seed = web_mode
                              ? (seat_auditor->seed ^ 0x53505AULL)
                              : (seat_auditor->seed ^ 0xA0D17) + seat;
            auto controller = make_spz_controller(
                net, game_decks, seat, policy, nullptr, nullptr,
                advantage);
            controller.observe =
                [seat_auditor](const PlayerObservation& obs,
                               const GameEvent& ev) {
                    seat_auditor->on_event(obs, ev);
                };
            config.human_controllers[seat] = controller;
        };
        attach(spz_seat, auditor);
        if (mirror) {
            attach(1 - spz_seat, mirror_auditor);
        } else {
            config.bots[1 - spz_seat].kind = BotKind::Handcrafted;
        }
        Game game(game_decks[0], game_decks[1], auditor->seed, config);
        const GameResult result = game.run();
        if (result.winner == static_cast<int>(spz_seat)) wins++;
        if (mirror) {
            for (const auto& f : mirror_auditor->flags) {
                auditor->flags.push_back(f);
            }
        }
        std::cout << "game " << g << " (seed " << auditor->seed
                  << ", SPZ " << spz_deck_name(d0 == d1 ? d0 : (spz_seat == 0 ? d0 : d1))
                  << " vs HC " << spz_deck_name(spz_seat == 0 ? d1 : d0)
                  << "): " << (result.winner == static_cast<int>(spz_seat)
                                   ? "WIN"
                                   : result.winner == -1 ? "DRAW" : "LOSS")
                  << ", " << auditor->flags.size() << " flags\n";
        for (const auto& f : auditor->flags) {
            std::cout << "  " << f << "\n";
            total_flags++;
        }
    }
    std::cout << "\nTOTAL: " << games << " games, " << wins << " wins, "
              << total_flags << " flags\n";
    return 0;
}
