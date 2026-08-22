# Results

Written for whoever works on this next, human or agent. Every entry earns
its place by either **stopping you from doing something wrong** or
**telling you what to do next**. War stories were cut.

**How to read a number.** Evaluation plays the actor's argmax against the
engine's built-in bot, alternating seats, rotating all 14 opponent decks.
400 games gives a **standard error of ±2.5 points**. A single evaluation
is not a result — compare means over many. And with N runs going, the
*best* number across them is mostly a max-of-noise statistic: one arm
below showed a 50.7% best off a 46.9% mean.

---

## Constraints — violating these invalidates the work

### The server does not tell you the opponent's deck

Upstream's `docs/bots.md`: registration declares your *own* deck, the
heartbeat's `deck` field is your *own*, the observation has no archetype
field, and bots must infer the opponent "solely from observable game
actions."

So `--open-decklist` is a **research setting**. Training with it means
training on a signal the deployed bot never receives. Measured cost of
that mismatch: **~2.5 points** when an actor trained one way is evaluated
the other way (classified 51.12% vs open 48.62%, 800 games each).

Deck classification from revealed cards is what deployment actually gets:
**76.6% accurate overall, 0% on turn one** (nothing revealed yet), 53%
turn two, ~90% by turn eight.

→ A bot intended for the server must be trained with classification.
ROADMAP #6 proposes fixing this upstream.

### The critic must never deploy

The critic sees both seats' observations concatenated — effectively both
hands. It exists only to lower advantage-estimate variance during
training. Using it at play time reads information the actor is not
offered. The one legitimate exception: inside a *determinized* world,
where the sampled hypothesis supplies both hands by construction.

### Native rows must stay bit-identical to the Python path

`aac_lockstep.py` holds candidate features, privileged rows, shaped
rewards and the record/result structure to **bit equality** (verified over
569 decisions / 5202 candidates). Logits agree only to ~1e-13, and that is
expected — numpy hands the dot product to BLAS's reordered summation.
Any change to the native runner must keep this passing.

### Some decisions offer up to 538 candidates

The afterstate expansion costs one game fork plus one feature extraction
**per legal action**. Median decision: 17 candidates. But 21 of 240 games
contain one offering more than 64, up to **538**. Same 240 games, 8
threads: **47.8s with `--native-max-actions 64`, versus not finishing in
19 minutes uncapped.**

→ Do not set `--native-max-actions 0` for training. Above the cap a
decision is played greedily from a prefix and emits **no training row** —
deliberately, because a softmax over a truncated candidate set is not the
distribution the actor sampled from and must not become a PPO target.
(`max_actions=0` is correct for `aac_lockstep.py`, which needs the full
expansion to compare against Python.)

### Score candidates batched, not one at a time

`Actor::score_batch` hoists the hidden-unit loop outside the candidate
loop, so the 2.2 MB weight matrix is read once per *decision* rather than
once per candidate: **0.80 → 0.24 s/game**, bit-identical (same
accumulation order). Reverting to per-candidate scoring silently costs 3x.

---

## Settled — do not re-run these

### Hyperparameters are exhausted

Ten concurrent arms from the same warm start, ~30 evaluations each
(SE ±0.5). Means, last-10 mean in parentheses:

| arm | mean | vs control |
|---|---|---|
| entropy 0.02 | 47.11 (48.00) | +0.2 |
| ppo_epochs 8 | 46.90 (47.80) | ~0 |
| **control** | **46.94 (47.80)** | — |
| actor_lr 5e-4 | 46.78 (47.57) | ~0 |
| round_ep 192 | 46.69 (48.17) | ~0 |
| temperature 1.2 | 46.58 (48.26) | ~0 |
| selfplay_frac 0.75 | 44.90 (46.15) | **−1.7** |
| gae_lambda 1.0 | 43.44 (43.51) | **−4.3** |
| hidden 512 | 42.35 (43.80) | **−5.1** |

**Nothing beat the control significantly.** Every arm improved overnight,
so the gains came from *more games*, not tuning. λ=1.0 and selfplay 0.75
actively hurt. **Width 512 was ~5 points worse than 256 at equal games** —
the old "+5% per 2x width" claim did not replicate.

