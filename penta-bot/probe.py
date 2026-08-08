"""Behavioral + calibration DIFF of two value nets (forensic probe).

Built for the 2026-08 collapse forensics: given a HEALTHY net and a
COLLAPSED net, play the same fixed-seed games vs the built-in handcrafted
bot with each and diff

  a. behavior: action-TYPE distribution at contested decisions (>1 legal
     action), attack/block opportunity take-rates, spells/lands per game,
     game length, final life both sides, loss reasons (life / deck-out /
     decision-cap);
  b. value-transform calibration on ~300 mid-game observations sampled
     from those games: value delta for discarding a hand card
     (feature-level transform: hand-slot count -0.25, hand-size scalar
     -1/7) and value-vs-hand-size sweep (hand-size scalar alone swept
     0..7, rest fixed);
  c. prediction sanity: mean value at game start (~0.5 expected), mean
     value in eventually-won vs eventually-lost games, and a 10-bin
     calibration curve (predicted value vs realized outcome).

Usage:
    python3 probe.py --net-a penta_net.npz --net-b penta_net.ckpt-9000.npz \
        --label-a healthy --label-b collapsed --games 100
"""

import argparse
import json
import random
import time
from multiprocessing import Pool

import numpy as np

from extractor import Extractor, import_penta
from net import Net
from trainer import choose, make_fork, matchup

# Action-type buckets for the contested-decision distribution.
BUCKETS = (
    "PlayLand", "CastSpell", "Ability", "DeclareAttacker",
    "FinishDeclaringAttackers", "DeclareBlocker",
    "FinishDeclaringBlockers", "PassPriority", "Other",
)
_BUCKET_OF = {
    "PlayLand": "PlayLand",
    "CastSpell": "CastSpell",
    "ActivateAbility": "Ability",
    "ActivateManaAbility": "Ability",
    "DeclareAttacker": "DeclareAttacker",
    "FinishDeclaringAttackers": "FinishDeclaringAttackers",
    "DeclareBlocker": "DeclareBlocker",
    "FinishDeclaringBlockers": "FinishDeclaringBlockers",
    "PassPriority": "PassPriority",
}

MAX_DECISIONS = 1000  # probe cap; > trainer's 600 so true caps are rare


def bucket(action_type):
    return _BUCKET_OF.get(action_type, "Other")


def play_probe_game(net, extractor, penta, my_seat, d1, d2, seed, max_eval,
                    rng):
    """One greedy game vs handcrafted; returns a per-game record dict."""
    opponent_seat = "p2" if my_seat == "p1" else "p1"

    def make_game():
        return penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=opponent_seat, seed=seed)

    game = make_game()
    history = []
    chosen_types = []        # bucket of every chosen action (incl. forced)
    contested_types = []     # bucket at decisions with >1 legal action
    state_values = []        # net value of the pre-decision state (contested)
    attack_opp = attack_taken = 0
    block_opp = block_taken = 0
    start_value = None
    mid_feats = []           # mid-game feature rows for transform probes
    engine_error = False
    while game.result() is None and len(history) < MAX_DECISIONS:
        obs = json.loads(game.observe())
        if start_value is None:
            start_value = net.value(extractor.features(obs))
        actions = obs["legalActions"]
        type_of = {a["index"]: a["type"] for a in actions}
        contested = len(actions) > 1
        try:
            index, _, _ = choose(make_fork(make_game, history, game), obs,
                                 net, extractor, rng, epsilon=0.0,
                                 max_eval=max_eval, depth=len(history))
            game.act(index)
        except ValueError:
            engine_error = True  # upstream 0.3.0 fault; drop the game
            break
        history.append(index)
        b = bucket(type_of[index])
        chosen_types.append(b)
        if contested:
            contested_types.append(b)
            feats = extractor.features(obs)
            state_values.append(net.value(feats))
            if 4 <= obs.get("turn", 0) <= 15:
                mid_feats.append(feats)
            avail = {bucket(t) for t in type_of.values()}
            if "DeclareAttacker" in avail:
                attack_opp += 1
                attack_taken += b == "DeclareAttacker"
            if "DeclareBlocker" in avail:
                block_opp += 1
                block_taken += b == "DeclareBlocker"

    result = game.result()
    final = json.loads(game.observe(my_seat))
    me = 0 if my_seat == "p1" else 1
    won = result == my_seat
    if engine_error:
        reason = "engine-error"
    elif result is None:
        reason = "cap"
    elif won:
        reason = "won"
    elif result == "draw":
        reason = "draw"
    elif final["life"][me] <= 0:
        reason = "life"
    elif final["librarySizes"][me] == 0:
        reason = "deckout"
    else:
        reason = "other"
    return {
        "won": won,
        "reason": reason,
        "decisions": len(history),
        "my_life": final["life"][me],
        "opp_life": final["life"][1 - me],
        "chosen_types": chosen_types,
        "contested_types": contested_types,
        "state_values": np.asarray(state_values, dtype=np.float32),
        "start_value": start_value,
        "attack_opp": attack_opp, "attack_taken": attack_taken,
        "block_opp": block_opp, "block_taken": block_taken,
        "mid_feats": (np.stack(mid_feats) if mid_feats
                      else np.empty((0, extractor.size), dtype=np.float32)),
    }


