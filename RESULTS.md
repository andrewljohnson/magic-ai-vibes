# Results

Everything measured, with error bars. Kept because the numbers have to
outlive the code that produced them.

**How to read a gate.** Evaluation plays the actor's argmax against
the engine's built-in handcrafted bot, alternating seats and rotating all
14 opponent decks. Gates are 400 games, so a single gate carries a
**standard error of ±2.5 points**. A single gate is not a result. Compare
means over many gates, and note that with N runs going, the *best* gate
across them is mostly a max-of-noise statistic — one arm below showed a
50.7% best off a 46.9% mean.

---

## 1. Strength

| stage | evaluation | note |
|---|---|---|
| handcrafted bot | 31.6% | the bar |
| 128-net AAC | ~35% | prior ceiling |
| 256-net AAC | 39.8% | 400 games |
| 256-net, more games | 44.0% | |
| 256-net + belief features | 45.0% | 200 games (±3.5) — the old champion |
| **native loop, 100k games** | **47.5% mean / 49.9% best** | 32 gates |
| **same actor, 800-game gate** | **51.1% ±1.8** | first clean pass over 50% |

The curve has never plateaued. Each "more net / more games / +belief"
step kept climbing, and the native loop's 100k-game run continued it.

## 2. Hyperparameters are exhausted

Ten concurrent arms, all warm-started from the same actor, ~30 gates each
(SE ±0.5). Means, with the last-10 mean in parentheses:

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

**No knob beat the control significantly.** Every arm improved overnight,
which means the gains came from *more games*, not from tuning. Two knobs
actively hurt, and **width 512 was ~5 points worse than 256 at equal
games** — the old "+5% per 2x width" claim did not replicate.

Conclusion: the next gain is structural, not a hyperparameter.

## 3. Entropy collapse causes plateaus — and is reversible

The 512-width run flattened at 39–41% while its policy entropy decayed
0.171 → 0.165 → 0.154 → 0.151. Restarting it warm from its own best
actor with `--entropy-beta 0.025` pulled entropy back to **0.244** and it
resumed climbing past the ceiling it had been stuck under.

**Entropy is the leading indicator**: it flattens *before* the gate curve
does. The monitor shows it as a column for this reason. A run sliding
toward ~0.15 with a flat curve has stopped exploring.

## 4. Throughput: generation moved to Rust

| | games/sec |
|---|---|
| Python fork-pool trainer | ~0.9 |
| **native runner** | **7.8** |

The Python loop forked the game once per candidate action, round-tripped
every afterstate through protocol JSON, and re-extracted 1081 features in
numpy — per decision, per candidate. `spz-core/src/aac.rs` does that
natively and hands back a whole round of trajectories in flat buffers.
**The learner never changed**: Python still owns PPO, GAE, the critic,
gating and checkpointing.

`aac_lockstep.py` certifies it. Replaying a native episode's moves
through the original Python path, candidate features, privileged critic
rows, shaped rewards and the record/result structure are **bit-identical**
over 569 decisions / 5202 candidates. Logits agree to 1.4e-13 — not
bit-equal by design, because numpy hands the dot product to BLAS's
reordered summation while the native scorer accumulates straight through.

### 4a. The "native engine hang" was never a hang

The old trainer's watchdog fired on workers presumed stuck in engine code.
They were not stuck. The afterstate expansion costs one game fork plus one
feature extraction **per legal action**, and while the median decision
offers 17 candidates, **21 of 240 gate games contain one offering more
than 64 — up to 538**. That is ~30x a normal decision, several times per
game. Same 240 games, 8 threads: **47.8s with the cap, versus not
finishing in 19 minutes uncapped.**

`--native-max-actions` bounds it: past the cap a decision is played
greedily from a prefix and emits **no training row**, because a softmax
over a truncated candidate set is not the distribution the actor sampled
from and must not become a PPO target.

### 4b. Batching the actor forward

