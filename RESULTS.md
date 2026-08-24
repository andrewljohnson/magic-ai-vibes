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
are scored on: **3.9% ± 1.0**. Not 73%, and far below the 50% that
would be parity with the built-in bot. The trained actor is ~47 points ahead of it.

→ The neural approach is not behind a trivial baseline. The docstring has
been corrected. Worth remembering as a lesson about uncited numbers in
comments: it nearly reordered the roadmap.

### The pure self-play build reaches 37–43% — it does NOT beat the bot

**CORRECTED 2026-08-23.** An earlier version of this section claimed 37.5%
"beats the built-in bot's 31.6%". That was wrong, and the error was in the
BASELINE, not the measurement.

The gate scores OUR win rate in head-to-head games against the built-in
handcrafted bot (`mcts_runner.rs`: `z = 1.0 if winner == our_seat`). So
the bar for beating it is **50%**, by definition. 37.5% means we lose
62.5% of those games.

The "31.6%" traces to the abandoned C++ alpha-sim, where it was the LOWER
BOUND of a 95% confidence interval for a *learned* challenger against
handcrafted ("Aggregate: 76-124 (38.0%), 95% interval 31.6%-44.9%"). It
was never the handcrafted bot's own score, and it could not be -- a bot's
win rate against itself is 50% by symmetry. It was copied into README.md,
RESULTS.md, monitor.py, training_report.py and gate_heuristic.py as "the
bar the project set out to beat" and repeated for months.

→ **Parity is 50%.** The only line here that has ever cleared it is the
AAC actor at 51.1% ±1.8, and only just.

2026-08-23, run `az_long`, both nets from random init, no handcrafted
bootstrapping anywhere in the loop (the pure-build rule in AGENTS.md).

| round | gate | capped | note |
|---|---|---|---|
| 59 | 9.3% (34.8% as draws) | 153 / 300 | half the games never resolved |
| 119 | **37.5% ± 2.8** | **0 / 300** | 112W 1D 187L |

37.5% is well BELOW the 50% that is parity, and because ZERO games were
capped the two scoring conventions agree exactly -- there is no
capped-as-loss / capped-as-draw ambiguity to argue about for the first
time here.

The capping collapsed on its own between those two gates. That is
`--truncation loss` working as intended but with a long delay: the policy
had to learn to CLOSE GAMES OUT before the gate stopped truncating, and
that took ~60 rounds. The earlier A/B that found "no win-rate effect" was
reading gates taken before the effect had arrived.

Per-matchup at round 119: The Deck 86.4, Counterburn 59.1, Robots 57.1,
Lions DIB 47.7, BWR Aggro / Erhnamgeddon / Troll Disk 33.3, Goblins /
Jeskai 31.8, White Weenie 28.6, Artifacts / Lion Dib Bolt 23.8, Mono
Black 18.2, GR Aggro 14.3.

→ Behind the AAC line's 47.5–51% and behind parity (50%). The AAC line
took ~100k games and this is ~6k. The remaining weak matchups are aggro, same as ROADMAP A.

### Truncation scoring: fixes stalling, does NOT move win rate

Two arms, identical but for how a game hitting the decision cap is
labelled. `drop` removes those records; `loss` scores 0.0 for both seats,
matching what the gate already does.

| | drop | loss |
|---|---|---|
| gate @ round 39 | 16.7% | 24.2% |
| gate @ round 79 | 24.2% | 20.0% |
| self-play games finishing | 69–88% | **88–96%** |
| capped games in gate | 3, but excursions to **43** | 35 → **4** |

The mechanism works and is worth keeping: penalising truncation makes
games finish, and it removes the stall excursions the `drop` rule allowed
(nothing corrects a behaviour that produces no gradient). But the WIN RATE
is unchanged -- the two arms cross over between gates and are
indistinguishable pooled.

→ Keep `--truncation loss`, for the objective/metric alignment and the
stall fix, not for strength.

→ **A 120-game gate has SE ±3.8.** Comparing arms a few points apart at
that precision is comparing noise, which is what the crossover above
actually shows. Use ≥300 games before believing an arm difference.

### The sigmoid was eating the value head's move ranking

2026-08-23. Search stopped helping: on the same net, 32 sims scored 40.0%
and 1 sim scored 39.3%. A leaf evaluator that gives every sibling the same
score makes Q constant, and PUCT then follows the prior -- search becomes a
slower copy of the policy.

Measured on the NATIVE afterstate features search actually evaluates
(candidate afterstates from `aac_stream_episodes`, deck-slot belief context
included), 285 decisions:

| | sibling LOGIT spread | prob spread search sees | decisions blind (<0.01) |
|---|---|---|---|
| hard 0/1 targets | 0.899 | 0.0035 | 56.5% |
| label smoothing 0.05 | 0.709 | **0.0215** | **38.6%** |

The value head DOES separate sibling moves -- about 0.9 logits, plenty to
rank them. But BCE against hard 0/1 drives logits outward wherever the data
is separable (|logit| median 4.92, max 29.3, a quarter of decisions past
|8|), and out there the sigmoid is flat, so the ranking is destroyed
between the net and the search. Smoothing targets to [e, 1-e] caps the
optimal logit at ln((1-e)/e) = 2.94 and multiplied the usable spread by 6.

