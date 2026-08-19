#!/usr/bin/env python3
"""NATIVE SO-ISMCTS gate: one PyO3 call plays the whole batch of games vs
the engine's handcrafted bot ENTIRELY in Rust (spz_core.ismcts_gate) --
no per-move Python/JSON crossing, multithreaded across games. Replaces the
Python per-move gate loop (gate_hosted / ismcts_gate_stream), which crossed
the FFI boundary once per move (~115s/game, 0.009 g/s).

Usage:
  PENTA_ENGINE_DIR=engine-0.7.0 python3 ismcts_gate_native.py \
      --iters 64 --games 48 --redeterminize-m 1 --workers 8
"""
import argparse
import math
import os
import tempfile
import time

import numpy as np

import spz_core
from extractor import pinned_catalog
from net import Net

DECKS = ("Sligh", "White Weenie", "The Deck", "Counterburn",
         "Goblins", "Erhnamgeddon", "Mono Black", "Jeskai Aggro")


def export_spzw(net, path):
    hidden, inputs = net.w1.shape
    with open(path, "wb") as f:
        f.write(np.array([hidden, inputs], dtype="<u8").tobytes())
        f.write(np.ascontiguousarray(net.w1, dtype="<f8").tobytes())
        f.write(np.ascontiguousarray(net.b1, dtype="<f8").tobytes())
        f.write(np.ascontiguousarray(net.w2, dtype="<f8").tobytes())
        f.write(np.array([net.b2], dtype="<f8").tobytes())


def wilson_lcb(wins, games, z=1.96):
    if games == 0:
        return 0.0
    p = wins / games
    denom = 1 + z * z / games
    centre = p + z * z / (2 * games)
    margin = z * math.sqrt(p * (1 - p) / games + z * z / (4 * games * games))
    return (centre - margin) / denom


def build_specs(games, seed_base):
    pairs = [(a, b) for a in DECKS for b in DECKS if a != b]
    specs = []
    for g in range(games):
        our_p1 = (g % 2 == 0)
        d1, d2 = pairs[(g // 2) % len(pairs)]
        specs.append((d1, d2, our_p1, seed_base + g))
    return specs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=64)
    ap.add_argument("--c-puct", type=float, default=1.5)
    ap.add_argument("--budget", type=int, default=400)
    ap.add_argument("--games", type=int, default=48)
    ap.add_argument("--workers", type=int, default=0, help="0 = all cores")
    ap.add_argument("--redeterminize-m", type=int, default=1)
    ap.add_argument("--value-net", default="penta_net.npz")
    ap.add_argument("--head", default="policy_head.ckpt-dagger1.npz")
    ap.add_argument("--weight", type=float, default=0.15)
    ap.add_argument("--inert", action="store_true")
    ap.add_argument("--seed-base", type=int, default=5_000_000)
    ap.add_argument("--decklists", default="builtin-decklists.json")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    catalog_json = __import__("json").dumps(pinned_catalog())
    tmp = tempfile.gettempdir()
    tag = str(os.getpid())
    value_spzw = os.path.join(tmp, f"gate_value.{tag}.spzw")
    head_spzw = os.path.join(tmp, f"gate_head.{tag}.spzw")
    export_spzw(Net.load(os.path.join(here, args.value_net)), value_spzw)
    export_spzw(Net.load(os.path.join(here, args.head)), head_spzw)
    decklists_path = (args.decklists if os.path.isabs(args.decklists)
                      else os.path.join(here, args.decklists))

    specs = build_specs(args.games, args.seed_base)
    print(f"NATIVE ISMCTS gate: iters={args.iters} M={args.redeterminize_m} "
          f"games={args.games} workers={args.workers or 'all'} "
          f"net={args.value_net}", flush=True)
    t0 = time.time()
    wins, draws, finished = spz_core.ismcts_gate(
        catalog_json, value_spzw, head_spzw, args.weight, int(args.iters),
        float(args.c_puct), int(args.budget), bool(args.inert),
        int(args.redeterminize_m), decklists_path, specs, int(args.workers))
    el = time.time() - t0
    losses = finished - wins - draws
    rate = (wins + 0.5 * draws) / finished if finished else 0.0
    print(f"FINAL native iters={args.iters} M={args.redeterminize_m}: "
          f"{wins}W {draws}D {losses}L / {finished} finished "
          f"({args.games} played) -> {100*wins/max(finished,1):.1f}% wins, "
          f"{100*rate:.1f}% w/ draws (LCB {100*wilson_lcb(wins, finished):.1f}%) "
          f"[{el:.1f}s, {args.games/el:.3f} g/s]", flush=True)


if __name__ == "__main__":
    main()
