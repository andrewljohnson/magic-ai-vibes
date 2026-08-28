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


_CMC = None


def cmc(defn):
    """Total mana cost. Colour requirements are ignored -- this is used only
    to ask 'would one more land have made this castable', and in a deck
    that is essentially mono-red the approximation is close enough. It can
    still be wrong for off-colour or X spells."""
    global _CMC
    if _CMC is None:
        _CMC = {}
        for c in pinned_catalog()["cards"]:
            mc = c.get("manaCost") or {}
            _CMC[c["definition"]] = sum(
                v for k, v in mc.items()
                if isinstance(v, int) and k != "xMultiplier")
    return _CMC.get(defn)


def unlocked_by_one_more_land(obs):
    """Cards in hand that one extra land would make castable, and that we
    cannot cast right now. This is the ONLY reason a precombat land drop
    beats a postcombat one: without a use for the mana this turn, holding
    the land is better or neutral, because it tells the opponent less."""
    avail = our_untapped_lands(obs)
    out = []
    for c in (obs.get("hand") or ()):
        d = c.get("definition")
        k = CARDS.get(d, ("", "", ""))[2]
        if k == "Land":
            continue
        cost = cmc(d)
        if cost is not None and cost == avail + 1:
            out.append(name_of(d))
    return out


def adds_mana(defn):
    """A card that produces mana without costing any -- Moxen, Black Lotus,
    Sol Ring. Keeping a hand with no land AND none of these is a hand that
    cannot function."""
    name, text, kind = CARDS.get(defn, ("", "", ""))
    if kind == "Land":
        return True
    return "add " in text.lower() and (cmc(defn) or 0) == 0


def pumps_creature(defn):
    """Gives a creature +X/+X or an ability. Pointed at an OPPONENT'S
    creature this is simply helping them."""
    _, text, _ = CARDS.get(defn, ("", "", ""))
    t = text.lower()
    return "gets +" in t or "get +" in t


def creature_stats(perm):
    try:
        return int(perm.get("power") or 0), int(perm.get("toughness") or 0)
    except (TypeError, ValueError):
        return 0, 0


