#!/usr/bin/env python3
"""Group-sequential (Wald SPRT) screening gate for paired benchmarks.

Runs the paired --benchmark harness in small deterministic shards (each
shard is an independent, bit-reproducible simulator invocation with a
seed derived from the base seed) and applies a preregistered Wald
sequential probability ratio test between shards:

    H0: challenger outright-win probability p = p0 (default 0.50)
    H1: p = p1 (default 0.53, the program's declared 3pp effect)

Draws count as non-wins, matching the certification gate. Evidence is
consumed in a fixed shard order, so the stopping decision is a pure
function of (base seed, shard plan) and is exactly reproducible.

This is a SCREEN, not a certification: PASS-SCREEN promotes a candidate
to the full fixed-N certify.sh gate; REJECT-SCREEN stops spending games
on a hopeless candidate early. Checking the boundary only at shard
boundaries makes the test strictly more conservative than the
per-observation SPRT, so the declared error rates still hold.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
import time
from pathlib import Path

DECKS = ("Green", "Red", "Blue", "White", "RU Aggro")
GAMES_PER_REPETITION = 60  # 15 unordered pairings x 2 assignments x 2 starts


def splitmix64(state: int) -> int:
    state = (state + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
    return z ^ (z >> 31)


def shard_seed(base_seed: int, index: int) -> int:
    seed = base_seed & 0xFFFFFFFFFFFFFFFF
    for _ in range(index + 1):
        seed = splitmix64(seed)
    return seed


def wilson95(wins: int, games: int) -> tuple[float, float]:
    z = 1.959963984540054
    proportion = wins / games
    denominator = 1.0 + z * z / games
    center = (proportion + z * z / (2.0 * games)) / denominator
    margin = (
        z
        * math.sqrt(
            proportion * (1.0 - proportion) / games
            + z * z / (4.0 * games * games)
        )
        / denominator
    )
    return center - margin, center + margin


RECORD_RE = re.compile(
    r"^  Challenger record: (\d+)-(\d+)-(\d+) \([0-9.]+% wins\)$",
    re.MULTILINE,
)
CELL_RE = re.compile(
    r"^  (Green|Red|Blue|White|RU Aggro) vs "
    r"(Green|Red|Blue|White|RU Aggro): "
    r"(\d+)-(\d+)-(\d+) \((\d+) games\)$",
    re.MULTILINE,
)


def run_shard(command: list[str]) -> tuple[tuple[int, int, int], dict]:
    process = subprocess.run(
        command, capture_output=True, text=True, check=False
    )
    if process.returncode not in (0, 1):
        raise RuntimeError(
            f"shard exited {process.returncode}:\n{process.stdout}"
            f"\n{process.stderr}"
        )
    text = process.stdout
    record_match = RECORD_RE.search(text)
    if record_match is None:
        raise RuntimeError(f"shard log had no challenger record:\n{text}")
    record = tuple(int(record_match.group(i)) for i in (1, 2, 3))
    cells: dict = {}
    for match in CELL_RE.finditer(text):
        key = (match.group(1), match.group(2))
        cells[key] = tuple(int(match.group(i)) for i in (3, 4, 5))
    return record, cells


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", default="build/old-school-sim")
    parser.add_argument("--base-seed", type=int, required=True)
    parser.add_argument(
        "--shard-reps",
        type=int,
        default=2,
        help="repetitions per unordered pairing per shard (2 = 120 games)",
    )
    parser.add_argument(
        "--max-shards",
        type=int,
        default=17,
        help="cap; 17 shards x 2 reps = 2040 games, the fixed-N budget",
    )
    parser.add_argument("--challenger", default="learned-value-c16")
    parser.add_argument("--baseline", default="handcrafted")
    parser.add_argument("--learned-rollouts", type=int, default=8)
    parser.add_argument("--train-games", type=int, default=800)
    parser.add_argument("--train-seed", type=int, default=424242)
    parser.add_argument("--p0", type=float, default=0.50)
    parser.add_argument("--p1", type=float, default=0.53)
    parser.add_argument("--alpha", type=float, default=0.05)
    parser.add_argument("--beta", type=float, default=0.20)
    parser.add_argument("--json", type=Path, default=None)
    args = parser.parse_args(argv)

    upper = math.log((1.0 - args.beta) / args.alpha)
    lower = math.log(args.beta / (1.0 - args.alpha))
    win_step = math.log(args.p1 / args.p0)
    loss_step = math.log((1.0 - args.p1) / (1.0 - args.p0))
    shard_games = args.shard_reps * GAMES_PER_REPETITION
    fixed_budget = args.max_shards * shard_games

    print(
        f"fastgate: SPRT H0 p={args.p0} vs H1 p={args.p1}, "
        f"alpha={args.alpha}, beta={args.beta}"
    )
    print(
        f"  boundaries: reject-H0 at LLR>={upper:.4f}, "
        f"accept-H0 (futility) at LLR<={lower:.4f}"
    )
    print(
        f"  plan: up to {args.max_shards} shards x {shard_games} games "
        f"= {fixed_budget} games; base seed {args.base_seed}"
    )

    llr = 0.0
    wins = losses = draws = 0
    pooled_cells: dict = {}
    shards = []
    verdict = "INCONCLUSIVE"
    started = time.monotonic()
    for index in range(args.max_shards):
        seed = shard_seed(args.base_seed, index)
        command = [
            str(args.simulator),
            "--benchmark",
            "--games",
            str(args.shard_reps),
            "--seed",
            str(seed),
            "--train-games",
            str(args.train_games),
            "--train-seed",
            str(args.train_seed),
            "--challenger",
            args.challenger,
            "--baseline",
            args.baseline,
            "--learned-rollouts",
            str(args.learned_rollouts),
        ]
        (shard_wins, shard_losses, shard_draws), cells = run_shard(command)
        wins += shard_wins
        losses += shard_losses
        draws += shard_draws
        for key, (cell_wins, cell_losses, cell_draws) in cells.items():
            pooled = pooled_cells.get(key, (0, 0, 0))
            pooled_cells[key] = (
                pooled[0] + cell_wins,
                pooled[1] + cell_losses,
                pooled[2] + cell_draws,
            )
        llr += shard_wins * win_step + (
            shard_losses + shard_draws
        ) * loss_step
        games = wins + losses + draws
        shards.append(
            {
                "index": index,
                "seed": seed,
                "record": [shard_wins, shard_losses, shard_draws],
                "cumulative_llr": llr,
                "cumulative_win_rate": wins / games,
            }
        )
        print(
            f"  shard {index:2d} seed {seed:>20d}: "
            f"{shard_wins}-{shard_losses}-{shard_draws}  "
            f"pooled {wins}-{losses}-{draws} "
            f"({100.0 * wins / games:.1f}%)  LLR {llr:+.3f}"
        )
        if llr >= upper:
            verdict = "PASS-SCREEN"
            break
        if llr <= lower:
            verdict = "REJECT-SCREEN"
            break
    elapsed = time.monotonic() - started

    games = wins + losses + draws
    interval = wilson95(wins, games)
    print(f"verdict: {verdict}")
    print(
        f"  pooled record {wins}-{losses}-{draws} "
        f"({100.0 * wins / games:.2f}%), Wilson 95% "
        f"[{100.0 * interval[0]:.2f}%, {100.0 * interval[1]:.2f}%]"
    )
    print(
        f"  games used: {games} of fixed-N {fixed_budget} "
        f"({100.0 * games / fixed_budget:.0f}%); wall {elapsed:.1f}s"
    )
    if verdict == "PASS-SCREEN":
        print("  next: promote to the full fixed-N certify.sh gate")

    if args.json is not None:
        args.json.write_text(
            json.dumps(
                {
                    "verdict": verdict,
                    "sprt": {
                        "p0": args.p0,
                        "p1": args.p1,
                        "alpha": args.alpha,
                        "beta": args.beta,
                        "upper": upper,
                        "lower": lower,
                    },
                    "base_seed": args.base_seed,
                    "shard_reps": args.shard_reps,
                    "max_shards": args.max_shards,
                    "record": {
                        "wins": wins,
                        "losses": losses,
                        "draws": draws,
                        "games": games,
                    },
                    "wilson95": {"low": interval[0], "high": interval[1]},
                    "games_used": games,
                    "fixed_budget": fixed_budget,
                    "wall_seconds": elapsed,
                    "shards": shards,
                    "pooled_cells": {
                        f"{challenger} vs {baseline}": list(record)
                        for (
                            challenger,
                            baseline,
                        ), record in sorted(pooled_cells.items())
                    },
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        print(f"  json: {args.json}")
    if verdict == "PASS-SCREEN":
        return 0
    if verdict == "REJECT-SCREEN":
        return 1
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
