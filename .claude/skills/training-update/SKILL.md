---
name: training-update
description: Report the state of penta bot training runs — a table, a terminal chart, and an honest read of whether anything has actually changed. Use whenever the user asks how training is going, for a training update, how the runs look, or whether a run is improving/stalling.
---

# Training update

Run this from `penta-bot/`:

```bash
python3 training_report.py           # live runs (default)
python3 training_report.py --all     # include stopped and finished
```

It prints a table (games, evaluations, mean±SE, last-8, best, entropy,
games/sec, sparkline) and an ASCII chart with the 31.6% built-in-bot line
and the 50% target marked. Liveness comes from the process table, so
"live" means a trainer process actually exists.

Paste the table and chart, then add the interpretation. The tool reports
numbers; the judgement is the point.

## How to read it — do not skip this

**Compare MEAN, never BEST.** Evaluations are 400 games, so one carries a
standard error of ±2.5 points. `BEST` is a max-of-noise statistic and is
in the table only because people ask for it — one arm once showed 50.7%
best off a 46.9% mean. If two runs are within ~1.5 SE of each other, say
they are indistinguishable rather than ranking them.

**MEAN vs LAST8 is the trend.** LAST8 well above MEAN means it is still
climbing; converged means they meet. Do not call a climb from one
evaluation.

**ENT is the leading indicator.** Policy entropy flattens *before* the
score curve does. A run drifting toward ~0.15 with a flat curve has
stopped exploring — the fix is a warm restart from its own best actor with
a higher `--entropy-beta` (this worked: 0.151 → 0.244, and it resumed
climbing past its ceiling). The script flags entropy under 0.16 for you.
Early runs from random init show high entropy (~0.5); that is normal, not
a signal.

**G/S falls as an actor strengthens.** Better play means longer games,
which means more decisions per game. A dropping rate is usually health,
not a problem. A rate that falls while several runs share the box is just
contention.

## Before reporting, check

- Is a run near the end of its `--games` budget and about to stop?
- Did a run die? A gap between `GAMES` and the run's target with no live
  process means it stopped; say so rather than reporting stale numbers.
- Are runs a paired comparison (same seed, one variable)? Then compare
  them directly. If they differ in more than one way, say the comparison
  is confounded instead of implying a winner.

## Context worth carrying

`RESULTS.md` holds the constraints and the settled questions. Two that
come up constantly when reading a run:

- A run trained with `--open-decklist` is **not deployable** — the server
  discloses no opponent deck. Note it if such a run is being compared to
  a classified one.
- More games is the reliable lever; hyperparameters are exhausted. If
  someone proposes a tuning sweep off the back of an update, point at
  RESULTS.md → "Hyperparameters are exhausted".
