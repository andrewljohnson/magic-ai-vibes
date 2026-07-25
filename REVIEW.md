# Independent Review Log

A second agent maintains this file as a running external review of bot-strength
work. Entries are timestamped, newest first. Each entry critiques the latest
state of the repo and, where possible, contributes independently generated
experimental data. Numbers reported here come from actual runs of the checked-in
binary, never from extrapolation.

## Status at a glance

*Updated 2026-07-25 10:51 PDT — refreshed at the top of every review cycle.*

- **HEADLINE: the challenger beat Handcrafted with confidence.** 2,000
  paired games on a virgin seed in the four-deck environment: 55.1%,
  95% CI 53.0%–57.3%. First clean above-50% result in project history
  (the old 54.1% was leakage-inflated). Green and Blue slices dominant,
  White an exact tie; Red (35.2%) is the one unsolved deck.
- **Environment FROZEN at `c64b80c`.** Five-deck Old School engine
  committed with 122 passing tests and a zero-finding ASan/UBSan run.
  Canonical lift-gate baseline: 2 of 5 (White +55.0 and Red +43.8 pass;
  Green −1.3 and RU −5.0 are within one-cell noise; Blue −17.5 is the
  one confirmed gap). Future table deltas now mean learning progress.
- **Next big move (in progress):** port the challenger recipe (n-step
  bootstrap, 16 generations, search-on collection, K=8) onto the frozen
  five-deck engine — its old-world Blue transformation (64.6% vs 46.0%)
  is the best lead on the Blue gap, and Codex has correctly required
  five-deck revalidation before it affects any verdict.
- **What Codex shipped:** the whole five-deck Old School engine in one
  push — RU Aggro rules (flying, Ironclaw restriction, Disintegrate
  X/exile), Giant Growth, frozen predeclared Handcrafted heuristics for
  every new card, clean-break schema policy, a 30,000-game deck-balance
  matrix with a 45-55% regression guard, 121 tests passing. Earlier in
  the cycle: Mix50 rejected with a clean causal readout (search share
  exonerated for the G4→G5 collapse).
- **Claude challenger: crossed 50%.** G16 at K=8 scored 51.7% pooled vs
  Handcrafted (49.5/53.0/52.5) on the frozen three-seed screen — the
  program's monotone ladder is 43.3 → 46.7 → 48.7 → 51.7. Blue flipped
  from worst slice to 65.3% pooled, beating Handcrafted's Blue — strong
  confirmation of the stack-tactics hypothesis and directly relevant to
  the five-deck Blue/Green gap. A 2,000-game confirmation on fresh seed
  202 is in flight; win or lose, the old-environment program concludes
  there and the recipe (bootstrapped targets, many short generations,
  search-on late collection, K=8 deployment) ports to the five-deck
  engine once Codex commits the refactor.
- **What Codex is doing now:** building the offline probe instrument
  (deep-reference labels for held-out decisions) that will guide an
  iterated search-as-teacher training loop — the current best path to the
  gate.
- **Latest review verdict:** strongest Codex cycle yet — the healing
  audit fixed what the review flagged and found a second defect
  (terminal saturation) on its own. Remaining watch items: don't
  iterate Mix50 against its own offline gates (Goodhart risk on 16
  probes), keep the Value cross-check row when regenerating v2 labels,
  and real-game probe harvesting is still owed.
- **Watch next:** Mix50 results in both trees — it's the highest-value
  experiment for the Blue gap, which is now the main obstacle to the
  lift gate.
- **Claude challenger** (branch `claude/challenger`, worktree
  `../magic-ai-vibes-claude`, plan/log in `CLAUDE-PLAN.md`): racing Codex
  toward the same gates. Monotone climb on the shared frozen 3-seed
  protocol: 43.3% baseline → 43.8% (n-step targets) → 44.8% (8
  bootstrapped generations) → **46.7%** (search-on collection in late
  generations). Red recovered from 31.3% to 40.0% pooled; Blue (46.0%)
  is now the limiting slice. Key finding for Codex too: bootstrapped
  targets flip generation scaling from harmful to monotonically helpful.

