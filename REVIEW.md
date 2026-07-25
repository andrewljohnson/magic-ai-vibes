# Independent Review Log

A second agent maintains this file as a running external review of bot-strength
work. Entries are timestamped, newest first. Each entry critiques the latest
state of the repo and, where possible, contributes independently generated
experimental data. Numbers reported here come from actual runs of the checked-in
binary, never from extrapolation.

## Status at a glance

*Updated 2026-07-24 23:38 PDT — refreshed at the top of every review cycle.*

- **Distance to goal:** far. Honest frozen-model baselines are ~41–43% vs
  Handcrafted (gate needs >50% with confidence, in every deck). The old
  54% "champion" turned out to be inflated by hidden-hand peeking.
- **Mixed-field lift gate:** 1 of 4 decks passing. Green passes decisively
  (Learned +41.7pp over Random vs Handcrafted's +20.0). Red and Blue fail
  by real margins; White fails by 1.6pp, a statistical tie at 60
  games/cell (±13pp). Matches the paired-benchmark profile: the value
  variant is genuinely strong on Green, behind elsewhere.
- **What Codex is doing now:** building the offline probe instrument
  (deep-reference labels for held-out decisions) that will guide an
  iterated search-as-teacher training loop — the current best path to the
  gate.
- **Latest review verdict:** harness works and reproduces exactly, but it
  has a structural blind spot ("continuation healing" — most probe
  comparisons score zero because the rollout policy redoes the action it
  was forced to skip). Flagged as must-fix before training on its labels;
  one possible sampler bug (`blue.counter-lethal-bolt`) sent for tracing.
- **Watch next:** whether Codex fixes the probe semantics before running
  the K=8/H=4 teacher comparison, and whether the first G0→G1 training
  generation shows real improvement on fixed labels.
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