This is the SIXTH time on this project that the bug was an output SCALE
rather than a learning failure. The others: actor logits saturating the
sigmoid, the double-squashed value head, the value head regressing shaped
return, peaked cold-start priors, mis-scaled PUCT exploration.

→ `--value-smoothing` (default 0.05). The round log now prints `vmag`, the
median |logit|, so this cannot hide again.

→ **Measure on the real feature path.** A first pass at this used
`Extractor.features()` with no deck context, so the belief block was empty
and the inputs were out of distribution. It reported |logit| 26.8 and 92.6%
of decisions blind -- roughly five times the true saturation. The
conclusion survived; the numbers did not.

### Fixing the value head revived search: +0.7 points -> +13.3, and the
### gate went 44.0% -> 49.2%

Same net, same 120 games, only the search budget varies. Measured after
`--value-smoothing 0.05`:

| configuration | score |
|---|---|
| no search (1 sim) | 30.0% ± 4.2 |
| **32 sims, c_puct 1.5** | **43.3% ± 4.5** |
| 32 sims, c_puct 0.5 | 42.5% ± 4.5 |

Search is worth **+13.3 points** over its own prior. Before the value-head
fix it was worth +0.7 (39.3% vs 40.0%) -- i.e. nothing. That is the
policy improvement operator coming back to life, and it is what the loop
needs to keep climbing.

c_puct 1.5 and 0.5 are identical within noise, so the exploration/Q
balance was never the constraint. The saturated sigmoid was.

→ **The deployed bot must use search.** ROADMAP B estimated ~+2 points
from deploying search; on a working value head it is +13.

**The training gate followed immediately.** Best before the fix was 44.0%;
the first gate after it was **49.2% ± 2.9** (147W 1D 152L / 300) at round
39 -- at parity within noise, from a pure from-scratch self-play build with
no handcrafted bootstrapping. One squashing function was worth ~9 points
of playing strength.

### How strong is the AZ policy improvement operator? ~6 points

MEASURED 2026-08-23 on the az_main checkpoint (~8k self-play games).

The loop only ratchets if search plays BETTER than the policy that
generated it. Gated against the built-in bot, 84 games:

| search budget | gate | cost |
|---|---|---|
| 1 sim (the raw policy) | 20.2% ± 4.4 | 2s |
| 32 sims | **26.2% ± 4.8** | 185s |

So search is worth about **6 points** over its own prior, and the policy
has NOT yet absorbed that -- which is the gap the loop exists to close.

But the operator is weak in a specific, measurable way. Comparing the
search's visit distribution to the prior over 1792 decisions:

| sims | search argmax == policy argmax | KL(visits ‖ policy) |
|---|---|---|
| 32 | 92.0% | 0.0426 |
| 128 | 92.4% | **0.0241** |

Search agrees with the prior on 92% of decisions, and MORE simulations
make it agree MORE, not less. Raising the simulation budget is therefore
not the lever it looks like -- 4x the cost moved the target closer to
where the policy already was. (Mean decision offers only ~5 actions, so
32 sims is already ~6 visits per action.)

→ **SUPERSEDED 2026-08-23.** Everything above was measured with the
SATURATED value head, where extra simulations could only re-confirm the
prior -- of course more of them did not help. With the sigmoid fixed,
search scales with budget again, 120 games each:

| sims | score |
|---|---|
| 1 | 35.8% ± 4.4 |
| 32 | 41.7% ± 4.5 |
| 128 | **46.7% ± 4.6** |

Each 4x in budget buys about 5 points, monotonically. So DO raise the
search budget -- training targets at 32 sims are much weaker than they
could be, and a deployed bot should search as hard as its clock allows.

The lesson worth keeping is the one about the measurement, not the number:
a conclusion measured through a broken component describes the breakage,
not the question.

### The dominance prune costs 6.4x in search and buys nothing

MEASURED 2026-08-23, 40 games, trainer paused so the machine was idle:

| gate configuration | s/game | win rate |
|---|---|---|
| dominance OFF | **1.17** | 17.5% |
| dominance ON | 7.55 | 17.5% |

Identical strength, 6.4x the cost. `dominance_keep` clones the game and
applies once PER ACTION, and `available()` calls it at EVERY node of
EVERY descent -- so it pays the same afterstate expansion this codebase
removed everywhere else (~13 ms per action row). It is a certified prune,
so it cannot weaken play; it simply is not worth what it costs inside a
tree search.

→ Self-play always ran with it off. The gate had it ON because I turned it
on assuming a "certified prune" was cheap, which took a 120-game gate from
~140s to ~1161s. Do not enable it in a search loop. `AZ_GATE_DOM=1`
re-enables it for measurement only.

→ The general lesson is the one this project keeps relearning: measure the
real workload with nothing else running. An earlier attempt at this
comparison ran the gate CONCURRENTLY with a 12-thread trainer and was
pure noise.

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

| stage | win rate vs the built-in bot |
|---|---|
| **PARITY with the built-in bot (the actual bar)** | **50.0%** |
| **pure AZ self-play, after the value-head fix** | **49.2% ±2.9** |
| pure AZ self-play, before that fix | 37–43% |
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
