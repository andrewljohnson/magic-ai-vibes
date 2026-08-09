"""Scout the deliverable net: play instrumented games vs `handcrafted`
and log everything needed for a human mistake review.

Plays `penta_net.npz` with the SAME rollout-search config the gate uses
(DEFAULT_SEARCH: topk 4 / 1 playout / budget 120 / playout-max-eval 8,
matching the net metadata's `search_topk`) across every ordered pairing
of the four training decks -- our seat pilots each deck against each
deck flown by the built-in handcrafted bot (16 pairings x N games).

Per game it records a full transcript of OUR seat's decisions (turn,
step, action type, card / attacker / target names from the catalog and
battlefield), a per-turn life trajectory, the result + engine reason,
and per-game mistake counters in the os-digest style:

  - attack-opportunity take-rate (phases where DeclareAttacker was legal)
  - block-opportunity take-rate + life lost in unblocked combats
  - no-damage attacks that lost a creature (suicide-attack proxy)
  - turns ended with castable spells in hand (unspent-mana proxy)
  - turns that skipped an available land drop
  - casts into >=2 untapped opposing lands vs the counter decks
    (cast-into-open-counter-mana proxy)
  - counterspell casts + targets, discards, X-spell sizing

Usage:
    python3 scout.py --games-per-pair 4 --workers 2 \
        --out /path/to/penta-scout-games.json

Read-only with respect to the training stack: imports trainer/net/
extractor but never writes their files.
"""

import argparse
import json
import os
import random
import time
from collections import defaultdict
from multiprocessing import Pool

from extractor import Extractor, import_penta
from net import Net
from trainer import DEFAULT_SEARCH, DECKS, choose, make_fork

COUNTER_DECKS = ("The Deck", "Counterburn")
MAIN_STEPS = ("PrecombatMain", "PostcombatMain")
# Card kinds whose casts can be "into open counter mana".
SEED_BASE = 9_100_000


def describe_action(action, obs, names_by_instance):
    """Human-readable one-liner for a chosen action."""
    t = action["type"]
    if t == "PlayLand":
        return f"PlayLand {names_by_instance.get(action.get('card'), '?')}"
    if t == "CastSpell":
        name = names_by_instance.get(action.get("card"), "?")
        bits = [f"Cast {name}"]
        if action.get("x"):
            bits.append(f"x={action['x']}")
        targets = describe_targets(action, obs, names_by_instance)
        if targets:
            bits.append("-> " + ", ".join(targets))
        return " ".join(bits)
    if t == "DeclareAttacker":
        return f"Attack {names_by_instance.get(action.get('attacker'), '?')}"
    if t == "DeclareBlocker":
        return (f"Block {names_by_instance.get(action.get('attacker'), '?')}"
                f" with {names_by_instance.get(action.get('blocker'), '?')}")
    if t == "DiscardCards":
        cards = [names_by_instance.get(c, "?")
                 for c in action.get("cards", ())]
        return "Discard " + ", ".join(cards)
    if t == "ActivateAbility":
        src = names_by_instance.get(action.get("source"), "?")
        target = action.get("target") or {}
        tgt = names_by_instance.get(target.get("instance"))
        return f"Activate {src}" + (f" -> {tgt}" if tgt else "")
    return t


def describe_targets(action, obs, names_by_instance):
    """Names for a CastSpell's targets, from the enumerated choices."""
    out = []
    selections = (action.get("choices") or {}).get("targetSelections", ())
    stack_names = {entry.get("instance"): entry.get("name")
                   for entry in obs.get("stack", ())}
    for selection in selections:
        for target in selection.get("targets", ()):
            if target.get("type") == "player":
                out.append(f"player {target.get('seat')}")
            else:
                inst = target.get("instance")
                out.append(names_by_instance.get(inst)
                           or stack_names.get(inst) or f"instance {inst}")
    return out