→ Do not run another hyperparameter sweep expecting gains. The next win
is structural.

### Search with this actor loses badly

The actor is **not a value function**. Nothing in AAC training anchors its
logit's scale — the loss only sees a softmax over one decision's siblings.
Logits span −142..+196, so after the value reader's sigmoid, 95.2% of
afterstates saturate and **79.5% of decisions have every candidate within
1e-6 of every other**. The search cannot tell moves apart.

| config | score |
|---|---|
| 1-ply argmax | **49.0%** |
| ISMCTS iters=16, uncalibrated | 6.2% |
| ISMCTS iters=16, Platt-calibrated | 37.5% |

Calibration (`calibrate_aac_spzw.py`) genuinely works — saturation 95.2%
→ 0%, and 6.2% → 37.5% on identical games — but calibrated search **still
loses to no search at all**, at ~5000x the cost per game. And 37.5% is
*flattered*: the ISMCTS runner takes the true decklists for both seats.

The diagnosis, and the precondition for retrying: the Platt fit buys
~0.003 nats over base rate. A strong *relative ranker*, a worthless
*absolute* win-probability estimate — exactly what MCTS backup needs.
Calibration restores discrimination; it cannot add information the actor
never learned.

→ Do not retry search with a better prompt. Retry it with a real value
function (ROADMAP #1), and fix the runner's deck handling first.

### Four throughput hypotheses are dead

One process saturates near 8 threads (~8.5 g/s); **four processes at 8
threads each reach ~26 g/s aggregate on the same machine.** ~3x remains
and the limit is per-process, not hardware. Tested and **rejected**:

| hypothesis | test | result |
|---|---|---|
| glibc malloc arena contention | mimalloc | 8.02 → 8.47 g/s |
| single-threaded PPO update | OMP 1 vs 12 | 3.29 → 3.56 g/s |
| page-fault / purge churn | `MIMALLOC_PURGE_DELAY=-1` | 8.26 → 8.71 g/s |
| heterogeneous-core stragglers | work-stealing scheduler | no gain |

→ Cause still unknown; profile with `perf` rather than guess a fifth
time. **The workaround costs nothing: run several concurrent runs, not
one wide one.** Note the dev box is an i9-13900KS — 8 performance cores
plus 16 efficiency cores at ~51% clock under load — so "32 cores" is not
32 interchangeable cores.

---

## Levers that do work

### More games

The strength curve has never plateaued.

| stage | evaluation |
|---|---|
| the built-in bot we set out to beat | 31.6% |
| 256-net + belief features (old champion) | 45.0% (200 games, ±3.5) |
| native loop, 100k games | 47.5% mean / 49.9% best (32 evals) |
| **same actor, 800-game evaluation** | **51.1% ±1.8** |

Every intermediate step ("more net / more games / +belief") also climbed.
This is the boring, reliable lever.

### Entropy — the leading indicator

The 512-width run flattened at 39–41% while its policy entropy decayed
0.171 → 0.165 → 0.154 → 0.151. Restarting it warm from its own best actor
with `--entropy-beta 0.025` pulled entropy back to **0.244** and it
resumed climbing past the ceiling it had been stuck under.

→ Entropy flattens *before* the evaluation curve does, which is why the
monitor shows it as a column. A run sliding toward ~0.15 with a flat curve
has stopped exploring; raise `--entropy-beta` and warm-restart. ROADMAP #4
automates this.

### Generation in Rust

0.9 → **7.8 games/sec**. The Python loop forked per candidate action,
round-tripped every afterstate through protocol JSON, and re-extracted
1081 features in numpy — per decision, per candidate. `spz-core/src/aac.rs`
does that natively and returns a whole round in flat buffers. **The
learner never changed**: Python still owns PPO, GAE, the critic, gating
and checkpointing.

---

## In flight

Two from-scratch runs sharing seed 33, so they form a paired comparison
isolating exactly one variable — the decklist mode:

| run | decklists | purpose |
|---|---|---|
| `cls_scratch` | classified | **the deployable line** |
| `od_scratch` | open | research; deployable only if ROADMAP #6 lands |

Plus a PR to upstream proposing an opt-in open-decklist flag (ROADMAP #6).
