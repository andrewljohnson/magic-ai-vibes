"""Gate the OBSERVATION-ONLY hosted policy vs the current engine's
handcrafted bot -- the honest strength number for the public bot.

Uses the protocol-18 engine named by PENTA18_DIR (a directory holding a
freshly built penta.so from origin/main; never the pinned training
binding, which stays untouched) and drives our seat exclusively through
HostedPolicy.choose(observation) -- no clone, no playouts, exactly the
hosted contract.

Usage:
    PENTA18_DIR=/path/to/penta18/bindings/penta-py \
        python3 gate_hosted.py --games 120
"""

import argparse
import importlib.util
import json
import math
import os
import sys
import time
from multiprocessing import Pool

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from hosted_policy import HostedPolicy  # noqa: E402

DECKS = ("Sligh", "White Weenie", "The Deck", "Counterburn",
         "Goblins", "Erhnamgeddon", "Mono Black", "Jeskai Aggro")


def load_penta18():
    directory = os.environ["PENTA18_DIR"]
    spec = importlib.util.spec_from_file_location(
        "penta", os.path.join(directory, "penta.so"))
    module = importlib.util.module_from_spec(spec)
    sys.modules["penta"] = module
    spec.loader.exec_module(module)
    return module


_WORKER = {}


def _init(deck_name, head, weight):
    _WORKER["penta"] = load_penta18()
    _WORKER["policy"] = HostedPolicy(head_path=head, weight=weight)


def _play(task):
    my_seat, d1, d2, seed = task
    penta = _WORKER["penta"]
    policy = _WORKER["policy"]
    opponent_seat = "p2" if my_seat == "p1" else "p1"
    game = penta.Game(d1, d2, opponent="handcrafted",
                      opponent_seat=opponent_seat, seed=seed)
    moves = 0
    while game.result() is None and moves < 3000:
        obs = json.loads(game.observe())
        game.act(policy.choose(obs))
        moves += 1
    result = game.result()
    if result == my_seat:
        return 1.0
    if result == "draw" or result is None:
        return 0.5
    return 0.0


def wilson_lcb(wins, games, z=1.96):
    if games == 0:
        return 0.0
    p = wins / games
    denom = 1 + z * z / games
    centre = p + z * z / (2 * games)
    margin = z * math.sqrt(p * (1 - p) / games + z * z / (4 * games * games))
    return (centre - margin) / denom


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--games", type=int, default=120)
    parser.add_argument("--seed-base", type=int, default=5_000_000)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--head", default="policy_head.ckpt-dagger1.npz")
    parser.add_argument("--weight", type=float, default=0.15)
    args = parser.parse_args()

    pairs = [(a, b) for a in DECKS for b in DECKS if a != b]
    tasks = []
    for g in range(args.games):
        my_seat = "p1" if g % 2 == 0 else "p2"
        d1, d2 = pairs[(g // 2) % len(pairs)]
        tasks.append((my_seat, d1, d2, args.seed_base + g))
    t0 = time.time()
    with Pool(args.workers, initializer=_init,
              initargs=("", args.head, args.weight)) as pool:
        scores = pool.map(_play, tasks)
    took = time.time() - t0
    wins = sum(1 for s in scores if s == 1.0)
    draws = sum(1 for s in scores if s == 0.5)
    rate = sum(scores) / len(scores)
    print(f"hosted policy vs handcrafted (engine 18): {args.games} games: "
          f"{wins} wins, {draws} draws -> {100 * rate:.1f}% "
          f"(LCB {100 * wilson_lcb(wins, args.games):.1f}%)  "
          f"[{took:.0f}s, {args.games / took:.2f} games/s]")


if __name__ == "__main__":
    main()
