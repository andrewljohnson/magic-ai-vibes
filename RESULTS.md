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

### Deck disclosure is OPT-IN, and both sides must agree

UPDATED 2026-08-22: upstream merged our open-decklist PR. `docs/bots.md`
now documents `discloseDeck: true` on registration or heartbeat, with
`opponentDeck` appearing in the observation **only when both sides opt
in**. Default is still full redaction.

So there are now two legitimate formats, and a bot must be trained for the
one it will play:

* **Disclosed** (both opted in): use `--open-decklist`. The belief block
  gets the opponent's true 60 cards.
* **Redacted** (default, and whenever the opponent has not opted in):
  train with classification. Accuracy from revealed cards is **76.6%
  overall, 0% on turn one**, 53% turn two, ~90% by turn eight.

The mismatch between them is measurable — **~2.5 points** when an actor
trained one way is evaluated the other (classified 51.12% vs open 48.62%,
800 games each) — so this is not a flag to flip casually at deploy time.
A disclosed-format bot needs a disclosed-format training run.

→ Since disclosure requires the OPPONENT to opt in too, and most will not,
the redacted bot is still the one that plays most games. Train that one
first; treat a disclosed variant as a second bot, not a replacement.

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

### Search works — but only with the right net in each role

SUPERSEDES the earlier "search loses badly" finding, which was a wiring
bug in the test harness, not a property of search.

`mcts.rs` implements the AlphaZero split: `action_prior()` reads
`policy.head` for the PUCT prior, leaf evaluation reads `policy.value`.
Every early test called `ismcts_gate(catalog, V, V, ...)` — the SAME file
for both — so the prior came from the value head, which provably cannot
rank sibling moves.

| configuration | score |
|---|---|
| value head as prior AND leaf | 6–12% |
| greedy 1-ply, value head | 3.8% |
| greedy 1-ply, actor | 50.7% |
| **value head leaf + actor prior, cap 600** | **53.1%** |

**Read that 53.1% as unproven.** It is 17W–15L over **32 games** — a
standard error of **±8.8 points**. Against greedy's 50.7% the difference
is a quarter of one standard error. The wiring fix above is real (6–12% →
~50% is far outside noise); "search then beats greedy" is not established
by this number, and the search-vs-greedy comparison still needs a few
hundred games. Do not plan around a +2.4 point search premium.

**Why the two nets are not interchangeable.** Every afterstate in an
episode carries the same label `z`, so the value head's loss never asks it
to separate siblings. Measured within-decision spread: value head **0.026**
median (20.6% of decisions under 0.01); actor **15.5 logits**. The value
head knows if a position is winning; the actor knows which move to play.
That is the policy/value split, and it is why both nets are needed.

**The actor must be calibrated for the prior role** — `head.value()`
applies a sigmoid and raw actor logits span −142..+196, so an uncalibrated
prior saturates to 0/1. `calibrate_aac_spzw.py` handles it.

**Two harness details that made search look worse than it was.** The
ISMCTS gate scores a capped game as a LOSS (`score: Some(0.0)`); the AAC
gate scores it as a DRAW. And search was being run at `max_decisions=250`
against the AAC gate's 600. Matching the cap removed all 5 capped games
and moved the score 50.0% → 53.1%. **Compare across harnesses only after
checking both conventions.**

Cost remains the real objection: ~0.09 games/sec versus ~100 for 1-ply.

### PUCT was mis-scaled, and it made search nearly a 1-ply greedy pick

FOUND 2026-08-22, while cold-starting the AlphaZero loop. The exploration
term read

    c_puct * P(a) * sqrt( ln A(a) / (1 + N(a)) )

where canonical AlphaZero PUCT is

    c_puct * P(a) * sqrt( N_parent ) / (1 + N(a)).

Unvisited actions default to **Q = 0**, which on a [0,1] value scale is
the *worst possible* score. So an unvisited sibling had to beat a visited
action's real Q on the exploration term alone, and the `ln` form is far
too weak to do it: once the first action tried returned any decent value,
the search never looked at a sibling again.

Measured on cold-start self-play, 32 iterations per decision:

| | before | after |
|---|---|---|
| median normalised visit entropy | 0.000 | 0.997 |
| median top-action visit share | 1.000 | 0.375 |

A median top-action share of **1.000** means the typical decision put all
32 visits on ONE action. Search was doing almost no searching.

→ Every search number recorded before this date — including the 53.1%
above — was produced by a search that barely branched. They are lower
bounds on what search can do, not measurements of it. **Re-measure
anything you intend to rely on.**

→ General lesson, and this is the fifth time on this project that the bug
was a SCALE rather than a learning failure (saturated actor logits, the
double-squashed value head, the value head regressing shaped return, the
peaked cold-start priors, now this). When something learns but does not
improve, print the range and the entropy before changing the algorithm.

### The "heuristic beats handcrafted 73%" claim was false

`hosted_policy.choose()`'s docstring long claimed that "first_bot's bare
ordering alone beats handcrafted ~73%" — a fixed category order (land >
cast > attack > rest) with no network. If true, a trivial heuristic would
have been 22 points ahead of every net here.

Measured with `gate_heuristic.py`, 400 games, the same protocol the actors
are scored on: **3.9% ± 1.0**. Not 73%, and well below the 31.6% built-in
bot. The trained actor is ~47 points ahead of it.

→ The neural approach is not behind a trivial baseline. The docstring has
been corrected. Worth remembering as a lesson about uncited numbers in
comments: it nearly reordered the roadmap.

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
