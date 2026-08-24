#!/usr/bin/env python3
"""Play games with the trained bot and write a READABLE move-by-move log,
flagging behaviour that looks wrong.

    PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python playout_log.py \
        --net deploy_v1 --games 12 --iters 128

WHY: the gate reports one number per 300 games. A number cannot tell you
that the bot is pointing its own burn at its own face, skipping land
drops, or holding a full grip while it passes the turn. Those are visible
in one game and invisible in the aggregate, so the log renders each of OUR
decisions in plain text and a set of detectors marks the suspicious ones.

The detectors are deliberately CONSERVATIVE about calling something a bug.
Several plays that look wrong are correct MTG -- burning your own creature
in response to removal, declining to attack into a bigger board, holding a
land for information. Each flag says what it saw; judgement stays human.
Counts are what matter: one skipped land drop is noise, one every game is
a defect.
"""
import argparse
import json
import os
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from extractor import import_penta, pinned_catalog          # noqa: E402
from hosted_policy import load_decklists, AzSearchPolicy    # noqa: E402


def card_index():
    """definition id -> (name, rules text, kind). Damage spells are found
    from rules text rather than a hardcoded list, so the detector keeps
    working as the catalog grows."""
    out = {}
    for c in pinned_catalog()["cards"]:
        parts = c.get("parts") or []
        text = " ".join((p.get("rulesText") or "") for p in parts)
        out[c["definition"]] = (c.get("name", "?"), text, c.get("kind", ""))
    return out


CARDS = None


def deals_damage(defn):
    _, text, _ = CARDS.get(defn, ("?", "", ""))
    t = text.lower()
    return "damage" in t and ("deals" in t or "deal " in t)


_X_SPELLS = None


def is_x_spell(defn):
    global _X_SPELLS
    if _X_SPELLS is None:
        _X_SPELLS = {c["definition"] for c in pinned_catalog()["cards"]
                     if (c.get("manaCost") or {}).get("variableX")}
    return defn in _X_SPELLS


def sorcery_speed(defn):
    """Creature/Sorcery/Enchantment/Artifact -- cards there is no reason to
    hold past your own main phase. Instants are excluded: holding one up is
    ordinary play, not a defect."""
    kind = CARDS.get(defn, ("", "", ""))[2]
    return kind in ("Sorcery", "Creature", "Enchantment", "Artifact")


def name_of(defn):
    return CARDS.get(defn, ("?", "", ""))[0]


def obj_owner(obs, oid):
    """Which seat controls object `oid` on the battlefield (or None)."""
    for p in obs.get("battlefield") or ():
        if p.get("objectId") == oid:
            return p.get("controller")
    return None


def render_action(obs, a):
    t = a.get("type")
    if t == "PlayLand":
        return f"PlayLand {name_of(card_def(obs, a.get('card')))}"
    if t == "CastSpell":
        nm = name_of(card_def(obs, a.get("card")))
        tg = describe_targets(obs, a.get("targets") or [])
        x = a.get("choices", {}).get("x", 0)
        return f"Cast {nm}" + (f" X={x}" if x else "") + (f" -> {tg}" if tg else "")
    if t == "DeclareAttacker":
        atk = a.get("attacker")
        d = a.get("defender") or {}
        return (f"Attack with {name_of(obj_def(obs, atk))} "
                f"-> {d.get('seat', d.get('type', '?'))}")
    if t == "DeclareBlocker":
        return (f"Block {name_of(obj_def(obs, a.get('attacker')))} with "
                f"{name_of(obj_def(obs, a.get('blocker')))}")
    if t == "ActivateAbility":
        src = name_of(obj_def(obs, a.get("source")))
        tg = describe_targets(obs, a.get("targets") or [])
        return f"Activate {src}" + (f" -> {tg}" if tg else "")
    if t == "ActivateManaAbility":
        return f"Tap {name_of(obj_def(obs, a.get('source')))} for mana"
    if t == "DiscardCards":
        return "Discard " + ", ".join(
            name_of(card_def(obs, c)) for c in (a.get("cards") or []))
    return t


def card_def(obs, oid):
    for c in obs.get("hand") or ():
        if c.get("objectId") == oid:
            return c.get("definition")
    return obj_def(obs, oid)


def obj_def(obs, oid):
    for p in obs.get("battlefield") or ():
        if p.get("objectId") == oid:
            return p.get("definition")
    for s in obs.get("stack") or ():
        if s.get("objectId") == oid:
            return s.get("definition")
    return None


