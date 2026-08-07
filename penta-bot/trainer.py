"""TD(lambda) self-play trainer for penta, external mode, afterstate values.

What is shipped (see README.md for the feasibility numbers behind this):

- Decision policy: epsilon-greedy over 1-ply AFTERSTATES. penta's Python
  binding exposes no clone, but the engine is deterministic, so a copy of
  the live game is reconstructed by `Game(same decks/seed)` + replaying the
  action history (~0.1 ms at decision 30). Each candidate action is played
  on such a copy and the resulting observation (from the acting seat) is
  scored by the value net; greedy picks the argmax.
- Learning: TD(lambda=0.9), gamma=1, on each seat's trajectory of chosen
  afterstate values, exactly mirroring the SPZ C++ recipe: the last
  recorded state's target is the 0/1(/0.5) outcome z, and going backward
  target[i] = (1-lambda) * v[i+1] + lambda * target[i+1], where v are the
  net's own values recorded at play time. Samples land in a replay ring;
  minibatch SGD with momentum (net.py) trains after every round.
- Self-play runs both seats with the same net; multiprocessing across
  seeds (one Game per task, one process pool) provides parallelism.

Forced moves (a single non-Concede action) are played without evaluation
or recording; decisions with huge branching factors evaluate a random
sample of at most --max-eval candidates.

Usage:
    python3 trainer.py --games 3000 --workers 8 --out penta_net.npz
"""

import argparse
import json
import random
import time
from multiprocessing import Pool

import numpy as np

from extractor import Extractor, import_penta
from net import Net

DECKS = ("Sligh", "White Weenie", "The Deck", "Counterburn")
TD_LAMBDA = 0.9
# Games that have not ended by here are passing-loop junk; score as a draw.
MAX_DECISIONS = 600