_WORKER = {}


def _init(net_path):
    _WORKER["penta"] = import_penta()
    _WORKER["extractor"] = Extractor()
    _WORKER["net"] = Net.load(net_path)


def _play(task):
    my_seat, d1, d2, seed, max_eval = task
    rng = random.Random(seed)
    return play_probe_game(_WORKER["net"], _WORKER["extractor"],
                           _WORKER["penta"], my_seat, d1, d2, seed,
                           max_eval, rng)


def run_games(net_path, games, seed_base, max_eval, workers):
    tasks = []
    for g in range(games):
        my_seat = "p1" if g % 2 == 0 else "p2"
        d1, d2 = matchup(g // 2)
        tasks.append((my_seat, d1, d2, seed_base + g, max_eval))
    t0 = time.time()
    with Pool(workers, initializer=_init, initargs=(net_path,)) as pool:
        records = pool.map(_play, tasks)
    print(f"  {net_path}: {len(records)} games in {time.time() - t0:.0f}s")
    return [r for r in records if r["reason"] != "engine-error"]


# -- analysis ------------------------------------------------------------


def behavior_summary(records):
    n = len(records)
    contested = [t for r in records for t in r["contested_types"]]
    shares = {b: contested.count(b) / max(1, len(contested)) for b in BUCKETS}
    reasons = [r["reason"] for r in records]
    per_game = lambda b: sum(  # noqa: E731
        r["chosen_types"].count(b) for r in records) / n
    a_opp = sum(r["attack_opp"] for r in records)
    a_take = sum(r["attack_taken"] for r in records)
    b_opp = sum(r["block_opp"] for r in records)
    b_take = sum(r["block_taken"] for r in records)
    return {
        "games": n,
        "win%": 100.0 * sum(r["won"] for r in records) / n,
        "mean len": np.mean([r["decisions"] for r in records]),
        "my final life": np.mean([r["my_life"] for r in records]),
        "opp final life": np.mean([r["opp_life"] for r in records]),
        "loss: life": reasons.count("life"),
        "loss: deckout": reasons.count("deckout"),
        "loss: cap": reasons.count("cap"),
        "attacks/game": per_game("DeclareAttacker"),
        "blocks/game": per_game("DeclareBlocker"),
        "spells/game": per_game("CastSpell"),
        "lands/game": per_game("PlayLand"),
        "attack take-rate%": 100.0 * a_take / max(1, a_opp),
        "block take-rate%": 100.0 * b_take / max(1, b_opp),
        "contested decisions": len(contested),
        "shares": shares,
    }


def transform_probe(net, extractor, feats, rng):
    """Value deltas for feature-level transforms on sampled observations."""
    C = extractor.defs
    HAND_SIZE = 5 * C + 16  # own-hand-count scalar (see Extractor._scalars)
    base_v = net.value_batch(feats)

    # Discard-a-hand-card: pick a random held card's slot, -0.25 count,
    # hand-size scalar -1/7.
    deltas = []
    for row, v0 in zip(feats, base_v):
        slots = np.nonzero(row[:C] >= 0.25)[0]
        if not len(slots):
            continue
        x = row.copy()
        x[slots[rng.randrange(len(slots))]] -= 0.25
        x[HAND_SIZE] = max(0.0, x[HAND_SIZE] - 1.0 / 7.0)
        deltas.append(net.value(x) - v0)
    deltas = np.asarray(deltas)

    # Hand-size sweep: scalar alone, 0..7 cards, rest fixed.
    sweep = []
    for h in range(8):
        X = feats.copy()
        X[:, HAND_SIZE] = h / 7.0
        sweep.append(float(np.mean(net.value_batch(X))))
    return deltas, sweep


def calibration(records, bins=10):
    values, outcomes = [], []
    for r in records:
        v = r["state_values"]
        values.append(v)
        outcomes.append(np.full(len(v), 1.0 if r["won"] else 0.0))
    values = np.concatenate(values)
    outcomes = np.concatenate(outcomes)
    edges = np.linspace(0.0, 1.0, bins + 1)
    rows = []
    for i in range(bins):
        mask = (values >= edges[i]) & (values < edges[i + 1] if i < bins - 1
                                       else values <= edges[i + 1])
        n = int(mask.sum())
        rows.append((edges[i], edges[i + 1], n,
                     float(outcomes[mask].mean()) if n else float("nan")))
    won_mask = outcomes == 1.0
    return rows, {
        "mean v (all states)": float(values.mean()),
        "mean v (won games)": float(values[won_mask].mean())
        if won_mask.any() else float("nan"),
        "mean v (lost games)": float(values[~won_mask].mean())
        if (~won_mask).any() else float("nan"),
        "mean v @ game start": float(np.mean(
            [r["start_value"] for r in records])),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--net-a", required=True)
    parser.add_argument("--net-b", required=True)
    parser.add_argument("--label-a", default="A")
    parser.add_argument("--label-b", default="B")
    parser.add_argument("--games", type=int, default=100)
    parser.add_argument("--seed-base", type=int, default=7_000_000)
    parser.add_argument("--max-eval", type=int, default=16)
    parser.add_argument("--workers", type=int, default=6)
    parser.add_argument("--samples", type=int, default=300,
                        help="mid-game observations for transform probes")
    args = parser.parse_args()

    extractor = Extractor()
    print(f"probe: {args.games} fixed-seed games vs handcrafted per net, "
          f"seeds {args.seed_base}+, engine {extractor.engine_version}")
    sides = []
    for path, label in ((args.net_a, args.label_a),
                        (args.net_b, args.label_b)):
        records = run_games(path, args.games, args.seed_base, args.max_eval,
                            args.workers)
        summary = behavior_summary(records)
        cal_rows, cal_stats = calibration(records)
        # Fixed-seed subsample of mid-game states for the transform probes.
        feats = np.concatenate([r["mid_feats"] for r in records])
        rng = random.Random(13)
        if len(feats) > args.samples:
            feats = feats[rng.sample(range(len(feats)), args.samples)]
        net = Net.load(path)
        deltas, sweep = transform_probe(net, extractor, feats, rng)
        sides.append((label, summary, cal_rows, cal_stats, deltas, sweep,
                      len(feats)))

    (la, sa, ca, csa, da, swa, nsa), (lb, sb, cb, csb, db, swb, nsb) = sides
    W = 26

    print(f"\n== behavior vs handcrafted ({sa['games']}/{sb['games']} games)")
    print(f"{'metric':{W}} {la:>12} {lb:>12}")
    for key in ("win%", "mean len", "my final life", "opp final life",
                "loss: life", "loss: deckout", "loss: cap",
                "attacks/game", "blocks/game", "spells/game", "lands/game",
                "attack take-rate%", "block take-rate%",
                "contested decisions"):
        print(f"{key:{W}} {sa[key]:12.2f} {sb[key]:12.2f}")
    print(f"\n== contested-decision action-type shares (%)")
    print(f"{'type':{W}} {la:>12} {lb:>12}")
    for b in BUCKETS:
        print(f"{b:{W}} {100 * sa['shares'][b]:12.2f} "
              f"{100 * sb['shares'][b]:12.2f}")

    print(f"\n== value transforms ({nsa}/{nsb} mid-game samples)")
    print(f"{'probe':{W}} {la:>12} {lb:>12}")
    print(f"{'discard delta mean':{W}} {da.mean():+12.4f} {db.mean():+12.4f}")
    print(f"{'discard delta std':{W}} {da.std():12.4f} {db.std():12.4f}")
    print(f"{'discard delta <0 (%)':{W}} "
          f"{100 * float(np.mean(da < 0)):12.1f} "
          f"{100 * float(np.mean(db < 0)):12.1f}")
    print("hand-size sweep, mean value at h cards in hand:")
    print(f"{'h':>4} {la:>12} {lb:>12}")
    for h in range(8):
        print(f"{h:4d} {swa[h]:12.4f} {swb[h]:12.4f}")
    print(f"{'slope (v7 - v0)':{W}} {swa[7] - swa[0]:+12.4f} "
          f"{swb[7] - swb[0]:+12.4f}")

    print(f"\n== prediction sanity")
    print(f"{'stat':{W}} {la:>12} {lb:>12}")
    for key in ("mean v @ game start", "mean v (all states)",
                "mean v (won games)", "mean v (lost games)"):
        print(f"{key:{W}} {csa[key]:12.4f} {csb[key]:12.4f}")
    print("calibration (10 bins of predicted v -> realized win rate):")
    print(f"{'bin':>12} {la + ' n':>10} {la + ' win':>9} "
          f"{lb + ' n':>10} {lb + ' win':>9}")
    for (lo, hi, na, wa), (_, _, nb, wb) in zip(ca, cb):
        print(f"[{lo:.1f},{hi:.1f}) {na:10d} {wa:9.3f} {nb:10d} {wb:9.3f}")


if __name__ == "__main__":
    main()