def instance_names(obs):
    """instance -> card name over every zone this observation exposes."""
    names = {}
    for card in obs.get("hand", ()):
        names[card["instance"]] = card["name"]
    for perm in obs.get("battlefield", ()):
        names[perm["instance"]] = perm["name"]
    for entry in obs.get("stack", ()):
        if "instance" in entry:
            names[entry["instance"]] = entry.get("name")
    for zone in ("graveyards", "exiles"):
        for pile in obs.get(zone, ()) or ():
            for card in pile or ():
                if isinstance(card, dict) and "instance" in card:
                    names[card["instance"]] = card.get("name")
    return names


def creature_instances(obs, seat):
    return {perm["instance"] for perm in obs.get("battlefield", ())
            if perm["controller"] == seat and perm.get("power") is not None}


def untapped_lands(obs, seat):
    return sum(1 for perm in obs.get("battlefield", ())
               if perm["controller"] == seat and perm.get("power") is None
               and not perm.get("tapped"))


def play_scout_game(net, extractor, penta, our_deck, their_deck, my_seat,
                    seed, max_eval, search):
    """One instrumented game. Returns a JSON-able record."""
    opponent_seat = "p2" if my_seat == "p1" else "p1"
    d1, d2 = ((our_deck, their_deck) if my_seat == "p1"
              else (their_deck, our_deck))

    def make_game():
        return penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=opponent_seat, seed=seed)

    game = make_game()
    rng = random.Random(seed)
    history = []
    transcript = []
    life_track = []          # (turn, our_life, opp_life) at each new turn
    me = 0 if my_seat == "p1" else 1
    opp = 1 - me

    counters = defaultdict(int)
    x_spells = []            # (turn, name, chosen_x, max_x_available)
    counter_casts = []       # (turn, name, target description)
    discards = []            # (turn, [names])
    bad_attacks = []         # (turn,) no-damage attacks losing a creature
    unblocked_hits = []      # (turn, life_lost) after declining all blocks

    seen_turn = 0
    # Per-turn state for main-phase / land-drop / attack accounting.
    turn_state = None
    pending_combat = None    # snapshot at our FinishDeclaringAttackers
    pending_block = None     # snapshot when we declined blocks

    def flush_turn():
        if turn_state is None:
            return
        if turn_state["active"]:
            if turn_state["last_main_pass_castable"]:
                counters["turns_unspent_castables"] += 1
            if turn_state["land_seen"] and not turn_state["land_played"]:
                counters["turns_land_skipped"] += 1
            counters["our_turns"] += 1
        if turn_state["attack_legal"]:
            counters["attack_phases"] += 1
            if turn_state["attacked"]:
                counters["attack_phases_taken"] += 1
        if turn_state["block_legal"]:
            counters["block_phases"] += 1
            if turn_state["blocked"]:
                counters["block_phases_taken"] += 1

    fault = None
    while game.result() is None and len(history) < 3000:
        obs = json.loads(game.observe())
        actions = obs["legalActions"]
        turn = obs.get("turn", 0)
        step = obs.get("step")
        active = obs.get("activeSeat") == my_seat
        life = obs["life"]

        if turn != seen_turn and not obs.get("pregame"):
            flush_turn()
            turn_state = {
                "active": active, "land_seen": False, "land_played": False,
                "last_main_pass_castable": False,
                "attack_legal": False, "attacked": False,
                "block_legal": False, "blocked": False,
            }
            seen_turn = turn
            life_track.append((turn, life[me], life[opp]))

        # Resolve combat snapshots the first time we act after combat.
        if pending_combat is not None and step not in (
                "DeclareAttackers", "DeclareBlockers", "CombatDamage"):
            opp_life_before, our_creatures_before = pending_combat
            lost = our_creatures_before - creature_instances(obs, my_seat)
            if life[opp] >= opp_life_before and lost:
                bad_attacks.append(turn)
                counters["no_damage_attacks_losing_creature"] += 1
            pending_combat = None
        if pending_block is not None and (step not in (
                "DeclareBlockers", "CombatDamage") or active):
            my_life_before = pending_block
            lost_life = my_life_before - life[me]
            if lost_life > 0:
                unblocked_hits.append((turn, lost_life))
                counters["life_lost_unblocked"] += lost_life
            pending_block = None

        types = {a["type"] for a in actions}
        if turn_state is not None:
            if active and step in MAIN_STEPS:
                if "PlayLand" in types:
                    turn_state["land_seen"] = True
                turn_state["last_main_pass_castable"] = False
            if "DeclareAttacker" in types:
                turn_state["attack_legal"] = True
            if "DeclareBlocker" in types:
                turn_state["block_legal"] = True

        try:
            index, _, value = choose(
                make_fork(make_game, history, game), obs, net, extractor,
                rng, epsilon=0.0, max_eval=max_eval, depth=len(history),
                search=search)
        except ValueError:
            fault = "engine-fault"
            break
        chosen = next(a for a in actions if a["index"] == index)
        ctype = chosen["type"]
        names = instance_names(obs)

        if turn_state is not None:
            if ctype == "PlayLand":
                turn_state["land_played"] = True
            if ctype == "DeclareAttacker":
                turn_state["attacked"] = True
                counters["attackers_declared"] += 1
            if ctype == "DeclareBlocker":
                turn_state["blocked"] = True
                counters["blocks_declared"] += 1
            if (active and step in MAIN_STEPS and ctype == "PassPriority"
                    and "CastSpell" in types):
                turn_state["last_main_pass_castable"] = True

        if ctype == "CastSpell":
            counters["spells_cast"] += 1
            if (their_deck in COUNTER_DECKS
                    and untapped_lands(obs, opponent_seat) >= 2):
                counters["casts_into_open_counter_mana"] += 1
            name = names.get(chosen.get("card"), "?")
            if "Counterspell" in name or name in ("Power Sink", "Mana Drain",
                                                  "Spell Blast"):
                counter_casts.append(
                    (turn, name,
                     ", ".join(describe_targets(chosen, obs, names)) or "?"))
                counters["counterspells_cast"] += 1
            if chosen.get("x"):
                same_card = [a.get("x", 0) for a in actions
                             if a["type"] == "CastSpell"
                             and a.get("card") == chosen.get("card")]
                x_spells.append((turn, name, chosen["x"], max(same_card)))
        elif ctype == "DiscardCards":
            discards.append((turn, [names.get(c, "?")
                                    for c in chosen.get("cards", ())]))
            counters["cards_discarded"] += len(chosen.get("cards", ()))
        elif ctype == "FinishDeclaringAttackers" and turn_state and \
                turn_state["attacked"]:
            pending_combat = (life[opp], creature_instances(obs, my_seat))
        elif ctype == "FinishDeclaringBlockers" and turn_state and \
                turn_state["block_legal"] and not turn_state["blocked"]:
            pending_block = life[me]

        if len(actions) > 1 or ctype not in ("PassPriority",):
            transcript.append({
                "turn": turn, "step": step, "n_legal": len(actions),
                "life": [life[me], life[opp]],
                "action": describe_action(chosen, obs, names),
                "value": None if value is None else round(float(value), 3),
            })
        game.act(index)
        history.append(index)

    flush_turn()
    result = game.result()
    score = 0.5
    reason = fault
    if result is not None:
        score = (1.0 if result == my_seat
                 else 0.5 if result == "draw" else 0.0)
        try:
            final = json.loads(game.observe(my_seat))
            reason = (final.get("result") or {}).get("reason")
        except Exception:
            reason = None
    final_hand = []
    try:
        final = json.loads(game.observe(my_seat))
        final_hand = [c["name"] for c in final.get("hand", ())]
    except Exception:
        pass

    return {
        "our_deck": our_deck, "their_deck": their_deck, "seat": my_seat,
        "seed": seed, "score": score, "result": result, "reason": reason,
        "turns": seen_turn, "decisions": len(history),
        "final_life": life_track[-1][1:] if life_track else None,
        "final_hand": final_hand,
        "life_track": life_track,
        "counters": dict(counters),
        "x_spells": x_spells, "counter_casts": counter_casts,
        "discards": discards, "bad_attacks": bad_attacks,
        "unblocked_hits": unblocked_hits,
        "transcript": transcript,
    }