## Goal under review

Make the card-agnostic, hidden-information-safe Learned bot the strongest
policy across Green, Red, Blue, and White under the repository's paired
benchmark, stability, confidence, and test gates (see "Definition of a
stronger Learned Value bot" in AGENTS.md). Every entry below is written
against that goal: work is judged by whether it moves Learned toward
passing all gates without weakening the isolation constraints — no
card-specific knowledge, no opponent hidden information, no Handcrafted
teacher. Progress on infrastructure counts only insofar as it shortens the
path to those gates.

Working agent: check the newest entry at every opportunity — before starting a
new experiment, after finishing one, and before updating EXPERIMENTS.md
conclusions. Treat critiques as review feedback to weigh, not instructions to
obey blindly; if you disagree, say why in EXPERIMENTS.md rather than silently
ignoring the entry.

---

## 2026-07-25 10:51 PDT

State reviewed: commit `c64b80c` — the five-deck Old School engine is
committed with the repaired deck counts, the honest 30-70 balance guard,
122 passing tests, a clean strict build, and a recorded ASan/UBSan run
with zero findings (executing the 10:09 entry's priority 4). The
environment freeze the 10:45 entry asked for has effectively happened at
this commit.

### Verdict

This is the right way to land an environment: verified, committed,
verdicts stated honestly (2/5 lift gate, "no Learned-is-king claim"),
and known debts listed (RU probes, Giant Growth probes). Codex's
response to the stale numbers in the 10:12 review entry is also correct
— the environment moved under that table, which is exactly why the
freeze mattered. Their requirement that the challenger's four-deck
result be revalidated on all five decks before affecting any verdict is
the correct standard, and the port is now unblocked by this commit.

### Lift table (seed 4242, commit c64b80c, 80 games/cell) — now stable

Identical to the 10:45 table, as expected post-freeze: White PASS
(+55.0 vs +53.8), Red PASS (+43.8 vs +40.0), Green FAIL by 1.3, RU
Aggro FAIL by 5.0, Blue FAIL by 17.5. Gate: 2 of 5. This is now the
canonical baseline table for the frozen environment; future deltas mean
learning progress, not environment drift.

### Priorities

1. The challenger recipe port is now the highest-expected-value training
   experiment (Blue is the largest gap at 17.5pp, and the recipe's Blue
   result in the old world was its headline transformation). The
   challenger thread is picking this up.
2. Green (−1.3) and RU (−5.0) are inside one-cell noise (±11pp at 80
   games); treat Blue as the only confirmed-failing deck until a bigger
   sample says otherwise. Don't spend training experiments chasing the
   two near-ties yet.
3. RU and Giant Growth probes remain owed before the next learning
   change, per Codex's own notebook — hold that line.
4. Suggest tagging `c64b80c` (e.g. `env-oldschool-v1`) so future
   environment changes are explicit version bumps rather than drift.

## 2026-07-25 10:45 PDT

State reviewed: Codex's predeclared deck-balance program (bounded
1,800-list Random search producing candidate C1 — richer RU counts plus
small Green/Blue trims — with honest reject-on-untouched-seed gates and a
softer C2 fallback), now live in the working tree. Plus: the Claude
challenger's milestone confirmation completed this cycle.

### Verdict

The C1/C2 design is good experiment discipline (selection on one seed
family, validation on untouched seeds, explicit refusal to pretend the
strict gate passed if only the soft one does). But the environment has
now changed decklists twice in two hours, and every change invalidates
every model, probe label, and comparison. Recommendation: finish C1/C2,
then FREEZE the environment with a version tag (decklists + Handcrafted
heuristics + schema) and declare training-comparison season open only
after that freeze. Training recipes cannot be compared across a moving
world.

### Challenger milestone (four-deck environment, confirmed)

2,000 paired games, virgin seed 202, frozen G16 model, K=8 deployment:
**1103-897 (55.1%), 95% CI 53.0%-57.3% — beats Handcrafted at 95%
confidence.** Green 47.0%/20.8% and Blue 64.6%/46.0% dominant, White an
exact 73.8%/73.8% tie, Red 35.2%/38.8% still failing. The old 54.1%
"champion" was leakage-inflated; this is the first clean, confirmed
above-50% result in project history. The recipe (n-step bootstrap
targets, 16 short generations, sliding replay window, search-on late
collection, K=8 deployment) is documented in CLAUDE-PLAN.md and is the
porting candidate for the five-deck world.

### Lift table (seed 4242, C1 decklists, 80 games/cell) — env changed again

| Deck | Learned lift | Best rival lift | Verdict | vs 10:09 table |
| --- | ---: | ---: | --- | --- |
| White | +55.0 pp | +53.8 (Handcrafted) | PASS | held |
| Red | +43.8 pp | +40.0 (Handcrafted) | PASS | held |
| Green | +32.5 pp | +33.8 (Handcrafted) | FAIL by 1.3 pp | recovered from −22.6 |
| RU Aggro | +51.2 pp | +56.2 (Handcrafted) | FAIL by 5.0 pp | flipped from +12.4 PASS |
| Blue | +27.5 pp | +45.0 (Handcrafted) | FAIL by 17.5 pp | worsened |

Gate: 2 of 5. Every cell moved because the decklists moved — deltas here
measure the environment, not learning progress. This is the concrete
cost of comparing across an unfrozen world, and why the freeze
recommendation above is priority one.

### Priorities

1. Freeze the environment (C1 or C2, decided by the predeclared gates),
   tag it, and only then open training comparisons. All lift-table
   deltas until then are environment noise.
2. Port the challenger recipe onto the frozen five-deck engine as the
   first post-freeze training experiment — its Blue result (64.6% vs
   46.0% in the old world) is the best known lead on the five-deck
   Blue gap, and generation/K scaling are the two knobs with five
   monotone data points behind them.
3. Red remains unsolved by every recipe in both trees (35-40% vs
   Handcrafted across all configurations). After the freeze, Red-burn
   probes (Bolt sequencing, face-vs-creature, Disintegrate X sizing in
   RU) deserve the same treatment Giant Growth got.
4. Codex's C2 "somewhat balanced" fallback honestly documents that the
   requested tactical density cannot hit 40% against Green/Blue under
   Random play — good; keep that documentation in the frozen
   environment's README so future balance complaints re-read it.

## 2026-07-25 10:12 PDT

No-change cycle: no new EXPERIMENTS.md entries since 10:09; the heavy
source churn (+3,643/−3,417) is the announced clean-break refactor with no
functional delta yet — binary is unchanged, all 121 tests still pass, and
the five-deck lift table is deterministic-identical to 10:09 (RU Aggro,
White, Red PASS; Blue −15.0 and Green −22.6 FAIL; gate 3 of 5).

Challenger status for context: the 16-generation K=8 screen's first seed
came in at 49.5% vs Handcrafted (old four-deck environment; G8 at the
same seed was 45.5%), with Blue at 64% on that seed. Two seeds remain;
if the Blue jump holds across seeds it strengthens the
deployment-K/stack-tactics hypothesis from the 10:09 entry.

## 2026-07-25 10:09 PDT

State reviewed: uncommitted five-deck Old School engine (+2,301 lines over
`1f02b63`): RU Aggro as a full fifth metagame deck per the user's
promotion, Giant Growth added to Green, frozen predeclared Handcrafted
heuristics for all new cards, clean-break schema policy, 30,000-game deck
balance matrix with a 45-55% band regression guard. All 121 tests pass
and the five-deck mixed field runs end to end.

### Verdict

The environment expansion landed fast and clean, and the first five-deck
lift table is the most informative single result of the project so far:
Learned's best deck is now the richest one. RU Aggro — repeated curve
decisions, flying evasion, an X-spell — is exactly where a card-agnostic
learner was predicted to shine, and it does: 80% win rate, +66.2pp lift,
12.4 points clear of Handcrafted. The environment-richness thesis is no
longer speculative.

### Lift table (seed 4242, NEW five-deck environment, 80 games/cell)

| Deck | Learned lift | Best rival lift | Verdict |
| --- | ---: | ---: | --- |
| RU Aggro | +66.2 pp | +53.8 (Handcrafted) | PASS decisively |
| White | +56.2 pp | +56.2 (Handcrafted) | PASS (exact tie) |
| Red | +53.8 pp | +41.2 (Handcrafted) | PASS decisively |
| Blue | +36.2 pp | +51.2 (Handcrafted) | FAIL by 15.0 pp |
| Green | +6.2 pp | +28.8 (Handcrafted) | FAIL by 22.6 pp |

Gate: 3 of 5. NOT comparable to any previous table — new environment,
new decklists, rebaselined everything. Red flipped to a decisive pass.
Green collapsed from the strongest deck to the weakest: its list swapped
4 Ironroot Treefolk for 4 Giant Growth, and combat-trick timing on the
stack is the single hardest decision class for a terminal-outcome value
model — while Handcrafted received hand-tuned Giant Growth rules
(9,000/9,500 lethal-prevention/lethal-push scores). This is the White
lock-plan problem reborn in a sharper form.

### Priorities

1. Green/Giant Growth is the new critical slice. Before any training
   experiment, add Giant Growth decision probes (pump-to-save vs
   pump-to-push vs hold, in response to Bolt, in combat) — the existing
   targeted stack-replay machinery records these states, but nothing
   verifies the learner values them correctly.
2. Blue's 15-point gap persists across environments and now has company:
   both failing decks are the instant-speed/stack-tactical ones. That
   pattern (values fine on sorcery-speed decks, behind on instant-speed
   decks) is a sharper hypothesis than "Blue is hard": the K=2
   determinization at stack decision points is likely the binding
   constraint. The challenger's monotone deployment-K result (46.7→48.7%
   pooled from K=2→K=8 in the old environment) is directly relevant —
   test deployment K scaling on Blue/Green in the new environment.
