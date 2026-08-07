"""TD(lambda) self-play trainer for penta, external mode, afterstate values.

What is shipped (see README.md for the feasibility numbers behind this):

- Decision policy: epsilon-greedy over 1-ply AFTERSTATES. Copies of the
  live game come from the binding's `clone_game()` (engine >= 0.3);
  where the binding lacks it, the deterministic fallback reconstructs by
  `Game(same decks/seed)` + replaying the action history. Each candidate
  action is played on such a copy and the resulting observation (from the
  acting seat) is scored by the value net; greedy picks the argmax.
- Learning: TD(lambda), gamma=1, on each seat's trajectory of chosen
  afterstate values: the last recorded state's target is the 0/1(/0.5)
  outcome z, and going backward
  target[i] = (1-lambda) * v[i+1] + lambda * target[i+1], where v are the
  net's own values recorded at play time. Samples land in a replay ring;
  minibatch SGD with momentum (net.py) trains after every round.
  DEFAULT lambda is 1.0 (pure undiscounted outcome targets, the SPZ C++
  default and its champion --hard-targets setting): penta trajectories run
  40-130 recorded decisions per seat, so per-decision lambda 0.9 left most
  targets with <5% outcome weight -- the net trained on its own bootstrap
  echo and collapsed (audit 2026-08, checks.py). --td-lambda restores
  bootstrapping if wanted.
- Self-play runs both seats with the same net; multiprocessing across
  seeds (one Game per task, one process pool) provides parallelism.
- League play (anti-drift, per the SPZ C++ recipe): with probability
  --league-handcrafted a game is played against penta's built-in
  handcrafted bot, and otherwise with probability LEAGUE_FROZEN_FRACTION
  (total, split across --league-frozen snapshots) the opponent seat is a
  frozen snapshot net playing greedily. Handcrafted games record only the
  learner's seat (the built-in bot's decisions are invisible); frozen-
  snapshot games record BOTH seats -- snapshot trajectories are still true
  outcome-labeled observations (as in the SPZ C++ recipe), with bootstrap
  values re-evaluated by the LEARNER net. Mirror games record both seats
  as before. The mode roll is derived from the game seed, so runs stay
  deterministic per seed.

Forced moves (a single legal action; Concede left legalActions in protocol
1) are played without evaluation or recording; decisions with huge
branching factors evaluate a random sample of at most --max-eval
candidates.

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
# 1.0 = pure outcome targets (see module docstring for the collapse
# post-mortem); override with --td-lambda.
TD_LAMBDA = 1.0
# Total probability that a game seats a frozen snapshot (when any are
# given), split evenly across the --league-frozen nets.
LEAGUE_FROZEN_FRACTION = 0.25
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


def make_fork(make_game, history, game=None):
    """A callable producing copies of the live game state.

    Prefers the binding's `clone_game()` (engine >= 0.3). The fallback,
    for older bindings, rebuilds `Game(same args, same seed)` and replays
    the recorded action indices -- exact, because the engine is
    deterministic.
    """
    if game is not None and hasattr(game, "clone_game"):
        return game.clone_game

    def fork():
        copy = make_game()
        for played in history:
            copy.act(played)
        return copy

    return fork


def afterstate_rows(fork, seat, actions, extractor):
    """Play each candidate on a forked copy; return feature rows and
    terminal outcomes (None where the game continues)."""
    rows, terminals = [], []
    for action in actions:
        copy = fork()
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


def choose(fork, obs, net, extractor, rng, epsilon, max_eval, depth):
    """Epsilon-greedy afterstate choice.

    Returns (action_index, features, value); features/value are None for
    forced moves, which are not recorded in the trajectory.
    """
    actions = obs["legalActions"]
    if len(actions) == 1:
        return actions[0]["index"], None, None
    seat = obs["seat"]

    if epsilon > 0.0 and rng.random() < epsilon:
        picked = rng.choice(actions)
        rows, terms = afterstate_rows(fork, seat, [picked], extractor)
        value = terms[0] if terms[0] is not None else net.value(rows[0])
        return picked["index"], rows[0], value

    budget = eval_budget(depth, max_eval)
    if len(actions) > budget:
        actions = rng.sample(actions, budget)
    rows, terms = afterstate_rows(fork, seat, actions, extractor)
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
                       max_eval, opponent_net=None, learner_seat=None,
                       lam=TD_LAMBDA):
    """One external-mode game; returns (rows, targets, stats).

    Mirror mode (opponent_net None) plays and records both seats with the
    learner net. League-frozen mode seats opponent_net on the other seat,
    playing greedily (epsilon 0); its trajectory is still true
    outcome-labeled data and is recorded too (as in the SPZ C++ recipe),
    but with bootstrap values from the LEARNER net, so TD targets never
    mix in a stale net's value scale.
    """
    def make_game():
        return penta.Game(d1, d2, opponent="external", seed=seed)

    game = make_game()
    history = []
    traj = {"p1": ([], []), "p2": ([], [])}
    while game.result() is None and len(history) < MAX_DECISIONS:
        seat = game.decision_seat()
        obs = json.loads(game.observe())
        fork = make_fork(make_game, history, game)
        if opponent_net is not None and seat != learner_seat:
            index, feats, value = choose(
                fork, obs, opponent_net, extractor, rng,
                epsilon=0.0, max_eval=max_eval, depth=len(history))
            if feats is not None:
                value = net.value(feats)  # learner-net bootstrap
        else:
            index, feats, value = choose(
                fork, obs, net, extractor, rng, epsilon,
                max_eval, depth=len(history))
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
        targets.append(td_targets(values, z, lam))
    stats = {"decisions": len(history), "result": result or "cap"}
    if rows:
        return np.concatenate(rows), np.concatenate(targets), stats
    return (np.empty((0, extractor.size), dtype=np.float32),
            np.empty(0, dtype=np.float32), stats)


def play_handcrafted_game(net, extractor, penta, d1, d2, seed, epsilon, rng,
                          max_eval, learner_seat, lam=TD_LAMBDA):
    """One league game vs penta's built-in handcrafted bot.

    Only the learner's seat is observed and recorded (reconstruction
    replays our own action indices, exactly as gate.py does against the
    seeded built-in opponents). Returns (rows, targets, stats).
    """
    opponent_seat = "p2" if learner_seat == "p1" else "p1"

    def make_game():
        return penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=opponent_seat, seed=seed)

    game = make_game()
    history = []
    feats_list, values = [], []
    while game.result() is None and len(history) < MAX_DECISIONS:
        obs = json.loads(game.observe())
        index, feats, value = choose(
            make_fork(make_game, history, game), obs, net, extractor, rng,
            epsilon, max_eval, depth=len(history))
        game.act(index)
        history.append(index)
        if feats is not None:
            feats_list.append(feats)
            values.append(value)

    result = game.result()  # None if the cap was hit
    stats = {"decisions": len(history), "result": result or "cap"}
    if not feats_list:
        return (np.empty((0, extractor.size), dtype=np.float32),
                np.empty(0, dtype=np.float32), stats)
    if result is None or result == "draw":
        z = 0.5
    else:
        z = 1.0 if result == learner_seat else 0.0
    return np.stack(feats_list), td_targets(values, z, lam), stats


# -- multiprocessing plumbing -------------------------------------------

_WORKER = {}


def _worker_init(hidden, frozen_paths):
    penta = import_penta()
    extractor = Extractor()
    _WORKER["penta"] = penta
    _WORKER["extractor"] = extractor
    _WORKER["net"] = Net(extractor.size, hidden=hidden, seed=0)
    _WORKER["frozen"] = [Net.load(path) for path in frozen_paths]


def _worker_play(task):
    weights, games, max_eval, lam = task
    net = _WORKER["net"]
    net.set_weights(weights)
    extractor = _WORKER["extractor"]
    penta = _WORKER["penta"]
    all_rows, all_targets, all_stats = [], [], []
    for d1, d2, seed, epsilon, mode, frozen_idx, learner_seat in games:
        rng = random.Random(seed * 2654435761 % (2**31))
        try:
            if mode == "handcrafted":
                rows, targets, stats = play_handcrafted_game(
                    net, extractor, penta, d1, d2, seed, epsilon, rng,
                    max_eval, learner_seat, lam=lam)
            else:
                opponent_net = (_WORKER["frozen"][frozen_idx]
                                if mode == "frozen" else None)
                rows, targets, stats = play_selfplay_game(
                    net, extractor, penta, d1, d2, seed, epsilon, rng,
                    max_eval, opponent_net=opponent_net,
                    learner_seat=learner_seat, lam=lam)
        except ValueError as error:
            # Upstream engine fault mid-game (seen on 0.3.0: "the scripted
            # opponent returned no action" from the built-in handcrafted
            # policy). The game cannot continue and has no outcome, so it
            # contributes no samples; the round line reports the count.
            rows = np.empty((0, extractor.size), dtype=np.float32)
            targets = np.empty(0, dtype=np.float32)
            stats = {"decisions": 0, "result": "engine-error",
                     "error": f"{d1} vs {d2} seed {seed}: {error}"}
        all_rows.append(rows)
        all_targets.append(targets)
        all_stats.append(stats)
    return np.concatenate(all_rows), np.concatenate(all_targets), all_stats


def matchup(game_number):
    """Rotate through ordered deck pairs (mirrors excluded)."""
    pairs = [(a, b) for a in DECKS for b in DECKS if a != b]
    return pairs[game_number % len(pairs)]


N_PAIRS = len(DECKS) * (len(DECKS) - 1)


def learner_seat_for(game_number):
    """Learner seat for league games. Plain `number % 2` is aliased with
    matchup(number) -- the pair count is even, so each ordered deck pair
    would ALWAYS put the learner on the same seat. Flipping the parity
    every full rotation gives every pair both seats over 2*N_PAIRS games.
    """
    return "p1" if (game_number + game_number // N_PAIRS) % 2 == 0 else "p2"


def league_roll(seed, handcrafted_fraction, n_frozen):
    """Deterministic per-seed opponent draw: ("mirror"|"handcrafted"|
    "frozen", frozen_idx). Uses its own RNG stream so play RNG is
    untouched."""
    rng = random.Random(seed * 2246822519 % (2**31))
    roll = rng.random()
    if roll < handcrafted_fraction:
        return "handcrafted", -1
    if n_frozen and roll < handcrafted_fraction + LEAGUE_FROZEN_FRACTION:
        return "frozen", rng.randrange(n_frozen)
    return "mirror", -1


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
    parser.add_argument("--td-lambda", type=float, default=TD_LAMBDA,
                        help="TD lambda per recorded decision; 1.0 (default)"
                             " = pure outcome targets, no bootstrapping")
    parser.add_argument("--replay-capacity", type=int, default=150_000)
    parser.add_argument("--round-games", type=int, default=64)
    parser.add_argument("--seed-base", type=int, default=1_000_000)
    parser.add_argument("--out", default="penta_net.npz")
    parser.add_argument("--init", default="",
                        help="warm-start weights (.npz) to continue from")
    parser.add_argument("--league-frozen", action="append", default=[],
                        metavar="PATH",
                        help="frozen snapshot net for league games "
                             "(repeatable; ~25%% of games total seat one "
                             "as the opponent)")
    parser.add_argument("--league-handcrafted", type=float, default=0.0,
                        metavar="FRACTION",
                        help="fraction of games played vs the built-in "
                             "handcrafted bot")
    args = parser.parse_args()

    extractor = Extractor()
    net = Net(extractor.size, hidden=args.hidden, seed=1)
    if args.init:
        net = Net.load(args.init)
    print(f"engine {extractor.engine_version} protocol "
          f"{extractor.protocol_version}; features {extractor.size} "
          f"({extractor.defs} defs x 5 zones + {extractor.n_scalars} scalars); "
          f"td-lambda {args.td_lambda:.2f}")
    if args.league_handcrafted or args.league_frozen:
        frozen_frac = LEAGUE_FROZEN_FRACTION if args.league_frozen else 0.0
        print(f"league: handcrafted {args.league_handcrafted:.2f}, frozen "
              f"{frozen_frac:.2f} across {len(args.league_frozen)} "
              f"snapshot(s) {args.league_frozen}")

    replay_X = np.empty((args.replay_capacity, extractor.size), dtype=np.float32)
    replay_y = np.empty(args.replay_capacity, dtype=np.float32)
    replay_size = 0
    replay_cursor = 0
    train_rng = np.random.default_rng(7)

    played = 0
    t_start = time.time()
    with Pool(args.workers, initializer=_worker_init,
              initargs=(args.hidden, tuple(args.league_frozen))) as pool:
        while played < args.games:
            round_n = min(args.round_games, args.games - played)
            frac = played / max(1, args.games)
            epsilon = (args.epsilon_start
                       + (args.epsilon_final - args.epsilon_start) * frac)
            game_specs = []
            for g in range(round_n):
                number = played + g
                d1, d2 = matchup(number)
                seed = args.seed_base + number
                mode, frozen_idx = league_roll(
                    seed, args.league_handcrafted, len(args.league_frozen))
                learner_seat = learner_seat_for(number)
                game_specs.append(
                    (d1, d2, seed, epsilon, mode, frozen_idx, learner_seat))
            per_worker = [game_specs[w::args.workers]
                          for w in range(args.workers)]
            weights = net.get_weights()
            tasks = [(weights, chunk, args.max_eval, args.td_lambda)
                     for chunk in per_worker if chunk]
            results = pool.map(_worker_play, tasks)

            new = 0
            decisions, caps = [], 0
            engine_errors = []
            for rows, targets, stats in results:
                for s in stats:
                    if s["result"] == "engine-error":
                        engine_errors.append(s["error"])
                        continue
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
            for error in engine_errors:
                print(f"      dropped game (engine error): {error}",
                      flush=True)

    print(f"done: {played} games -> {args.out}")


if __name__ == "__main__":
    main()