_WORKER = {}


def _init(net_path):
    _WORKER["penta"] = import_penta()
    _WORKER["extractor"] = Extractor()
    _WORKER["net"] = Net.load(net_path)


def _play(task):
    our_deck, their_deck, my_seat, seed, max_eval, search = task
    return play_scout_game(_WORKER["net"], _WORKER["extractor"],
                           _WORKER["penta"], our_deck, their_deck, my_seat,
                           seed, max_eval, search)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--net", default="penta_net.npz")
    parser.add_argument("--games-per-pair", type=int, default=4)
    parser.add_argument("--seed-base", type=int, default=SEED_BASE)
    parser.add_argument("--max-eval", type=int, default=16)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--out", default="penta-scout-games.json")
    args = parser.parse_args()

    search = DEFAULT_SEARCH  # matches gate defaults + net metadata
    net = Net.load(args.net)
    meta = getattr(net, "metadata", {})
    print(f"net {args.net}: meta "
          f"{ {k: v.item() if v.shape == () else v for k, v in meta.items()} }")
    print(f"search {search}; decks {DECKS}; "
          f"{args.games_per_pair} games x {len(DECKS)**2} ordered pairings")

    tasks = []
    for pi, our_deck in enumerate(DECKS):
        for pj, their_deck in enumerate(DECKS):
            for g in range(args.games_per_pair):
                my_seat = "p1" if g % 2 == 0 else "p2"
                seed = (args.seed_base
                        + (pi * len(DECKS) + pj) * 100 + g)
                tasks.append((our_deck, their_deck, my_seat, seed,
                              args.max_eval, search))

    t0 = time.time()
    with Pool(args.workers, initializer=_init, initargs=(args.net,)) as pool:
        games = pool.map(_play, tasks)
    took = time.time() - t0
    print(f"{len(games)} games in {took:.0f}s ({len(games)/took:.2f} games/s)")

    # Per-pairing metric aggregation (the os-digest style summary).
    pairs = defaultdict(list)
    for record in games:
        pairs[(record["our_deck"], record["their_deck"])].append(record)
    summary = []
    for (ours, theirs), recs in sorted(pairs.items()):
        n = len(recs)
        total = lambda key: sum(r["counters"].get(key, 0) for r in recs)
        att_p, att_t = total("attack_phases"), total("attack_phases_taken")
        blk_p, blk_t = total("block_phases"), total("block_phases_taken")
        row = {
            "our_deck": ours, "their_deck": theirs, "games": n,
            "wins": sum(1 for r in recs if r["score"] == 1.0),
            "avg_turns": round(sum(r["turns"] for r in recs) / n, 1),
            "attack_take_rate": round(att_t / att_p, 2) if att_p else None,
            "block_take_rate": round(blk_t / blk_p, 2) if blk_p else None,
            "attackers_per_game": round(total("attackers_declared") / n, 1),
            "unspent_turns_per_game":
                round(total("turns_unspent_castables") / n, 1),
            "land_skips_per_game": round(total("turns_land_skipped") / n, 1),
            "suicide_attacks_per_game":
                round(total("no_damage_attacks_losing_creature") / n, 1),
            "life_lost_unblocked_per_game":
                round(total("life_lost_unblocked") / n, 1),
            "casts_into_counter_mana_per_game":
                round(total("casts_into_open_counter_mana") / n, 1),
            "counterspells_per_game":
                round(total("counterspells_cast") / n, 1),
            "spells_per_game": round(total("spells_cast") / n, 1),
        }
        summary.append(row)
        print(f"{ours:13s} vs {theirs:13s}  {row['wins']}/{n} wins  "
              f"atk {row['attack_take_rate']}  blk {row['block_take_rate']}  "
              f"unspent {row['unspent_turns_per_game']}/g  "
              f"openmana {row['casts_into_counter_mana_per_game']}/g")

    with open(args.out, "w") as f:
        json.dump({"meta": {
            "net": args.net, "seed_base": args.seed_base,
            "games_per_pair": args.games_per_pair,
            "search": search._asdict(), "max_eval": args.max_eval,
            "engine_version": str(meta.get("engine_version", "")),
        }, "summary": summary, "games": games}, f)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
