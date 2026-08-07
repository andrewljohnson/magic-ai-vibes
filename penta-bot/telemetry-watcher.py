#!/usr/bin/env python3
"""Folds penta-bot training logs into one JSON for the dashboard.

Parses chunk-N.log training lines and progress.txt gate checkpoints
into build/telemetry/penta-telemetry.json every 20 seconds, so
penta-monitor.html can poll a single file. Run detached from the repo
root; exits on its own when no trainer has written for 30 minutes.
"""
import glob
import json
import os
import re
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "..", "build", "telemetry",
                   "penta-telemetry.json")
SMOKE_GAMES = 3000
CHUNK_GAMES = 2000

TRAIN_LINE = re.compile(
    r"games\s+(\d+)\s+eps\s+([\d.]+)\s+loss\s+([\d.]+).*?"
    r"([\d.]+) games/s")
GATE_LINE = re.compile(
    r"cumulative-games=(\d+).*?random.*?->\s+([\d.]+)%.*?"
    r"handcrafted.*?->\s+([\d.]+)%")


def snapshot():
    train = []
    for path in sorted(glob.glob(os.path.join(ROOT, "chunk-*.log"))):
        chunk = int(re.search(r"chunk-(\d+)", path).group(1))
        base = SMOKE_GAMES + (chunk - 1) * CHUNK_GAMES
        for line in open(path, errors="replace"):
            match = TRAIN_LINE.search(line)
            if match:
                train.append({
                    "games": base + int(match.group(1)),
                    "epsilon": float(match.group(2)),
                    "loss": float(match.group(3)),
                    "rate": float(match.group(4)),
                })
    gates = [{"games": SMOKE_GAMES, "random": 86.0, "handcrafted": 16.5,
              "note": "smoke"}]
    progress = os.path.join(ROOT, "progress.txt")
    if os.path.exists(progress):
        for line in open(progress, errors="replace"):
            match = GATE_LINE.search(line)
            if match:
                gates.append({
                    "games": int(match.group(1)),
                    "random": float(match.group(2)),
                    "handcrafted": float(match.group(3)),
                })
    return {"updated": time.strftime("%H:%M:%S"), "train": train,
            "gates": gates}


def main():
    quiet_since = time.time()
    while True:
        data = snapshot()
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        with open(OUT + ".tmp", "w") as out:
            json.dump(data, out)
        os.replace(OUT + ".tmp", OUT)
        newest = max((os.path.getmtime(p) for p in
                      glob.glob(os.path.join(ROOT, "chunk-*.log"))),
                     default=0)
        if newest > 0:
            quiet_since = max(quiet_since, newest)
        if time.time() - quiet_since > 1800:
            return
        time.sleep(20)


if __name__ == "__main__":
    main()
