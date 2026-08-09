"""Gate a trained net against penta's built-in bots.

Plays the greedy afterstate policy (epsilon 0, same machinery as
trainer.choose) against `handcrafted` and `random`, alternating seats and
rotating deck pairs, with fixed seeds. Afterstate copies come from
`clone_game()` when the binding has it; the replay-reconstruction
fallback works in opponent mode too, because the built-in opponent is
seeded.

Usage:
    python3 gate.py --net penta_net.npz --games 200
"""

import argparse
import json
import math
import random
import time
from multiprocessing import Pool

from extractor import Extractor, import_penta
from net import Net
from trainer import DEFAULT_SEARCH, DECKS, SearchConfig, choose, make_fork, \
    matchup


def play_gate_game(net, extractor, penta, opponent, my_seat, d1, d2, seed,
                   max_eval, rng, search=None):
    opponent_seat = "p2" if my_seat == "p1" else "p1"

    def make_game():
        return penta.Game(d1, d2, opponent=opponent,
                          opponent_seat=opponent_seat, seed=seed)

    game = make_game()
    history = []
    while game.result() is None and len(history) < 3000:
        obs = json.loads(game.observe())
        try:
            index, _, _ = choose(make_fork(make_game, history, game), obs,
                                 net, extractor, rng, epsilon=0.0,
                                 max_eval=max_eval, depth=len(history),
                                 search=search)
            game.act(index)
        except ValueError:
            # Upstream engine fault (e.g. the 0.3.0 handcrafted policy
            # returning no action): the game cannot continue either way,
            # so score it as the drawish 0.5 rather than crashing the gate.
            return 0.5
        history.append(index)
    result = game.result()
    if result == my_seat:
        return 1.0
    if result == "draw" or result is None:
        return 0.5
    return 0.0


_WORKER = {}


def _init(net_path):
    _WORKER["penta"] = import_penta()
    _WORKER["net"] = Net.load(net_path)
    # Feature schemas are versioned by input size (extractor.py): old
    # 675-input nets gate on the v1 layout, new nets on v2.
    _WORKER["extractor"] = Extractor.for_inputs(_WORKER["net"].inputs)


def _play(task):
    opponent, my_seat, d1, d2, seed, max_eval, search = task
    rng = random.Random(seed)
    return play_gate_game(_WORKER["net"], _WORKER["extractor"],
                          _WORKER["penta"], opponent, my_seat, d1, d2, seed,
                          max_eval, rng, search=search)


def wilson_lcb(wins, games, z=1.96):
    """95% lower confidence bound on the win rate."""
    if games == 0:
        return 0.0
    p = wins / games
    denom = 1 + z * z / games
    centre = p + z * z / (2 * games)
    margin = z * math.sqrt(p * (1 - p) / games + z * z / (4 * games * games))
    return (centre - margin) / denom


def run_gate(net_path, opponent, games, seed_base, max_eval, workers,
             search=None):
    tasks = []
    for g in range(games):
        my_seat = "p1" if g % 2 == 0 else "p2"
        d1, d2 = matchup(g // 2)
        tasks.append((opponent, my_seat, d1, d2, seed_base + g, max_eval,
                      search))
    t0 = time.time()
    with Pool(workers, initializer=_init, initargs=(net_path,)) as pool:
        scores = pool.map(_play, tasks)
    took = time.time() - t0
    wins = sum(1.0 for s in scores if s == 1.0)
    draws = sum(1.0 for s in scores if s == 0.5)
    rate = sum(scores) / games
    print(f"vs {opponent:12s} {games} games: {wins:.0f} wins, {draws:.0f} "
          f"draws -> {100 * rate:5.1f}% (LCB {100 * wilson_lcb(wins, games):5.1f}%)"
          f"  [{took:.0f}s, {games / took:.2f} games/s]")
    return rate


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--net", default="penta_net.npz")
    parser.add_argument("--games", type=int, default=200)
    parser.add_argument("--seed-base", type=int, default=5_000_000)
    parser.add_argument("--max-eval", type=int, default=16)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--search-topk", type=int,
                        default=DEFAULT_SEARCH.top_k,
                        help="rollout-search top-k at gate time; 0 gates "
                             "the plain 1-ply policy")
    parser.add_argument("--playouts", type=int,
                        default=DEFAULT_SEARCH.playouts)
    parser.add_argument("--playout-budget", type=int,
                        default=DEFAULT_SEARCH.budget)
    parser.add_argument("--playout-max-eval", type=int,
                        default=DEFAULT_SEARCH.playout_max_eval)
    parser.add_argument("--opponents", nargs="+",
                        default=["random", "handcrafted"])
    args = parser.parse_args()

    # HONESTY: with search on, playout clones carry true hidden state
    # (upstream issue #11 tracks determinized clones), so gate results
    # vs the scripted bots inherit that leak through the search policy.
    search = None
    if args.search_topk > 0:
        search = SearchConfig(
            top_k=args.search_topk, playouts=args.playouts,
            budget=args.playout_budget,
            playout_max_eval=args.playout_max_eval,
            playout_epsilon=DEFAULT_SEARCH.playout_epsilon)

    net = Net.load(args.net)
    meta = getattr(net, "metadata", {})
    print(f"net {args.net}: inputs {net.inputs}, hidden {len(net.w2)}, "
          f"meta {({k: v.item() if v.shape == () else v for k, v in meta.items()})}")
    print(f"decks: {', '.join(DECKS)}; alternating seats; "
          f"seeds from {args.seed_base}; "
          f"search {search if search else 'off (1-ply)'}")
    for opponent in args.opponents:
        run_gate(args.net, opponent, args.games, args.seed_base,
                 args.max_eval, args.workers, search=search)


if __name__ == "__main__":
    main()
