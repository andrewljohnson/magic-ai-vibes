"""Generate the winner-imitation archive for the policy-head prototype.

Mirror self-play with the current best net (both seats, standard search
config, epsilon 0), recording at every multi-action greedy decision:
the POST-PRUNE candidate afterstate features (the exact set choose()
screens -- decisions whose action count exceeds the eval budget are
played but not archived, since the sampled subset lives inside
choose()'s rng), which candidate was chosen, and whether the deciding
seat eventually WON. Games that cap or draw contribute nothing.

Output .npz (float16 X): X [n, features], y [n] (1 = chosen by the
eventual winner, 0 = offered to the winner but declined), group [n]
(decision id, for rank-accuracy evaluation). Only winner-seat
decisions are stored: winner-imitation, losers teach nothing here.

Usage:
    python3 gen_policy_archive.py --net penta_net.ckpt-v3-48000.npz \
        --games 1600 --out policy_archive.npz
"""

import argparse
import json
import random
import time
from multiprocessing import Pool

import numpy as np

from extractor import Extractor, import_penta
from net import Net
from trainer import (DEFAULT_SEARCH, MAX_DECISIONS, afterstate_rows, choose,
                     dominance_prune, eval_budget, make_fork, matchup)

_WORKER = {}


def _init(net_path):
    _WORKER["penta"] = import_penta()
    _WORKER["net"] = Net.load(net_path)
    _WORKER["extractor"] = Extractor.for_inputs(_WORKER["net"].inputs)


def _play(task):
    d1, d2, seed, max_eval = task
    penta, net, ex = (_WORKER["penta"], _WORKER["net"],
                      _WORKER["extractor"])

    def make_game():
        return penta.Game(d1, d2, opponent="external", seed=seed)

    game = make_game()
    rng = random.Random(seed)
    history = []
    # per-seat lists of (candidate_rows, chosen_position)
    decisions = {"p1": [], "p2": []}
    while game.result() is None and len(history) < MAX_DECISIONS:
        seat = game.decision_seat()
        obs = json.loads(game.observe())
        actions = obs["legalActions"]
        fork = make_fork(make_game, history, game)
        record = None
        if 1 < len(actions) <= eval_budget(len(history), max_eval):
            # The exact screen set: prune first (deterministic), then
            # afterstates. choose() will redo this identically.
            kept = dominance_prune(fork, obs, actions, ex)
            if len(kept) > 1:
                rows, _ = afterstate_rows(fork, seat, kept, ex)
                record = (kept, rows)
        index, _, _ = choose(fork, obs, net, ex, rng, epsilon=0.0,
                             max_eval=max_eval, depth=len(history),
                             search=DEFAULT_SEARCH)
        if record is not None:
            kept, rows = record
            positions = [i for i, a in enumerate(kept)
                         if a["index"] == index]
            if positions:  # chosen action was inside the archived set
                decisions[seat].append((rows, positions[0]))
        game.act(index)
        history.append(index)

    result = game.result()
    if result not in ("p1", "p2"):
        return None  # cap or draw: no winner to imitate
    won = decisions[result]
    if not won:
        return None
    X, y = [], []
    sizes = []
    for rows, chosen in won:
        for i, row in enumerate(rows):
            X.append(row.astype(np.float16))
            y.append(1.0 if i == chosen else 0.0)
        sizes.append(len(rows))
    return (np.stack(X), np.asarray(y, dtype=np.float16),
            np.asarray(sizes, dtype=np.int32))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--net", default="penta_net.ckpt-v3-48000.npz")
    parser.add_argument("--games", type=int, default=1600)
    parser.add_argument("--seed-base", type=int, default=4_200_000)
    parser.add_argument("--max-eval", type=int, default=16)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--out", default="policy_archive.npz")
    args = parser.parse_args()

    tasks = []
    for g in range(args.games):
        d1, d2 = matchup(g)
        tasks.append((d1, d2, args.seed_base + g, args.max_eval))
    t0 = time.time()
    with Pool(args.workers, initializer=_init, initargs=(args.net,)) as pool:
        results = pool.map(_play, tasks)
    kept = [r for r in results if r is not None]
    X = np.concatenate([r[0] for r in kept])
    y = np.concatenate([r[1] for r in kept])
    group = np.concatenate([
        np.repeat(np.arange(len(r[2])) + offset, r[2])
        for r, offset in zip(
            kept, np.cumsum([0] + [len(r[2]) for r in kept[:-1]]))
    ]).astype(np.int32)
    took = time.time() - t0
    np.savez_compressed(args.out, X=X, y=y, group=group,
                        net=args.net, seed_base=args.seed_base)
    print(f"{len(kept)}/{len(tasks)} decisive games in {took:.0f}s "
          f"({len(tasks) / took:.2f} games/s)")
    print(f"{len(y)} candidate rows, {int(y.sum())} winner-chosen, "
          f"{group[-1] + 1} decisions -> {args.out}")


if __name__ == "__main__":
    main()