3. The frozen Handcrafted heuristics were predeclared exactly as the
   09:47 review asked — good. Same discipline now needed for the
   training budget: 800 games across 20 ordered deck pairs is 2.5x
   thinner per pairing than the four-deck environment; predeclare
   whether train-games scales with the pairing count before comparing
   recipes across environments.
4. Sanitizer verification (ASan/UBSan) on the new engine is declared as
   a gate but not yet recorded in the notebook as run — run and record
   it before the first preregistered five-deck benchmark.

## 2026-07-25 09:47 PDT

State reviewed: commit `1f02b63` (Mix50 rejection recorded) plus in-flight
Old School scope expansion — AGENTS.md rules for the additive RU Aggro
diagnostic deck, the declared decklist/rules contract, and the
`old-school-sim` rebrand in the Makefile. All 106 tests pass.

### Verdict

Two strong pieces of science this cycle. (1) The Mix50 rejection is a
model result: halving searched trajectories improved calibration (Brier
0.0725 vs 0.1145) while *worsening* action regret (0.0203 vs 0.0138), and
the G4→G5 Red/White collapse survived — so search-trajectory share is
exonerated as the isolated root cause, and the follow-up (all-raw G5 from
the exact G4 checkpoint) is the correct causal isolation. (2) The RU
Aggro contract is exemplary environment engineering: exact card rules,
enumerated engineering gates, fail-closed model-schema versioning, and an
explicit no-strength-claims boundary. Note for the challenger thread:
Mix50's rejection weakens the case for the challenger's own mix variant;
its generation-ladder diagnostic (does the challenger recipe have a
mid-ladder collapse at all?) is now the better next experiment.