def describe_targets(obs, targets):
    out = []
    for t in targets:
        if not isinstance(t, dict):
            out.append(str(t)); continue
        if t.get("type") == "player" or "seat" in t:
            seat = t.get("seat")
            out.append(f"player {seat}"
                       + (" (US)" if seat == obs.get("seat") else ""))
        else:
            oid = t.get("objectId") or t.get("object") or t.get("id")
            owner = obj_owner(obs, oid)
            out.append(name_of(obj_def(obs, oid))
                       + (" (OURS)" if owner == obs.get("seat") else ""))
    return ", ".join(out)


def targets_self(obs, a):
    """Does this action point at our own face or our own permanent?"""
    me = obs.get("seat")
    for t in (a.get("targets") or []):
        if not isinstance(t, dict):
            continue
        if (t.get("type") == "player" or "seat" in t) and t.get("seat") == me:
            return "own face"
        oid = t.get("objectId") or t.get("object") or t.get("id")
        if oid is not None and obj_owner(obs, oid) == me:
            return "own permanent"
    return None


def our_untapped_lands(obs):
    me = obs.get("seat")
    return sum(1 for p in (obs.get("battlefield") or ())
               if p.get("controller") == me and not p.get("tapped")
               and CARDS.get(p.get("definition"), ("", "", ""))[2] == "Land")


