"""Behavioral sweep of ONE pilot deck vs every opponent deck.

The acceptance harness for deployment levers: plays --deck (default
"The Deck") piloted by --net against all 8 built-ins on the scout seed
block (seed = base + (pi*len(DECKS)+pj)*100 + g, identical to scout.py,
so results are comparable game-for-game with the recorded baselines:
7/128 wins pre-land-rule, 4/128 with it, spells/game ~5.5).

Reported per sweep: wins, spells/game, land skips per loss, and the
PROPERTY-DERIVED deploy counters (no card names in the logic):

  - big-threat casts/game: CastSpell of a creature with printed
    power >= 4 (Serra Angel class)
  - engine casts/game: CastSpell of a definition with an activated
    draw ability (extractor.draw_engine_defs -- Jayemdae Tome class)

Usage:
    python3 deck-sweep.py --net penta_net.npz [--deck "The Deck"]
"""

import argparse
import json
import re
import time
from multiprocessing import Pool

import scout
from extractor import Extractor, import_penta
from net import Net
from trainer import DECKS, DEFAULT_SEARCH

_CAST = re.compile(r"^Cast ([^-]+?)(?: x=\d+)?(?: ->.*)?$")


def deploy_counts(record, name_def, ex):
    """(big_threat_casts, engine_casts) from one scout transcript."""
    big = engines = 0
    for entry in record["transcript"]:
        match = _CAST.match(entry["action"])
        if not match:
            continue
        definition = name_def.get(match.group(1).strip())
        if definition is None:
            continue
        if definition in ex.draw_engine_defs:
            engines += 1
        elif ex.card_kind.get(definition) in ("Creature", "ArtifactCreature") \
                and ex.card_power.get(definition, 0) >= 4:
            big += 1
    return big, engines


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--net", default="penta_net.npz")
    parser.add_argument("--deck", default="The Deck")
    parser.add_argument("--games-per-opp", type=int, default=16)
    parser.add_argument("--seed-base", type=int, default=9_200_000)
    parser.add_argument("--max-eval", type=int, default=16)
    parser.add_argument("--workers", type=int, default=4)
    args = parser.parse_args()

    penta = import_penta()
    catalog = json.loads(penta.catalog())
    name_def = {c["name"]: c["definition"] for c in catalog["cards"]}
    net = Net.load(args.net)
    ex = Extractor.for_inputs(net.inputs)
    pi = DECKS.index(args.deck)

    tasks = []
    for pj, their in enumerate(DECKS):
        for g in range(args.games_per_opp):
            seat = "p1" if g % 2 == 0 else "p2"
            seed = args.seed_base + (pi * len(DECKS) + pj) * 100 + g
            tasks.append((args.deck, their, seat, seed, args.max_eval,
                          DEFAULT_SEARCH))

    t0 = time.time()
    with Pool(args.workers, initializer=scout._init,
              initargs=(args.net,)) as pool:
        games = pool.map(scout._play, tasks)
    n = len(games)
    wins = sum(1 for r in games if r["score"] == 1.0)
    losses = [r for r in games if r["score"] == 0.0]
    spells = sum(r["counters"].get("spells_cast", 0) for r in games) / n
    loss_skips = sum(r["counters"].get("turns_land_skipped", 0)
                     for r in losses) / max(1, len(losses))
    big = engines = 0
    for r in games:
        b, e = deploy_counts(r, name_def, ex)
        big += b
        engines += e
    print(f"{args.deck} sweep: {args.net} | {n} games in "
          f"{time.time() - t0:.0f}s")
    counterspells = sum(r["counters"].get("counterspells_cast", 0)
                        for r in games) / n
    print(f"  wins {wins}/{n} | spells/game {spells:.1f} | "
          f"land-skips/loss {loss_skips:.1f}")
    print(f"  big-threat casts/game {big / n:.2f} | "
          f"engine casts/game {engines / n:.2f} | "
          f"counterspells/game {counterspells:.2f}  (property-derived)")
    per_opp = {}
    for r in games:
        w, m = per_opp.get(r["their_deck"], (0, 0))
        per_opp[r["their_deck"]] = (w + (r["score"] == 1.0), m + 1)
    for deck, (w, m) in sorted(per_opp.items()):
        print(f"    vs {deck:13s} {w}/{m}")


if __name__ == "__main__":
    main()
