#!/usr/bin/env python3
"""Evaluate the bare first_bot ordering — no network — against the
engine's built-in bot, through the SAME protocol the actors are scored on.

    PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python gate_heuristic.py --games 400

WHY: hosted_policy.choose()'s docstring claims "first_bot's bare ordering
alone beats handcrafted ~73%". Our trained actor is at ~51%. If that claim
is real and comparable, a fixed category order (land > cast > attack >
rest, ties by action index) is 22 points ahead of every net in this repo
and the whole neural programme is behind a baseline we already wrote. If
it is stale or was measured some other way, the claim needs deleting
before someone plans around it.

Same protocol as aac_native.gate: alternating seats, rotating all 14
opponent decks, capped games scored as draws.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from extractor import import_penta  # noqa: E402
from hosted_policy import load_decklists, _TIE_ORDER  # noqa: E402
from aac_selfplay import MAX_DECISIONS  # noqa: E402


def pick(actions):
    """first_bot's bare ordering: category first, then action index."""
    return min(actions, key=lambda a: (_TIE_ORDER.get(a.get("type"), 7),
                                       a["index"]))["index"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=400)
    ap.add_argument("--learner-deck", default="Sligh")
    ap.add_argument("--seed-base", type=int, default=900_000)
    args = ap.parse_args()

    penta = import_penta()
    decks = list(load_decklists())
    opps = [d for d in decks if d != args.learner_deck]

    score = 0.0
    for g in range(args.games):
        my_p1 = g % 2 == 0
        opp_deck = opps[g % len(opps)]
        d1, d2 = ((args.learner_deck, opp_deck) if my_p1
                  else (opp_deck, args.learner_deck))
        my_seat = "p1" if my_p1 else "p2"
        game = penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=("p2" if my_p1 else "p1"),
                          seed=args.seed_base + g)
        n = 0
        while game.result() is None and n < MAX_DECISIONS:
            obs = json.loads(game.observe())
            game.act(pick(obs["legalActions"]))
            n += 1
        res = game.result()
        score += 1.0 if res == my_seat else (0.5 if res in (None, "draw") else 0.0)
        if (g + 1) % 100 == 0:
            print(f"  {g + 1:>4} games: {100 * score / (g + 1):5.1f}%",
                  flush=True)

    rate = score / args.games
    se = (rate * (1 - rate) / args.games) ** 0.5
    print()
    print(f"first_bot bare ordering (NO net): {100 * rate:.1f}% "
          f"± {100 * se:.1f} over {args.games} games")
    print(f"  trained actor for comparison:   ~51%")
    print(f"  parity with the built-in bot:    50.0%")


if __name__ == "__main__":
    main()
