#!/usr/bin/env python3
"""Live telemetry for the BELIEF curve -> web/penta-telemetry.json every 15s.

Parses:
  - chunk-belief-N.log training lines: "games M ... loss L ... R games/s"
    (M is chunk-local; cumulative = (N-1)*CHUNK + M).
  - progress.txt gate lines under the BELIEF header:
      "... cumulative-games=N | cheap K2/60: ... -> X%"
      "      honest K4/120: ... -> Y%"   (optional, attaches to the last N)
Emits the schema web/penta-monitor.html polls: gates[{games,random,handcrafted}]
(random := cheap K2 gate, handcrafted := honest K4 gate, else the cheap value)
and train[{games,loss,rate}]. Baseline to beat = 31.6%.
"""
import glob
import json
import os
import re
import time
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "web", "penta-telemetry.json")
CHUNK = 1500  # GAMES per belief chunk (curve-belief.sh)

TRAIN = re.compile(r"^games\s+(\d+)\s+eps\s+[\d.]+\s+loss\s+([\d.]+).*?([\d.]+)\s+games/s")
CHEAP = re.compile(r"cumulative-games=(\d+).*?cheap K2/60:.*?->\s+([\d.]+)%")
HONEST = re.compile(r"honest K4/120:.*?->\s+([\d.]+)%")


def build():
    train = []
    for path in sorted(glob.glob(os.path.join(HERE, "chunk-inert-*.log")),
                       key=lambda p: int(re.search(r"chunk-inert-(\d+)", p).group(1))):
        n = int(re.search(r"chunk-inert-(\d+)", path).group(1))
        base = (n - 1) * CHUNK
        for line in open(path, errors="replace"):
            m = TRAIN.search(line)
            if m:
                train.append({"games": base + int(m.group(1)),
                              "loss": float(m.group(2)),
                              "rate": float(m.group(3))})
    gates = []
    progress = os.path.join(HERE, "progress.txt")
    seen_belief_header = False
    if os.path.exists(progress):
        for line in open(progress, errors="replace"):
            if "INERT curve" in line:
                seen_belief_header = True
                continue
            if not seen_belief_header:
                continue
            c = CHEAP.search(line)
            if c:
                gates.append({"games": int(c.group(1)),
                              "random": float(c.group(2)),
                              "handcrafted": float(c.group(2))})  # default = cheap
                continue
            h = HONEST.search(line)
            if h and gates:
                gates[-1]["handcrafted"] = float(h.group(1))  # honest overrides
    return {"updated": datetime.now().strftime("%H:%M:%S"),
            "baseline": 31.6, "gates": gates, "train": train}


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    while True:
        try:
            with open(OUT, "w") as f:
                json.dump(build(), f)
        except Exception as e:  # noqa: BLE001
            print("telemetry error:", e, flush=True)
        time.sleep(15)


if __name__ == "__main__":
    main()