### Lift table (seed 4242, 60 games/cell) — unchanged from 09:27

Green PASS (+48.3 vs +30.0), Red PASS (tie +30.0), White FAIL by 1.6pp
(tie range), Blue FAIL by 13.4pp. Gate: 2 of 4. No learned-path changes
landed, so the table is deterministic-identical.

### Priorities

1. Sequence the raw-G5 causal test BEFORE the schema migration lands in
   the same tree. The feature-dimension change (exile zone, new card
   identities) makes every existing artifact non-reproducible in the new
   binary; running the G5 isolation after migration confounds it. Do it
   now on the frozen Alpha environment, or on a pre-migration branch.
2. The RU contract is silent on Handcrafted's new card values. Predeclare
   them before any comparative RU reporting — an untuned Handcrafted
   playing RU Aggro would make every "vs Handcrafted" RU number a
   strawman win for Learned. (AGENTS.md already keeps RU out of the
   gate; this is about honest diagnostics, not the gate.)
3. Bound Disintegrate's action enumeration explicitly (X ranges over all
   affordable values × all targets — the largest action space in the
   pool) and include a determinization-cost regression so search budgets
   stay comparable across decks.
4. Probe corpus versioning: probe-dev-v1/v2 fixtures embed GameState;
   the exile-zone addition changes state shape, so version the fixture
   corpus alongside the model schema — same fail-closed rule.

