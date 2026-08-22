#!/usr/bin/env python3
"""LOCKSTEP GATE for the native AAC self-play loop.

Before a native-generated batch is allowed to train anything, it must be
the SAME batch the Python path would have produced. This replays each
native episode's chosen actions through the original Python code --
`afterstate_rows` + `Extractor` + `belief_deck_context` + `_priv_features`
+ the tempo shaping in `_worker_episode` -- and compares every emitted
quantity.

What is held to BIT-EQUALITY (these are pure functions of the game state,
so any difference is a real port bug):
  * candidate afterstate features   (m x feat float32, per decision)
  * privileged critic input         (2*feat float32, per decision)
  * shaped tempo reward             (float64, per decision)
  * the forced/recorded decision structure and the game result

What is held to a TOLERANCE, and why: the actor logits. numpy's matmul
hands the dot product to BLAS, which blocks and reorders the summation;
the native scorer accumulates straight through in f64. Identical inputs,
identical weights, different summation ORDER -- so the last ulp or two can
differ. The check that matters operationally is that the ordering the
logits induce is the same, so this also reports how often the two
disagree about the ARGMAX (which is what the gate plays).

Run with max_actions=0 (the default here): the cap must be OFF for this
comparison, since a capped decision deliberately emits no row.

  PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python aac_lockstep.py \
      --episodes 4 --actor aac_par_belief_best.npz --hidden 256 --belief
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
from trainer import afterstate_rows, make_fork  # noqa: E402
from aac_selfplay import (  # noqa: E402
    _priv_features, own_board_power, K_IDLE, K_LIFE, K_POWER, MAX_DECISIONS)
from aac_torch_par import _np_logits, actor_weights  # noqa: E402
import aac_torch as AT  # noqa: E402
import aac_native as AN  # noqa: E402


def python_episode(weights, ex, penta, decks, card_power, d1, d2, seed,
                   temperature, handcrafted, learner_seat, forced):
    """`_worker_episode` with the sampling replaced by a fixed action
    script, so the Python and native games walk identical states and every
    emitted row is directly comparable. `forced` is the native run's
    chosen index per RECORDED decision, in order."""
    w1, b1, w2, b2 = weights
    if handcrafted:
        opp = "p2" if learner_seat == "p1" else "p1"
        game = penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=opp, seed=seed)
    else:
        game = penta.Game(d1, d2, opponent="external", seed=seed)
    records = []
    prev_ol = {"p1": None, "p2": None}
    prev_op = {"p1": None, "p2": None}
    n = 0
    while game.result() is None and n < MAX_DECISIONS:
        seat = game.decision_seat()
        record_this = (not handcrafted) or (seat == learner_seat)
        obs = json.loads(game.observe())
        acts = obs["legalActions"]
        if not record_this or len(acts) == 1:
            game.act(acts[0]["index"])
            n += 1
            continue
        if len(records) >= len(forced):
            break            # native stopped here; nothing left to compare
        if ex.belief:
            belief_deck_context(ex, decks, obs, d1 if seat == "p1" else d2)
        priv = _priv_features(game, seat, ex)
        fork = make_fork(None, None, game=game)
        cand, _t = afterstate_rows(fork, seat, acts, ex)
        cand = np.asarray(cand, dtype=np.float32)
        logits = _np_logits(cand, w1, b1, w2, b2)
        z = logits / max(temperature, 1e-6)
        z = z - z.max()
        pi = np.exp(z)
        pi = pi / pi.sum()
        c = forced[len(records)]
        logp_old = float(np.log(pi[c] + 1e-12))
        idx = acts[c]["index"]
        chosen_action = next(a for a in acts if a["index"] == idx)
        idle = 1.0 if (chosen_action.get("type") in ("Pass", "PassPriority")
                       and obs.get("step") == "PrecombatMain"
                       and obs.get("activeSeat") == seat) else 0.0
        game.act(idx)
        n += 1
        after = json.loads(game.observe(seat))
        ol = after["life"][1 - (0 if seat == "p1" else 1)]
        op = own_board_power(after, card_power)
        r = -K_IDLE * idle
        if prev_ol[seat] is not None:
            r += K_LIFE * max(0, prev_ol[seat] - ol)
            r += K_POWER * max(0, op - prev_op[seat])
        prev_ol[seat] = ol
        prev_op[seat] = op
        records.append({"cand": cand, "chosen": c, "logp_old": logp_old,
                        "seat": seat, "priv": priv, "r": r,
                        "logits": logits})
    return records, game.result()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--episodes", type=int, default=4)
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--belief", action="store_true")
    ap.add_argument("--actor", default=None,
                    help="npz actor to test with; default = random init "
                         "(a trained actor exercises real board states)")
    ap.add_argument("--learner-deck", default="Sligh")
    ap.add_argument("--seed", type=int, default=4242)
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--logit-tol", type=float, default=1e-9,
                    help="max allowed |native - numpy| actor logit gap")
    args = ap.parse_args()

    penta = import_penta()
    ex = Extractor(version=2, belief=args.belief)
    decks = load_decklists()
    opps = [d for d in decks if d != args.learner_deck]
    spz = AN.load_spz_core()

    actor = AT.MLP(ex.size, args.hidden)
    if args.actor:
        d = np.load(args.actor)
        actor.load_state_dict({k: torch.as_tensor(d[k]) for k in d.files})
    weights = actor_weights(actor)

    specs = []
    for i in range(args.episodes):
        learner_p1 = i % 2 == 0
        opp = opps[i % len(opps)]
        d1, d2 = ((args.learner_deck, opp) if learner_p1
                  else (opp, args.learner_deck))
        specs.append((d1, d2, args.seed + i, i % 2 == 0, learner_p1))

    print(f"native: {args.episodes} episodes, belief={args.belief}, "
          f"feat={ex.size}, hidden={args.hidden}, "
          f"actor={args.actor or 'random-init'}")
    # max_actions=0: the cap must be OFF, since a capped decision emits no
    # row by design and would show up here as a structure mismatch.
    episodes, stats = AN.stream_episodes(spz, actor, args.hidden,
                                         args.belief, specs, 1.0,
                                         threads=args.threads,
                                         max_actions=0)

    n_dec = n_cand = 0
    bad_cand = bad_priv = bad_r = bad_shape = 0
    bad_result = 0
    max_logit_gap = 0.0
    max_logp_gap = 0.0
    argmax_disagree = 0
    all_logits = stats["logits"]      # flat, in record order across episodes
    off = 0                           # running offset into that buffer
    for (nat_recs, nat_res, _fin), (d1, d2, seed, hc, lp1) in zip(episodes, specs):
        forced = [r["chosen"] for r in nat_recs]
        py_recs, py_res = python_episode(
            weights, ex, penta, decks, ex.card_power, d1, d2, seed, 1.0,
            hc, "p1" if lp1 else "p2", forced)
        if len(py_recs) != len(nat_recs):
            bad_shape += 1
            print(f"  ! episode seed={seed}: {len(nat_recs)} native records "
                  f"vs {len(py_recs)} python")
        if py_res != nat_res:
            bad_result += 1
            print(f"  ! episode seed={seed}: result {nat_res!r} native "
                  f"vs {py_res!r} python")
        for i, nr in enumerate(nat_recs):
            m = nr["cand"].shape[0]
            nat_logits = all_logits[off:off + m]
            off += m               # advance for EVERY native record, so the
            if i >= len(py_recs):  # buffer stays aligned past a short replay
                continue
            pr = py_recs[i]
            n_dec += 1
            n_cand += m
            if nr["cand"].shape != pr["cand"].shape:
                bad_shape += 1
                continue
            if not np.array_equal(nr["cand"], pr["cand"]):
                bad_cand += 1
            if not np.array_equal(nr["priv"].astype(np.float64), pr["priv"]):
                bad_priv += 1
            if nr["r"] != pr["r"]:
                bad_r += 1
            max_logit_gap = max(max_logit_gap,
                                float(np.max(np.abs(nat_logits
                                                    - pr["logits"]))))
            max_logp_gap = max(max_logp_gap,
                               abs(nr["logp_old"] - pr["logp_old"]))
            if int(np.argmax(nat_logits)) != int(np.argmax(pr["logits"])):
                argmax_disagree += 1

    print(f"\ndecisions compared: {n_dec}  candidates: {n_cand}")
    print(f"  candidate features bit-equal : "
          f"{'PASS' if bad_cand == 0 else f'FAIL ({bad_cand} decisions)'}")
    print(f"  privileged rows bit-equal    : "
          f"{'PASS' if bad_priv == 0 else f'FAIL ({bad_priv} decisions)'}")
    print(f"  shaped rewards bit-equal     : "
          f"{'PASS' if bad_r == 0 else f'FAIL ({bad_r} decisions)'}")
    print(f"  record/result structure      : "
          f"{'PASS' if bad_shape == 0 and bad_result == 0 else 'FAIL'}")
    print(f"  max |logit| gap vs numpy     : {max_logit_gap:.3e} "
          f"(tol {args.logit_tol:.0e}) "
          f"{'PASS' if max_logit_gap <= args.logit_tol else 'FAIL'}")
    print(f"  max |logp_old| gap vs numpy  : {max_logp_gap:.3e}")
    print(f"  argmax disagreements         : {argmax_disagree}/{n_dec} "
          f"(near-exact ties; f64 vs BLAS summation order)")

    ok = (bad_cand == 0 and bad_priv == 0 and bad_r == 0 and bad_shape == 0
          and bad_result == 0 and max_logit_gap <= args.logit_tol)
    print("\nROW LOCKSTEP: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
