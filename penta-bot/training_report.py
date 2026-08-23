#!/usr/bin/env python3
"""Terminal training report: one table, one chart, no dependencies.

    python3 training_report.py            # live runs
    python3 training_report.py --all      # include stopped/finished
    python3 training_report.py --top 6    # limit the chart

Reuses monitor.py's log parsing so the terminal view and the web view can
never disagree about what a run scored.

Reading it: evaluations are 400 games, so the standard error on a single
one is +-2.5 points. The column that means something is MEAN (with its
standard error); LAST8 shows where a run is heading; BEST is mostly a
max-of-noise statistic and is shown only because people ask for it.
ENT is policy entropy -- it flattens BEFORE the score curve does, so a run
drifting toward ~0.15 with a flat curve has stopped exploring.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import monitor  # noqa: E402

SPARK = "▁▂▃▄▅▆▇█"
BASELINE = 50.0   # parity with the built-in bot; see RESULTS.md
TARGET = 50.0


def sparkline(vals, lo, hi):
    if not vals:
        return ""
    span = max(hi - lo, 1e-9)
    out = []
    for v in vals:
        i = int((v - lo) / span * (len(SPARK) - 1) + 0.5)
        out.append(SPARK[min(max(i, 0), len(SPARK) - 1)])
    return "".join(out)


def plot(runs, width=64, height=16):
    """A plain ASCII scatter of every run's curve, games on x, % on y."""
    pts = [(g["games"], g["pct"], i)
           for i, r in enumerate(runs) for g in r["gates"] if g["games"] > 0]
    if not pts:
        return ["(no evaluations yet)"]
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    x_hi = max(xs)
    y_lo, y_hi = min(min(ys), BASELINE) - 2, max(max(ys), TARGET) + 2
    marks = "●▲■◆✦✚✱▮"
    grid = [[" "] * width for _ in range(height)]

    def row(pct):
        return min(height - 1, max(0, int((y_hi - pct) / (y_hi - y_lo)
                                          * (height - 1) + 0.5)))
    # reference lines first, so data marks overwrite them
    for val, ch in ((BASELINE, "-"), (TARGET, "=")):
        if y_lo <= val <= y_hi:
            grid[row(val)] = [ch] * width
    for x, y, i in pts:
        col = min(width - 1, int(x / max(x_hi, 1) * (width - 1) + 0.5))
        grid[row(y)][col] = marks[i % len(marks)]

    lines = []
    for r, cells in enumerate(grid):
        pct = y_hi - r * (y_hi - y_lo) / (height - 1)
        tag = ""
        if abs(pct - TARGET) < (y_hi - y_lo) / (height - 1) / 2:
            tag = "  = 50% target"
        elif abs(pct - BASELINE) < (y_hi - y_lo) / (height - 1) / 2:
            tag = "  - 50% parity with built-in bot"
        lines.append(f"{pct:5.1f}% |{''.join(cells)}|{tag}")
    lines.append("       +" + "-" * width + "+")
    lines.append(f"        0{' ' * (width - 12)}{x_hi / 1000:.0f}k games")
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--all", action="store_true",
                    help="include stopped and finished runs")
    ap.add_argument("--top", type=int, default=8, help="max runs charted")
    args = ap.parse_args()

    runs = monitor.collect()["runs"]
    live = [r for r in runs if not r["done"] and not r["stale"]]
    shown = runs if args.all else (live or runs[:args.top])
    shown = shown[:args.top]

    print()
    print(f"{'RUN':<14}{'GAMES':>8}{'EVALS':>7}{'MEAN':>9}"
          f"{'LAST8':>8}{'BEST':>7}{'ENT':>7}{'G/S':>6}  CURVE")
    print("-" * 96)
    for i, r in enumerate(shown):
        mark = "●▲■◆✦✚✱▮"[i % 8]
        pcts = [g["pct"] for g in r["gates"] if g["games"] > 0]
        spark = sparkline(pcts[-24:], min(pcts) if pcts else 0,
                          max(pcts) if pcts else 1)
        mean = f"{r['mean']:.1f}±{r['se']:.1f}" if r["mean"] is not None else "—"
        last8 = f"{r['recent']:.1f}%" if r["recent"] is not None else "—"
        best = f"{r['best']:.1f}%" if r["best"] is not None else "—"
        ent = f"{r['entropy']:.3f}" if r["entropy"] is not None else "—"
        gps = f"{r['gps']:.1f}" if r["gps"] is not None else "—"
        state = "" if (not r["done"] and not r["stale"]) else \
                (" [done]" if r["done"] else " [stopped]")
        print(f"{mark} {r['name']:<12}{r['games']:>8,}{r['n']:>7}{mean:>9}"
              f"{last8:>8}{best:>7}{ent:>7}{gps:>6}  {spark}{state}")
    print()
    for line in plot(shown):
        print(line)
    print()
    low_ent = [r["name"] for r in shown
               if r["entropy"] is not None and r["entropy"] < 0.16]
    if low_ent:
        print(f"!  entropy under 0.16 on {', '.join(low_ent)} — if the curve "
              f"is also flat, that run has stopped exploring")
    print("   mean is the number to compare (±standard error). a single "
          "400-game evaluation is ±2.5 points.")
    print()


if __name__ == "__main__":
    main()