def analyse(net, games, iters, deck, out_dir, verbose_games):
    global CARDS
    CARDS = card_index()
    penta = import_penta()
    decks = list(load_decklists())
    opps = [d for d in decks if d != deck]
    pol = AzSearchPolicy(f"{net}_value.spzw", f"{net}_policy.azp",
                         our_deck=deck, iters=iters)
    flags = Counter()
    examples = defaultdict(list)
    per_game = []
    os.makedirs(out_dir, exist_ok=True)

    for g in range(games):
        my_p1 = g % 2 == 0
        opp = opps[g % len(opps)]
        d1, d2 = (deck, opp) if my_p1 else (opp, deck)
        me = "p1" if my_p1 else "p2"
        game = penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=("p2" if my_p1 else "p1"),
                          seed=770000 + g)
        lines = [f"=== game {g}: {deck} (us, {me}) vs {opp} ==="]
        n = 0
        # per-turn bookkeeping for the land-drop and attack detectors
        turn_saw_land_action = {}
        turn_played_land = {}
        turn_saw_attack = {}
        turn_attacked = {}
        passed_main_turns = set()
        turn_land_offered_pre = set()
        while game.result() is None and n < 600:
            raw = game.observe()
            obs = json.loads(raw)
            acts = obs.get("legalActions") or []
            if not acts:
                break
            turn = obs.get("turn")
            active = obs.get("activeSeat")
            idx = pol.choose(obs, raw_json=raw)
            chosen = next((a for a in acts if a.get("index") == idx), acts[0])
            ctype = chosen.get("type")

            if len(acts) > 1:
                life = obs.get("life")
                lines.append(
                    f"  T{turn} {obs.get('step')} "
                    f"[{'our turn' if active == me else 'their turn'}] "
                    f"life {life} | {len(acts)} options -> "
                    f"{render_action(obs, chosen)}")

            # --- detectors ---
            where = f"game {g} T{turn} {obs.get('step')}"
            if ctype == "CastSpell":
                defn0 = card_def(obs, chosen.get("card"))
                xv = (chosen.get("choices") or {}).get("x", 0)
                # An X spell cast for X=0 does nothing at all. It is not a
                # judgement call like declining an attack -- the card is
                # spent for zero effect, so it is always a misplay.
                if defn0 is not None and xv == 0 and is_x_spell(defn0):
                    flags["x_spell_cast_for_zero"] += 1
                    if len(examples["x_spell_cast_for_zero"]) < 12:
                        examples["x_spell_cast_for_zero"].append(
                            f"{where}: {render_action(obs, chosen)} "
                            f"-- X=0 deals nothing, card wasted")
            if ctype in ("CastSpell", "ActivateAbility"):
                defn = card_def(obs, chosen.get("card")) if ctype == "CastSpell" \
                    else obj_def(obs, chosen.get("source"))
                self_t = targets_self(obs, chosen)
                if self_t and defn is not None and deals_damage(defn):
                    flags["damage_pointed_at_self"] += 1
                    examples["damage_pointed_at_self"].append(
                        f"{where}: {render_action(obs, chosen)} [{self_t}]")

            if active == me:
                if any(a.get("type") == "PlayLand" for a in acts):
                    turn_saw_land_action[turn] = True
                    if "PrecombatMain" in str(obs.get("step")):
                        turn_land_offered_pre.add(turn)
                if ctype == "PlayLand":
                    turn_played_land[turn] = True
                    # A land played AFTER combat produces mana this turn
                    # that can no longer be spent precombat or on combat
                    # tricks. When the same drop was available precombat,
                    # deferring it is a straight loss of options.
                    if ("PostcombatMain" in str(obs.get("step"))
                            and turn in turn_land_offered_pre):
                        flags["land_played_after_combat"] += 1
                        if len(examples["land_played_after_combat"]) < 12:
                            examples["land_played_after_combat"].append(
                                f"{where}: {render_action(obs, chosen)} in "
                                f"postcombat main, though it was legal "
                                f"precombat")
                if any(a.get("type") == "DeclareAttacker" for a in acts):
                    turn_saw_attack[turn] = True
                    if ctype == "DeclareAttacker":
                        turn_attacked[turn] = True
                # passing a main phase with a castable spell in hand
                # Holding an INSTANT is normal play, and one held card gets
                # a priority window in every step of the turn, so the naive
                # form of this counted the same decision six times. Count
                # sorcery-speed castables only, once per turn, in a main
                # phase with an empty stack.
                if (ctype == "PassPriority" and not (obs.get("stack") or [])
                        and "Main" in str(obs.get("step"))
                        and turn not in passed_main_turns):
                    slow = [a for a in acts if a.get("type") == "CastSpell"
                            and sorcery_speed(card_def(obs, a.get("card")))]
                    if slow:
                        passed_main_turns.add(turn)
                        flags["passed_main_holding_sorcery"] += 1
                        if len(examples["passed_main_holding_sorcery"]) < 12:
                            examples["passed_main_holding_sorcery"].append(
                                f"{where}: passed main holding "
                                f"{'; '.join(render_action(obs, a) for a in slow[:3])} "
                                f"({our_untapped_lands(obs)} untapped lands)")
            game.act(idx)
            n += 1

        for t, saw in turn_saw_land_action.items():
            if saw and not turn_played_land.get(t):
                flags["skipped_land_drop"] += 1
                if len(examples["skipped_land_drop"]) < 12:
                    examples["skipped_land_drop"].append(
                        f"game {g} T{t}: could play a land, did not")
        for t, saw in turn_saw_attack.items():
            if saw and not turn_attacked.get(t):
                flags["declined_all_attacks"] += 1
                if len(examples["declined_all_attacks"]) < 12:
                    examples["declined_all_attacks"].append(
                        f"game {g} T{t}: had attackers available, attacked with none")

        res = game.result()
        outcome = "WIN" if res == me else ("draw/unfinished"
                                           if res in (None, "draw") else "LOSS")
        lines.append(f"=== result: {outcome} after {n} decisions ===")
        per_game.append({"game": g, "opponent": opp, "outcome": outcome,
                         "decisions": n})
        if g < verbose_games:
            with open(os.path.join(out_dir, f"game-{g:03d}.txt"), "w") as f:
                f.write("\n".join(lines) + "\n")
        print(f"  game {g}: {outcome} vs {opp} ({n} decisions)", flush=True)

    return flags, examples, per_game


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--net", default="deploy_v1")
    ap.add_argument("--games", type=int, default=12)
    ap.add_argument("--iters", type=int, default=128)
    ap.add_argument("--deck", default="Sligh")
    ap.add_argument("--out", default="playouts")
    ap.add_argument("--verbose-games", type=int, default=6,
                    help="how many games to write full transcripts for")
    args = ap.parse_args()

    flags, examples, per_game = analyse(
        args.net, args.games, args.iters, args.deck,
        os.path.join(HERE, args.out), args.verbose_games)

    total_turns = sum(g["decisions"] for g in per_game)
    report = {
        "net": args.net, "iters": args.iters, "games": args.games,
        "deck": args.deck,
        "record": {
            "win": sum(1 for g in per_game if g["outcome"] == "WIN"),
            "loss": sum(1 for g in per_game if g["outcome"] == "LOSS"),
            "other": sum(1 for g in per_game
                         if g["outcome"] not in ("WIN", "LOSS")),
        },
        "decisions": total_turns,
        "flags": dict(flags),
        "per_game_rate": {k: round(v / max(args.games, 1), 2)
                          for k, v in flags.items()},
        "examples": {k: v[:12] for k, v in examples.items()},
    }
    path = os.path.join(HERE, "playout_report.json")
    with open(path, "w") as f:
        json.dump(report, f, indent=1)
    print("\n=== flags (count, per game) ===")
    for k, v in flags.most_common():
        print(f"  {k:32s} {v:5d}   {v/max(args.games,1):.2f}/game")
    if not flags:
        print("  none")
    print(f"\nwrote {path} and transcripts to {args.out}/")


if __name__ == "__main__":
    main()
