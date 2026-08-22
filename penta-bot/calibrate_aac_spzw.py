#!/usr/bin/env python3
"""Export an AAC actor to .spzw with its output CALIBRATED to a win
probability, so it can serve as an ISMCTS leaf evaluator.

WHY THIS EXISTS
---------------
The AAC actor is trained as a RELATIVE scorer: the loss only ever sees
softmax(logit/temp) over one decision's afterstates, so nothing anchors
the logit's absolute scale. Measured on the 49% actor, logits span
-142..+196 with mean |logit| 41.7. net.rs's Mlp::value applies a sigmoid,
and at that scale the sigmoid is fully saturated:

    95.2% of afterstates land >0.99 or <0.01
    79.5% of DECISIONS have every candidate within 1e-6 of every other

An MCTS leaf evaluator that returns the same number for every legal move
cannot search. Observed: ISMCTS at iters=16 scored 6.2% (1W/15L) with
half its games hitting the decision cap, versus 49% for the same actor
playing 1-ply argmax. The ordering was intact -- the SCALE was not.

THE FIX
-------
Platt scaling: fit P(win) = sigmoid(a * logit + b) against real outcomes,
then fold (a, b) into the exported weights. Because

    value = sigmoid(w2 . tanh(w1 x + b1) + b2)

scaling w2 and b2 by `a` and adding `b` to b2 yields sigmoid(a*logit + b)
exactly, so the calibrated net drops into the existing .spzw reader with
no Rust change.

Pairs are (logit of the afterstate actually chosen, that seat's game
result), harvested from native self-play -- i.e. "given this afterstate
score, how often did this seat go on to win?", which is the question an
MCTS leaf value is supposed to answer.

Usage:
  PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python calibrate_aac_spzw.py \
      aac_belief_native_best.npz out.spzw --hidden 256 --belief --games 64
"""
import argparse
import os
import sys

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import aac_torch as AT  # noqa: E402
import aac_native as AN  # noqa: E402
from extractor import Extractor  # noqa: E402
from hosted_policy import load_decklists  # noqa: E402


def harvest(actor, hidden, belief, learner_deck, games, seed_base, threads):
    """(logit, won) pairs for the afterstates actually played."""
    spz = AN.load_spz_core()
    decks = list(load_decklists())
    opps = [d for d in decks if d != learner_deck]
    specs = []
    for g in range(games):
        p1 = g % 2 == 0
        od = opps[g % len(opps)]
        d1, d2 = ((learner_deck, od) if p1 else (od, learner_deck))
        # handcrafted opponent: the distribution the gate actually faces
        specs.append((d1, d2, seed_base + g, True, p1))
    episodes, stats = AN.stream_episodes(spz, actor, hidden, belief, specs,
                                         1.0, threads=threads)
    logits = stats["logits"]
    x, y = [], []
    off = 0
    for records, result in episodes:
        for r in records:
            m = r["cand"].shape[0]
            chosen_logit = float(logits[off + r["chosen"]])
            off += m
            if result is None:          # capped game: no outcome to learn from
                continue
            z = 0.5 if result == "draw" else (1.0 if result == r["seat"] else 0.0)
            x.append(chosen_logit)
            y.append(z)
    return np.asarray(x), np.asarray(y)


def fit_platt(x, y, iters=400, lr=0.05):
    """Logistic regression of `y` on `x` -> (a, b). Draws (y=0.5) are kept
    as soft targets, which plain sklearn logistic regression would not
    accept, so this is a small explicit fit."""
    xs = (x - x.mean()) / (x.std() + 1e-9)   # fit standardized, unscale after
    a = torch.zeros(1, requires_grad=True)
    b = torch.zeros(1, requires_grad=True)
    tx = torch.as_tensor(xs, dtype=torch.float32)
    ty = torch.as_tensor(y, dtype=torch.float32)
    opt = torch.optim.Adam([a, b], lr=lr)
    for _ in range(iters):
        loss = torch.nn.functional.binary_cross_entropy_with_logits(
            a * tx + b, ty)
        opt.zero_grad()
        loss.backward()
        opt.step()
    a_s, b_s = float(a.detach()), float(b.detach())
    # undo the standardization so (a, b) apply to the RAW logit
    a_raw = a_s / (x.std() + 1e-9)
    b_raw = b_s - a_s * x.mean() / (x.std() + 1e-9)
    return a_raw, b_raw, float(loss.detach())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("actor")
    ap.add_argument("out")
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--belief", action="store_true")
    ap.add_argument("--learner-deck", default="Sligh")
    ap.add_argument("--games", type=int, default=64)
    ap.add_argument("--seed-base", type=int, default=7_000_000)
    ap.add_argument("--threads", type=int, default=8)
    args = ap.parse_args()

    ex = Extractor(version=2, belief=args.belief)
    actor = AT.MLP(ex.size, args.hidden)
    d = np.load(args.actor)
    actor.load_state_dict({k: torch.as_tensor(d[k]) for k in d.files})

    x, y = harvest(actor, args.hidden, args.belief, args.learner_deck,
                   args.games, args.seed_base, args.threads)
    print(f"harvested {len(x)} (logit, outcome) pairs from {args.games} games")
    print(f"  raw logit: min={x.min():.1f} med={np.median(x):.1f} "
          f"max={x.max():.1f} |mean|={np.abs(x).mean():.1f}")
    print(f"  base win rate on these pairs: {y.mean():.3f}")

    a, b, loss = fit_platt(x, y)
    print(f"Platt fit: a={a:.6g} b={b:.6g}  (final BCE {loss:.4f})")

    cal = a * x + b
    p = 1.0 / (1.0 + np.exp(-cal))
    sat = float(np.mean((p > 0.99) | (p < 0.01)))
    print(f"  calibrated p: min={p.min():.3f} med={np.median(p):.3f} "
          f"max={p.max():.3f} | saturated {100*sat:.1f}% (was 95.2%)")

    w1 = np.asarray(d["f1.weight"], dtype="<f8")
    b1 = np.asarray(d["f1.bias"], dtype="<f8")
    w2 = np.asarray(d["f2.weight"], dtype="<f8").reshape(-1) * a
    b2 = float(np.asarray(d["f2.bias"]).reshape(-1)[0]) * a + b
    hidden, inputs = w1.shape
    with open(args.out, "wb") as f:
        f.write(np.array([hidden, inputs], dtype="<u8").tobytes())
        f.write(np.ascontiguousarray(w1).tobytes())
        f.write(np.ascontiguousarray(b1).tobytes())
        f.write(np.ascontiguousarray(w2).tobytes())
        f.write(np.array([b2], dtype="<f8").tobytes())
    print(f"wrote calibrated {hidden}x{inputs} -> {args.out}")


if __name__ == "__main__":
    main()