def eval_budget(depth, max_eval):
    """Replay cost is O(history length) per candidate, so evaluate fewer
    candidates the deeper the game runs; keeps a decision's cost bounded."""
    if depth < 200:
        return max_eval
    if depth < 400:
        return max(4, max_eval // 2)
    return max(4, max_eval // 4)


def candidate_actions(obs):
    acts = [a for a in obs["legalActions"] if a["type"] != "Concede"]
    return acts if acts else obs["legalActions"]


def afterstate_rows(make_game, history, seat, actions, extractor):
    """Play each candidate on a reconstructed copy; return feature rows and
    terminal outcomes (None where the game continues)."""
    rows, terminals = [], []
    for action in actions:
        copy = make_game()
        for played in history:
            copy.act(played)
        copy.act(action["index"])
        result = copy.result()
        obs_after = json.loads(copy.observe(seat))
        rows.append(extractor.features(obs_after))
        if result is None:
            terminals.append(None)
        elif result == "draw":
            terminals.append(0.5)
        else:
            terminals.append(1.0 if result == seat else 0.0)
    return rows, terminals


def choose(make_game, history, obs, net, extractor, rng, epsilon, max_eval):
    """Epsilon-greedy afterstate choice.

    Returns (action_index, features, value); features/value are None for
    forced moves, which are not recorded in the trajectory.
    """
    actions = candidate_actions(obs)
    if len(actions) == 1:
        return actions[0]["index"], None, None
    seat = obs["seat"]

    if epsilon > 0.0 and rng.random() < epsilon:
        picked = rng.choice(actions)
        rows, terms = afterstate_rows(make_game, history, seat, [picked], extractor)
        value = terms[0] if terms[0] is not None else net.value(rows[0])
        return picked["index"], rows[0], value

    budget = eval_budget(len(history), max_eval)
    if len(actions) > budget:
        actions = rng.sample(actions, budget)
    rows, terms = afterstate_rows(make_game, history, seat, actions, extractor)
    values = net.value_batch(np.stack(rows))
    scores = [t if t is not None else v for t, v in zip(terms, values)]
    best = int(np.argmax(scores))
    return actions[best]["index"], rows[best], float(scores[best])


def td_targets(values, z, lam=TD_LAMBDA):
    """Backward TD(lambda) targets over one seat's recorded values, gamma=1.
    Mirrors the SPZ C++ target loop with the recorded values as bootstraps."""
    n = len(values)
    targets = np.empty(n, dtype=np.float32)
    tail = z
    targets[n - 1] = tail
    for i in range(n - 2, -1, -1):
        tail = (1.0 - lam) * values[i + 1] + lam * tail
        targets[i] = tail
    return targets


def play_selfplay_game(net, extractor, penta, d1, d2, seed, epsilon, rng,
                       max_eval):
    """One external-mode self-play game; returns (rows, targets, stats)."""
    def make_game():
        return penta.Game(d1, d2, opponent="external", seed=seed)

    game = make_game()
    history = []
    traj = {"p1": ([], []), "p2": ([], [])}
    while game.result() is None and len(history) < MAX_DECISIONS:
        seat = game.decision_seat()
        obs = json.loads(game.observe())
        index, feats, value = choose(
            make_game, history, obs, net, extractor, rng, epsilon, max_eval)
        game.act(index)
        history.append(index)
        if feats is not None:
            traj[seat][0].append(feats)
            traj[seat][1].append(value)

    result = game.result()  # None if the cap was hit
    rows, targets = [], []
    for seat in ("p1", "p2"):
        feats, values = traj[seat]
        if not feats:
            continue
        if result is None or result == "draw":
            z = 0.5
        else:
            z = 1.0 if result == seat else 0.0
        rows.append(np.stack(feats))
        targets.append(td_targets(values, z))
    stats = {"decisions": len(history), "result": result or "cap"}
    if rows:
        return np.concatenate(rows), np.concatenate(targets), stats
    return (np.empty((0, extractor.size), dtype=np.float32),
            np.empty(0, dtype=np.float32), stats)


# -- multiprocessing plumbing -------------------------------------------

_WORKER = {}


def _worker_init(hidden):
    penta = import_penta()
    extractor = Extractor()
    _WORKER["penta"] = penta
    _WORKER["extractor"] = extractor
    _WORKER["net"] = Net(extractor.size, hidden=hidden, seed=0)


def _worker_play(task):
    weights, games, max_eval = task
    net = _WORKER["net"]
    net.set_weights(weights)
    extractor = _WORKER["extractor"]
    penta = _WORKER["penta"]
    all_rows, all_targets, all_stats = [], [], []
    for d1, d2, seed, epsilon in games:
        rng = random.Random(seed * 2654435761 % (2**31))
        rows, targets, stats = play_selfplay_game(
            net, extractor, penta, d1, d2, seed, epsilon, rng, max_eval)
        all_rows.append(rows)
        all_targets.append(targets)
        all_stats.append(stats)
    return np.concatenate(all_rows), np.concatenate(all_targets), all_stats


def matchup(game_number):
    """Rotate through ordered deck pairs (mirrors excluded)."""
    pairs = [(a, b) for a in DECKS for b in DECKS if a != b]
    return pairs[game_number % len(pairs)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--games", type=int, default=3000)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--hidden", type=int, default=64)
    parser.add_argument("--lr", type=float, default=0.01)
    parser.add_argument("--batch", type=int, default=256)
    parser.add_argument("--epsilon-start", type=float, default=0.25)
    parser.add_argument("--epsilon-final", type=float, default=0.03)
    parser.add_argument("--max-eval", type=int, default=16)
    parser.add_argument("--replay-capacity", type=int, default=150_000)
    parser.add_argument("--round-games", type=int, default=64)
    parser.add_argument("--seed-base", type=int, default=1_000_000)
    parser.add_argument("--out", default="penta_net.npz")
    parser.add_argument("--init", default="",
                        help="warm-start weights (.npz) to continue from")
    args = parser.parse_args()

    extractor = Extractor()
    net = Net(extractor.size, hidden=args.hidden, seed=1)
    if args.init:
        net = Net.load(args.init)
    print(f"engine {extractor.engine_version} protocol "
          f"{extractor.protocol_version}; features {extractor.size} "
          f"({extractor.defs} defs x 5 zones + {extractor.n_scalars} scalars)")

    replay_X = np.empty((args.replay_capacity, extractor.size), dtype=np.float32)
    replay_y = np.empty(args.replay_capacity, dtype=np.float32)
    replay_size = 0
    replay_cursor = 0
    train_rng = np.random.default_rng(7)

    played = 0
    t_start = time.time()
    with Pool(args.workers, initializer=_worker_init,
              initargs=(args.hidden,)) as pool:
        while played < args.games:
            round_n = min(args.round_games, args.games - played)
            frac = played / max(1, args.games)
            epsilon = (args.epsilon_start
                       + (args.epsilon_final - args.epsilon_start) * frac)
            game_specs = []
            for g in range(round_n):
                number = played + g
                d1, d2 = matchup(number)
                game_specs.append((d1, d2, args.seed_base + number, epsilon))
            per_worker = [game_specs[w::args.workers]
                          for w in range(args.workers)]
            weights = net.get_weights()
            tasks = [(weights, chunk, args.max_eval)
                     for chunk in per_worker if chunk]
            results = pool.map(_worker_play, tasks)

            new = 0
            decisions, caps = [], 0
            for rows, targets, stats in results:
                for s in stats:
                    decisions.append(s["decisions"])
                    caps += s["result"] == "cap"
                for i in range(len(targets)):
                    replay_X[replay_cursor] = rows[i]
                    replay_y[replay_cursor] = targets[i]
                    replay_cursor = (replay_cursor + 1) % args.replay_capacity
                new += len(targets)
            replay_size = min(replay_size + new, args.replay_capacity)
            played += round_n

            steps = max(1, 2 * new // args.batch)
            losses = []
            for _ in range(steps):
                pick = train_rng.integers(0, replay_size, size=args.batch)
                losses.append(net.train_batch(
                    replay_X[pick], replay_y[pick], args.lr))
            net.save(args.out,
                     engine_version=extractor.engine_version,
                     protocol_version=extractor.protocol_version,
                     hidden=args.hidden, games=played)
            rate = played / (time.time() - t_start)
            print(f"games {played:5d}  eps {epsilon:.3f}  "
                  f"loss {np.mean(losses):.4f}  "
                  f"samples {new:5d}  replay {replay_size:6d}  "
                  f"len {np.mean(decisions):5.1f}  caps {caps}  "
                  f"{rate:5.2f} games/s", flush=True)

    print(f"done: {played} games -> {args.out}")


if __name__ == "__main__":
    main()