## 2026-07-25 09:27 PDT

State reviewed: commit `58c3370` (deterministic probe scoring, reference
semantic fixes) plus a large in-flight iteration framework
(`learned_iteration.{hpp,cpp}`): domain-separated seeds, balanced deck
schedules, TD(lambda) machinery, checkpointed G0-G8 generations with model
caches and fingerprints, per-generation probe attribution, and a
predeclared "Mix50" collection repair. All 106 tests pass (66 engine, 6
iteration, 12 probe, 10 probe-metric, 12 probe-runner, CLI).

### Verdict

Strongest Codex cycle so far. The continuation-healing audit executed the
23:38 review's priorities and went further: unit traces confirmed healing
in `red.bolt-face-lethal` (the Pass branch casts the same Bolt later),
cleared `blue.counter-lethal-bolt` of a sampler bug, and then isolated a
*second*, distinct defect — terminal-outcome saturation, where branches
genuinely diverge but reach the same terminal within the horizon. H=4 was
correctly rejected for erasing a known defensive improvement. The
probe-dev-v2 design choice (root-irreversible fixtures at real
last-opportunity timing, not an artificial persistent-Pass rule) is better
than the reviewer's own suggestion — it keeps labels executable under
real Magic semantics. Also noted: `four_state_bootstrap_targets` adopts
the challenger's frozen-parent bootstrap design — cross-pollination
working as intended.

### Convergent evidence on Blue (important)

Codex's checkpoint attribution found canonical G8 (all-search late
collection) loses 16.0 points of Blue while gaining 16.7 of White vs G3.
The Claude challenger — a completely separate implementation of
generation scaling — independently landed with Blue as its worst slice
(46.0%) after enabling all-search collection in late generations. Two
codebases, same signature. This materially raises confidence in the Mix50
hypothesis (mixed raw/search collection), and the challenger will test
its own mixture variant in parallel.

### Lift table (seed 4242, 100 games/matchup, 60 games/cell)

| Deck | Learned lift | Best rival lift | Verdict |
| --- | ---: | ---: | --- |
| Green | +48.3 pp | +30.0 (Handcrafted) | PASS |
| Red | +30.0 pp | +30.0 (Handcrafted) | PASS (exact tie) |
| White | +61.7 pp | +63.3 (Handcrafted) | FAIL by 1.6 pp (tie range) |
| Blue | +38.3 pp | +51.7 (Handcrafted) | FAIL by 13.4 pp (real) |

Gate: 2 of 4 passing. Delta vs the user's earlier view: Red recovered
from a clear FAIL to exact parity. Blue is the one substantive gap;
White is a coin-flip miss at this sample size (±13 pp/cell).

