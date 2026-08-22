#!/usr/bin/env python3
"""Play two saved actors against each other. Mirror match, alternating seats.

    PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python head_to_head.py \
        A.npz B.npz --games 200

WHY THIS EXISTS
---------------
Every strength number in this project is "win rate vs the engine's
built-in handcrafted bot" -- and we also TRAIN roughly half our games
against that same bot (`--selfplay-frac 0.5`). Training on the opponent
you are scored against means a rise in the score can be either real
strength or opponent-specific exploitation, and the score alone cannot
tell you which.

Head-to-head is the tiebreaker. It is a second, independent axis: if
actor A scores worse than B against handcrafted but beats B directly,
the handcrafted number is measuring exploitation rather than strength.

It also answers the cycling question the roadmap flags: self-play can
produce non-transitive actors (new beats old, but both do worse against
a third party), and nothing in the current setup would notice.

Both actors pilot the same deck, so this is a mirror match -- deck
matchup cannot confound the result. Seats alternate so first-player
advantage cancels. Draws and games hitting the decision cap score 0.5.
"""
import argparse
import json
import os
import sys

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from extractor import import_penta, Extractor  # noqa: E402
from hosted_policy import load_decklists, belief_deck_context  # noqa: E402
from aac_selfplay import MAX_DECISIONS  # noqa: E402
import aac_torch as AT  # noqa: E402


def load(path, feat, hidden):
    net = AT.MLP(feat, hidden)
    d = np.load(path)
    net.load_state_dict({k: torch.as_tensor(d[k]) for k in d.files})
    net.eval()
    return net


def play(a, b, ex, penta, decks, deck, seed, a_is_p1, open_decklist):
    """One mirror game. Returns A's score (win 1 / draw .5 / loss 0)."""
    game = penta.Game(deck, deck, opponent="external", seed=seed)
    n = 0
    while game.result() is None and n < MAX_DECISIONS:
        seat = game.decision_seat()
        obs = json.loads(game.observe())
        acts = obs["legalActions"]
        if len(acts) == 1:
            game.act(acts[0]["index"])
            n += 1
            continue
        if ex.belief:
            # Mirror match, so our deck IS the opponent's deck; with
            # classification off this is what the trained actor saw.
            belief_deck_context(ex, decks, obs, deck if open_decklist else None)
        actor = a if ((seat == "p1") == a_is_p1) else b
        idx, _ = AT.decide(game, seat, obs, actor, ex, 1.0, sample=False)
        game.act(idx)
        n += 1
    res = game.result()
    if res is None or res == "draw":
        return 0.5
    a_seat = "p1" if a_is_p1 else "p2"
    return 1.0 if res == a_seat else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--games", type=int, default=200)
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--belief", action="store_true", default=True)
    ap.add_argument("--deck", default="Sligh")
    ap.add_argument("--seed-base", type=int, default=3_000_000)
    ap.add_argument("--open-decklist", action="store_true")
    args = ap.parse_args()

    penta = import_penta()
    ex = Extractor(version=2, belief=args.belief)
    decks = load_decklists()
    a = load(args.a, ex.size, args.hidden)
    b = load(args.b, ex.size, args.hidden)

    score = 0.0
    for g in range(args.games):
        score += play(a, b, ex, penta, decks, args.deck,
                      args.seed_base + g, g % 2 == 0, args.open_decklist)
        if (g + 1) % 25 == 0:
            rate = score / (g + 1)
            print(f"  {g + 1:>4} games: A {100 * rate:5.1f}%", flush=True)

    rate = score / args.games
    se = (rate * (1 - rate) / args.games) ** 0.5
    print()
    print(f"A = {os.path.basename(args.a)}")
    print(f"B = {os.path.basename(args.b)}")
    print(f"A scores {100 * rate:.1f}% ± {100 * se:.1f} over {args.games} "
          f"mirror games ({args.deck}, alternating seats)")
    if abs(rate - 0.5) < 2 * se:
        print("=> indistinguishable at 2 SE")
    else:
        print(f"=> {'A' if rate > 0.5 else 'B'} is stronger head-to-head")


if __name__ == "__main__":
    main()