Scoring candidates one at a time re-streamed the whole 256×1081 f64 weight
matrix (2.2 MB, well past L2) per candidate. Hoisting the hidden-unit loop
outside the candidate loop reads each weight row once per *decision*:
**0.80 → 0.24 s/game**, bit-identical (same accumulation order).

### 4c. Per-process scaling — UNRESOLVED

One process saturates near 8 threads (~8.5 g/s); **four processes at 8
threads each reach ~26 g/s aggregate on the same machine.** So ~3x remains
and the limit is per-process, not hardware. Four hypotheses were tested
and **rejected**, each worth 5–8% at most:

| hypothesis | test | result |
|---|---|---|
| glibc malloc arena contention | mimalloc | 8.02 → 8.47 g/s |
| single-threaded PPO update | OMP 1 vs 12 | 3.29 → 3.56 g/s |
| page-fault / purge churn | `MIMALLOC_PURGE_DELAY=-1` | 8.26 → 8.71 g/s |
| heterogeneous-core stragglers | work-stealing scheduler | no gain |

Cause unknown. The workaround costs nothing: **run several concurrent
arms rather than one wide job.** Worth knowing that the dev box is an
i9-13900KS — 8 performance cores plus 16 efficiency cores, at ~51% clock
under load — so "32 cores" is not 32 interchangeable cores.

## 5. Search (ISMCTS) — NEGATIVE, do not re-run blind

The bot is a 1-ply afterstate scorer and the old C++ reference was 57.7%
*with* determinized search, so plugging the trained actor into spz-core's
existing ISMCTS looked like the cheap path past 50%. It is not.

**The actor is not a value function.** Nothing in AAC training anchors
the logit's absolute scale — the loss only sees a softmax over one
decision's siblings. Measured: logits span −142..+196, and after the
value reader's sigmoid, 95.2% of afterstates saturate and **79.5% of
decisions have every candidate within 1e-6 of every other.** The search
could not tell moves apart.

| config | score |
|---|---|
| 1-ply argmax | **49.0%** |
| ISMCTS iters=16, uncalibrated | 6.2% |
| ISMCTS iters=16, Platt-calibrated | 37.5% |

Calibration (`calibrate_aac_spzw.py`) is a real fix — saturation 95.2% →
0%, and 6.2% → 37.5% on identical games — but calibrated search **still
loses badly to no search at all**, while costing ~5000x per game (1466s
for 16 games). And 37.5% is *flattered*: the ISMCTS runner takes the true
decklists for both seats.

Why it fails, and what would change the answer: the Platt fit buys ~0.003
nats over base rate. The actor is a strong *relative ranker* and a nearly
worthless *absolute* win-probability estimate — which is exactly what MCTS
backup needs. Calibration restores the ability to distinguish moves; it
cannot add information the actor never learned.

**Revisit only with a real value function.** See ROADMAP #1.

## 6. Open decklists

The repo held two lineages that disagreed about what the bot may know:
the determinized/ISMCTS path always used the **true** decklists, while the
AAC path **classified** the opponent's deck from revealed cards. Measured
classification accuracy over 9487 decisions:

| turn | 1 | 2 | 3 | 4 | 8 | overall |
|---|---|---|---|---|---|---|
| accuracy | **0%** | 53% | 64% | 77% | 90% | **76.6%** |

A quarter of decisions computed the unseen-pool maths against the wrong 60
cards, worst exactly where planning matters. It also meant our numbers
were never comparable to the 57.7% reference, which had open decklists.

**Decided at the time: open decklists.** SUPERSEDED -- upstream's bot
guide confirms the server discloses no deck metadata, so open decklists
are a research setting only and cannot be used to train a deployed bot.

Inference-only A/B on an actor *trained* under classification: classified
51.12%, open 48.62% (800 games each). That is distribution shift, not
evidence — an actor that spent 100k games learning to exploit classified
features breaks when handed a different input distribution. The
train-from-scratch comparison is what settles it, and is in flight.