### Priorities

1. Mix50's eight offline gates use numerically exact thresholds derived
   from canonical baselines. As one-shot rejection gates they are fine;
   do not iterate the recipe against them — passing a 16-fixture corpus
   by tuning is Goodhart, and the notebook's own "reject but never
   promote" language should be enforced literally.
2. The planned bootstrap-after-turn reference horizon is the right fix
   for terminal saturation, but it re-introduces critic bias into
   labels; keep the Value-continuation cross-check row when regenerating
   v2 labels.
3. The Blue convergence makes Mix50 the highest-value experiment in
   either tree. Run it before further probe-semantics refinement.
4. Real-game probe harvesting (disagreement states, Learned-loss states)
   remains owed and is now the main corpus-quality bottleneck.

## 2026-07-24 23:38 PDT

State reviewed: uncommitted probe-runner implementation (`probe_runner.{hpp,cpp}`,
CLI `--score-probes`, label cache, five policy views, reference-sensitivity
cross-check) plus the recorded first-light results in EXPERIMENTS.md.

### Verdict

The harness is accepted and the reporting is honest. My independent run of
`--score-probes --train-seed 424242 --train-games 800` reproduced the
recorded metrics exactly (71.5s), confirming end-to-end determinism. The
review responsiveness is also noted: the Value-continuation cross-check and
deployed-policy-row corrections from the 23:09 entry were implemented
before generating the canonical cache. However, the reviewer ran the queued
horizon escalation, and it refutes the notebook's working hypothesis about
the exact-zero pairs — see data below. The instrument as built has a
structural blind spot that should be fixed before the K=8/H=4 teacher
comparison or any G1 iteration.

### Independent experimental data: the zeros are structural, not horizon

```sh
./build/alpha-sim --score-probes --train-seed 424242 --train-games 800 \
  --probe-horizon 24 --probe-cache <scratch>/probe-h24.labels.tsv
```

| Best-pair delta | H=12 (canonical) | H=24 (reviewer) |
| --- | ---: | ---: |
| green.develop-bears pass vs cast-bears | 0.0000 ± 0.0000 | 0.0000 ± 0.0000 |
| red.bolt-face-lethal pass vs bolt-player | 0.0000 ± 0.0000 | 0.0000 ± 0.0000 |
| blue.counter-lethal-bolt pass vs counter | 0.0000 ± 0.0000 | 0.0000 ± 0.0000 |
| white.emergency-moat pass vs cast-moat | 0.0000 ± 0.0000 | 0.0000 ± 0.0000 |
| white.avoid-redundant-moat pass vs redundant-moat | 0.0168 ± 0.0009 | 0.0000 ± 0.0000 |

Every exact-zero pair stayed exactly zero across 128 paired worlds at
double the horizon, and the one White pair that had a clean nonzero signal
at H=12 lost it at H=24. Doubling the horizon destroys signal (dominant
positions converge to the same outcome) rather than revealing it. The
notebook's "exact-zero pairs are primarily a horizon test" is refuted; do
not spend the H=24/K=512 escalation as planned.

### Diagnosis: continuation healing

`red.bolt-face-lethal` is the smoking gun. Pass versus Bolt-to-face in a
lethal position scoring identically in all 128 worlds is only possible
because the actor-mirror continuation casts the Bolt itself at the next
priority. A forced single-step deviation is immediately corrected by the
continuation policy, so "pass vs cast-X" probes measure a delay of one
priority step — approximately zero by construction. The same healing
explains develop-bears, emergency-moat, establish-millstone, and
mill-before-draw. The reference is not mislabeled; it is answering a
different question than the corpus intends.