def their_untapped_creatures(obs):
    me = obs.get("seat")
    out = []
    for p in (obs.get("battlefield") or ()):
        if p.get("controller") == me or p.get("tapped"):
            continue
        if p.get("power") is None:
            continue
        out.append(p)
    return out


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
        turn_land_would_unlock = {}
        turn_cast_precombat = {}
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
            # Buffing an opponent's creature.
            if ctype in ("CastSpell", "ActivateAbility"):
                bdef = (card_def(obs, chosen.get("card"))
                        if ctype == "CastSpell"
                        else obj_def(obs, chosen.get("source")))
                me_seat = obs.get("seat")
                if bdef is not None and pumps_creature(bdef):
                    for t in (chosen.get("targets") or []):
                        if not isinstance(t, dict):
                            continue
                        oid = t.get("objectId") or t.get("object") or t.get("id")
                        if oid is not None and obj_owner(obs, oid) not in (None, me_seat):
                            flags["buffed_opponent_creature"] += 1
                            if len(examples["buffed_opponent_creature"]) < 12:
                                examples["buffed_opponent_creature"].append(
                                    f"{where}: {render_action(obs, chosen)} "
                                    f"-- that creature is THEIRS")
                            break

            # Keeping an opening hand that cannot make mana at all.
            if ctype == "KeepHand":
                hand = obs.get("hand") or []
                if hand and not any(adds_mana(c.get("definition")) for c in hand):
                    flags["kept_hand_with_no_mana"] += 1
                    if len(examples["kept_hand_with_no_mana"]) < 12:
                        examples["kept_hand_with_no_mana"].append(
                            f"{where}: kept " + ", ".join(
                                c.get("name", "?") for c in hand)
                            + " -- no land, no Mox, no Lotus")

            # Declining a free attack: they have nothing that can block.
            if (ctype in ("FinishDeclaringAttackers", "PassPriority")
                    and any(a.get("type") == "DeclareAttacker" for a in acts)
                    and not their_untapped_creatures(obs)):
                flags["no_attack_into_undefended"] += 1
                if len(examples["no_attack_into_undefended"]) < 12:
                    atk = [render_action(obs, a) for a in acts
                           if a.get("type") == "DeclareAttacker"][:3]
                    examples["no_attack_into_undefended"].append(
                        f"{where}: declined to attack with "
                        f"{'; '.join(atk)} -- they have NO untapped creatures")

            # Attacking into a blocker that kills us and survives.
            if ctype == "DeclareAttacker":
                atk = None
                for p in (obs.get("battlefield") or ()):
                    if p.get("objectId") == chosen.get("attacker"):
                        atk = p
                if atk is not None:
                    ap, at = creature_stats(atk)
                    lethal = [b for b in their_untapped_creatures(obs)
                              if creature_stats(b)[0] >= at
                              and creature_stats(b)[1] > ap
                              # a flyer they cannot block is not a bad attack
                              and (not atk.get("flying") or b.get("flying"))]
                    if lethal and len(their_untapped_creatures(obs)) == len(lethal):
                        flags["attacked_into_bigger_blocker"] += 1
                        if len(examples["attacked_into_bigger_blocker"]) < 12:
                            b = lethal[0]
                            bp, bt = creature_stats(b)
                            examples["attacked_into_bigger_blocker"].append(
                                f"{where}: {render_action(obs, chosen)} "
                                f"({ap}/{at}) into {b.get('name')} ({bp}/{bt}) "
                                f"-- it dies, the blocker lives")

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
                        unl = unlocked_by_one_more_land(obs)
                        if unl:
                            turn_land_would_unlock[turn] = unl
                if ctype == "CastSpell" and "PrecombatMain" in str(obs.get("step")):
                    turn_cast_precombat[turn] = True
                # PAYING MANA TO ANIMATE A LAND THAT IS ALREADY TAPPED.
                #
                # Observed in hosted play, in the SAME upkeep as the mana
                # burn above: tap Mishra's Factory for colourless, then spend
                # that mana animating the now-tapped Factory. A tapped 2/2
                # cannot attack, and the animation ends at end of turn so it
                # cannot block either -- the mana buys literally nothing.
                #
                # It is worth seeing this next to `floated_mana_into_burn`:
                # spending the mana AVOIDS the burn, so the second action is
                # locally correct. The error is the first one. Which means
                # the burn rate UNDERCOUNTS wasted mana -- sometimes the
                # waste hides in a null activation instead of showing up as
                # lost life.
                if ctype == "ActivateAbility":
                    src = chosen.get("source")
                    for perm in (obs.get("battlefield") or ()):
                        if perm.get("objectId") != src or not perm.get("tapped"):
                            continue
                        kind = CARDS.get(perm.get("definition"),
                                         ("", "", ""))[2]
                        if kind == "Land":
                            flags["animated_a_tapped_land"] += 1
                            if len(examples["animated_a_tapped_land"]) < 12:
                                examples["animated_a_tapped_land"].append(
                                    f"game {g} T{turn} {obs.get('step')}: paid "
                                    f"to animate {perm.get('name')} while it "
                                    f"was already tapped -- it cannot attack "
                                    f"or block")
                        break

                # FLOATING MANA THAT CANNOT BE SPENT -> MANA BURN.
                #
                # Reported from hosted play: the bot tapped Mishra's Factory
                # for colourless during its OWN UPKEEP and burned for 1.
                # Mana burn is a live rule in this format, so producing mana
                # with nothing to spend it on is not merely idle -- it deals
                # damage to us. If the same decision offers no spell to cast
                # and no ability with a mana cost, the mana has nowhere to go.
                if ctype == "ActivateManaAbility":
                    sinks = [a for a in acts
                             if a.get("type") in ("CastSpell", "ActivateAbility")]
                    if not sinks:
                        flags["floated_mana_into_burn"] += 1
                        if len(examples["floated_mana_into_burn"]) < 12:
                            examples["floated_mana_into_burn"].append(
                                f"game {g} T{turn} {obs.get('step')}: "
                                f"{render_action(obs, chosen)} with nothing to "
                                f"spend it on -- burns for that much")

                # DISCARDING A LAND WITH THE LAND DROP STILL UNUSED.
                #
                # Reported from hosted play: the bot discarded Mishra's
                # Factory on a turn it could have played it. Discarding a
                # land you were still allowed to play is strictly worse than
                # playing it -- the card leaves your hand either way, and
                # playing it leaves a permanent on the battlefield. Distinct
                # from `skipped_land_drop`, which is merely not playing one.
                if ctype == "DiscardCards" and not turn_played_land.get(turn):
                    lands = [name_of(card_def(obs, c))
                             for c in (chosen.get("cards") or ())
                             if CARDS.get(card_def(obs, c),
                                          ("", "", ""))[2] == "Land"]
                    if lands and turn in turn_land_offered_pre:
                        flags["discarded_a_playable_land"] += 1
                        if len(examples["discarded_a_playable_land"]) < 12:
                            examples["discarded_a_playable_land"].append(
                                f"game {g} T{turn}: discarded "
                                f"{', '.join(lands)} with the land drop "
                                f"still unused")
                if ctype == "PlayLand":
                    turn_played_land[turn] = True
                    # A land played AFTER combat produces mana this turn
                    # that can no longer be spent precombat or on combat
                    # tricks. When the same drop was available precombat,
                    # deferring it is a straight loss of options.

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
                    free = [a for a in acts if a.get("type") == "CastSpell"
                            and (cmc(card_def(obs, a.get("card"))) or 9) == 0]
                    if free:
                        passed_main_turns.add(turn)
                        flags["held_free_mana_artifact"] += 1
                        if len(examples["held_free_mana_artifact"]) < 12:
                            examples["held_free_mana_artifact"].append(
                                f"{where}: passed main holding "
                                f"{'; '.join(render_action(obs, a) for a in free[:3])}"
                                f" -- costs nothing to play")
            game.act(idx)
            n += 1

        for t, saw in turn_saw_land_action.items():
            if saw and not turn_played_land.get(t):
                flags["skipped_land_drop"] += 1
                if len(examples["skipped_land_drop"]) < 12:
                    examples["skipped_land_drop"].append(
                        f"game {g} T{t}: could play a land, did not")

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