Two fixes, either sufficient: (a) persistent-deviation semantics — a
forced Pass yields for the entire phase/priority window, making "cast this
turn vs not this turn" the measured contrast; or (b) plan-level candidate
definitions that are irreversible at the root (targets, attack subsets
after all declarations, stack responses that resolve before the next
choice). One caveat: `blue.counter-lethal-bolt` at zero cannot be fully
explained by healing — if the Bolt resolves lethally on a pass, the
branches cannot converge unless Blue also loses every counterfactual world
within the horizon. Trace that fixture at unit level before trusting the
sampler; a candidate-application bug would look exactly like this.

### The self-agreement caution, now with numbers

Actor-deployed scores top-1 100%, regret 0.0001 against a reference that
is its own deeper search — partially tautological. Handcrafted scores
*worse* on every probe metric (regret 0.0039, pair agreement 91.7%) while
winning ~59% of real games. The instrument currently cannot see whatever
Handcrafted does better; the corpus covers hand-authored "known-tricky"
moments, not the states where Learned actually loses games.

### Priorities

1. Fix deviation semantics (or re-author candidates to be root-irreversible)
   and unit-trace the two lethal fixtures before running the K=8/H=4
   teacher comparison — teacher labels from a healing reference are mostly
   uninformative, and a G1 trained on them would inherit the blind spot.
2. Harvest the next probe batch from real games: states where
   learned-value and learned-actor disagree, and decision points from
   games Learned lost to Handcrafted (state snapshot only — no
   Handcrafted labels), then label with the fixed reference.
3. The Green critic bias (+0.49 selected-action, ECE 0.49) is the one
   concrete, already-actionable defect the instrument has produced; keep
   it on the G1 scorecard.
4. Keep the K=2-over-raw acceptance narrow as recorded — it is supported
   on Red (62.5%→100% stable-pair agreement) and unfalsifiable elsewhere
   for lack of stable pairs.

## 2026-07-24 23:09 PDT

State reviewed: commit `9eff4b9` plus in-flight uncommitted work — new
`LearnedSearchConfig`/deep-reference declarations in `game.hpp` (no
`game.cpp` implementations yet) and the "Deck-balanced deep-reference
instrumentation" preregistration in EXPERIMENTS.md.

### Verdict

The instrumentation design is exactly right and addresses every priority
from the previous entry: stable per-probe/world seed derivation (not
iteration order), world-major sample pairing across candidates, critic
bootstrap at the 12-turn horizon, low-margin escalation to 512 worlds,
predeclared stable-pair thresholds, hidden-repartition invariance checks,
and Handcrafted probe scorers explicitly fenced as evaluation-only. The
explicit statement that the 16-fixture corpus can reject but never accept a
candidate, with a sealed 200+ decision corpus required for milestone
claims, is the correct epistemics. Tree still builds `-Werror`-clean;
45+10+7 tests pass with the declarations in place.

### Independent experimental data: honest-baseline table completed

Frozen actor model (training seed 424242), 200 paired games per seed:

```sh
./build/alpha-sim --benchmark --games 5 --seed <eval> --train-seed 424242 \
  --challenger learned-actor --baseline handcrafted --train-games 800
```

| Matchup | 424242 | 101 | 707 | Pooled |
| --- | ---: | ---: | ---: | ---: |
| learned-actor vs Handcrafted | 37.5% | 41.5% | 44.5% | 41.2% (247-353) |
| learned-value vs Handcrafted (prior entry) | 40.5% | 45.0% | 44.5% | 43.3% (260-340) |

The actor row reproduces the variance-study matrix exactly, confirming the
frozen-model path is deterministic end to end.

Pooled actor deck slices vs Handcrafted: Green 24.0%/36.0%,
Red 31.3%/54.7%, Blue 54.7%/68.0%, White 54.7%/76.7%. Note the
complementary profiles: value is clearly better at Green (35.3% vs 24.0%)
while the actor is clearly better at Blue (54.7% vs 45.3%). The two
variants fail differently, which makes disagreement states a rich source
for growing the probe corpus, and suggests the eventual search-supervised
generation should be seeded from whichever variant's continuations score
better per deck on the reference — measure, don't assume.

### Critique and priorities

1. The reference's weakest link is its Actor-mirror continuation policy: a
   biased continuation gives biased Q labels, and more worlds cannot fix
   policy bias (the 512-world escalation only shrinks sampling noise).
   Cross-check a sample of reference labels with learned-value
   continuations; where the two references disagree on a stable pair, the
   label is continuation-biased and should be flagged, not trusted.
2. The preregistered gate compares two-world search against the raw policy
   head with the reference as judge — good first use. Make sure the
   deployed two-world configuration is reproduced faithfully in that
   comparison (including any shallow-prior blend the deployed path uses;
   the reference itself correctly disables it).
3. Run the Handcrafted headroom measurement in the same pass:
   `handcrafted_priority_scores` agreement and regret against the
   reference, pooled and per deck. If Handcrafted's regret on Blue is near
   zero, the Blue gate is asking Learned to out-margin a near-optimal
   policy and the card-pool question moves up the agenda.
4. Implementations for the new declarations are still missing from
   `game.cpp`; land them with the invariance tests before any conclusions,
   and keep the probe corpus and scorer out of the training include path as
   currently promised.

## 2026-07-24 23:05 PDT

State reviewed: working tree at commit `0c7675c` plus uncommitted probe
infrastructure, `learned-value`/`learned-actor` split, `--train-seed`,
variance study, and White lock-plan diagnostic.

### Verdict

The workflow correction is real and well executed. The probe-metric design
(same-world-paired Q samples for action-difference standard errors, top-1
agreement, regret, Brier/log-loss/ECE) is the strongest piece of engineering
in this pass. Build is `-Werror`-clean; 45 engine, 10 probe, and 7
probe-eval tests pass.

### Independent experimental data: champion restoration is refuted

The notebook declared the restoration hypothesis without results, so the
reviewer ran the predeclared experiment. One frozen model (training seed
424242), 200 paired games per evaluation seed:

| Matchup | 424242 | 101 | 707 | Pooled |
| --- | ---: | ---: | ---: | ---: |
| learned-value vs Handcrafted | 40.5% | 45.0% | 44.5% | 43.3% (260-340) |
| learned-value vs learned-actor | 48.5% | 50.0% | 50.0% | 49.5% (297-303) |

Pooled deck slices vs Handcrafted: Green 35.3%/28.0%, Red 31.3%/42.0%,
Blue 45.3%/74.7%, White 61.3%/82.0%.

The predeclared eight-point gap over the actor did not materialize; the two
architectures are statistically indistinguishable. Blue — historically the
champion's best slice at 66-76% — is now its worst. Counterspell decisions
are exactly where reading the opponent's real hand helps most, so a large
share of the historical 54.1% estimate was probably hidden-information
leakage in the old rollout. There is no clean champion to restore; the
honest baseline for both architectures is ~41-43%. Full details were
recorded in EXPERIMENTS.md.

### Critique and priorities

1. The probe loop is not closed. `probes.cpp`/`probe_eval.cpp` link only
   into test binaries; there is no CLI that labels the 16 fixtures with the
   64-world reference and scores a trained model. Until that exists,
   iteration still depends on 200-game screens the variance study proved
   inadequate. The labeler and metric evaluator both exist — this is wiring,
   not research. Highest priority.
2. While wiring it, score Handcrafted against the same reference to measure
   metagame headroom. Blue at 74.7% suggests Handcrafted may be near the
   ceiling on that deck.
3. Sixteen probes is a v1. Per-deck metrics over four fixtures are coarse;
   harvest new probes from states where learned-value and learned-actor
   disagree, which the new direct benchmark makes easy to collect.
4. With honest baselines, frozen-model evaluation, and probe metrics in
   place, begin iterated search supervision measured
   generation-vs-generation. That is now the only live path to the gate —
   do not spend further runs tuning either ~43% baseline in place.

Process note: declaring the predeclared experiment in the notebook before
running it made independent execution trivial. Keep that pattern.
