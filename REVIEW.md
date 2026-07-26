# Independent Review Log

A second agent maintains this file as a running external review of bot-strength
work. Entries are timestamped, newest first. Each entry critiques the latest
state of the repo and, where possible, contributes independently generated
experimental data. Numbers reported here come from actual runs of the checked-in
binary, never from extrapolation.

## Status at a glance

*Updated 2026-07-25 21:12 PDT — refreshed at the top of every review cycle.*

- **P1R rejected: the outcome signal overcorrects.** With the
  optimizer fixed (all mechanism gates passed), the learned residual
  holds indiscriminately — inverting even the live Force Spike. The
  question is now foundational: FT128 (declared) tests whether
  full-game counterfactuals contain the payable/live distinction at
  all. Seven treatments rejected on the defect; each narrowed the
  space.
- **W-family A/B: gate FAIL at 49.6% — and maximally informative.**
  Three deck slices identical game-for-game between weighted and
  uniform sides: with the priority head untrained, behavioral
  weighting is a near-no-op, exactly as theory predicts. Family HOLDS
  (no tuning) until the first accepted P-family head makes the
  likelihoods nondegenerate, then the A/B reruns. Eighth preregistered
  rejection on the broader defect; the composition thesis stands.
- Also delivered to Codex: commit `6a1c898` fails its own make test in
  pristine checkout — recommend atomic script+code commits.

- **Teacher audit now permanently reproducible** — the CLI reproduces
  the retired harness to six decimals (reviewer-verified). Orthogonal
  track pivoted honestly: pure distillation withdrawn, W-family
  (behavior-consistent world sampling, deployment-only) declared.

- **Both purist paths are now closed by evidence:** the value-side 2×2
  (all four cells rejected) and pure search distillation (teacher
  audit: the K=256 teacher confidently prefers the dominated action in
  both option-value probes; deeper search widens the error). The
  defect is invisible to state values AND to the search teacher.
- **Active mainline: outcome-tilted priority residual (P-family)** —
  bounded ±0.10 advantage-weighted correction from the recipe's own
  game outcomes over frozen S0 scores, zero-init identity (P0 ≡ S0),
  P1/P4 mechanism checkpoints, full five-rung promotion ladder
  attached. The only signal left that can know what search cannot:
  what actually wins games.
- Lift gate: 3 of 5 (v2 baseline; Red −3.7, Blue −2.5).

- **ENVIRONMENT v2 (user-directed):** Blue runs Power (Ancestral, Time
  Walk, Mox Sapphire, Sol Ring, Braingeyser) plus Force Spike and Air
  Elementals; Red is a curve deck; seven new cards; schema v2
  fail-closed; 129 tests + ASan/UBSan clean. All prior champion and
  milestone claims are noncomparable. Blue/White 90.4% random
  imbalance recorded honestly with a full-matrix guard.
- **Lift gate (v2 baseline): 3 of 5** at default deployment — Green,
  White, and RU pass (RU flipped!); Red −3.7 and Blue −2.5 are the
  narrowest misses the default view has ever shown, before any
  retraining in this world.
- **ROOT CAUSE CONFIRMED + FIX PREREGISTERED:** the Value critic is
  context-blind — bit-identical features between "pass and retain
  priority" and "pass and a lethal spell resolves" (independently
  verified). Codex's staged 2×2 retrain (context features × dense
  decision-root traces, bit-identical S0 control, K=8 throughout) is
  the strongest experimental design of the project and targets the
  last defect class directly.
- **Program history in one line:** the challenger recipe (bootstrapped
  targets → 16 generations → search-on collection → K=8) beat
  Handcrafted at 95% confidence on virgin seeds in two prior
  environments, beat the legacy champion on every deck (verified
  bit-for-bit by both agents), and its lift table reached 4 of 5
  before the v2 reset; full detail in the timestamped entries below.
- **Top program risk: environment churn** — three world-resets today;
  an explicit version/freeze policy is recommended so a champion
  promotion can complete inside one world.

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

## 2026-07-25 21:12 PDT

FT128's CLI (`--diagnose-terminal-credit`, K=1024/H=128, fail-closed
terminality) landed in the committed binary; no result recorded yet.
The reviewer has launched the preregistered command verbatim — the
independent first run, per the pattern that served the seed-909
champion screen. Result in the next entry. Lift table
deterministic-identical (3 of 5).

## 2026-07-25 21:03 PDT

No-result cycle: FT128 implementation committed (`31ecce6`) with its
fail-closed terminality accounting (every continuation must end in a
natural game result — a single bootstrapped sample invalidates the
run), but the audit itself has not been recorded. Lift table
deterministic-identical (3 of 5). W-family disposition (gate fail at
zero-head, held pending a trained priority head) is on the dashboard;
the FT128 verdict remains the program's single pending decisive event.

## 2026-07-25 20:52 PDT

No-change cycle: FT128 still implementing, no new commits or claims.
Lift table deterministic-identical (3 of 5). W-family A/B continues.

## 2026-07-25 20:44 PDT

No-change cycle: FT128 implementation continues (no results, no new
commits). Lift table deterministic-identical (3 of 5). The first live
W-family A/B continues at seed 515151. Both decisive results — FT128's
does-terminal-credit-contain-the-signal verdict and the W-family's
first gate — remain the next events.

## 2026-07-25 20:36 PDT

No-change cycle for Codex claims: FT128 implementation continues. Lift
table deterministic-identical (3 of 5).

W-family engineering update worth relaying: the A/B's earlier
mirror-identical result exposed that the first instrumented sampling
block was dead code for the C16 deployment — the live path (with
residual/shallow-prior handling) sampled worlds separately. The
weighted branch now sits on the live path, verified activating
(diverging seat behavior) with zero ASan/UBSan findings; the earlier
segfault did not reproduce under sanitizers and is now attributed to
the crashed run's environment rather than the (then-unexecuted)
weighted code. The first genuine A/B at seed 515151 is running.

## 2026-07-25 20:19 PDT

No-change cycle: FT128 implementation continues (no results yet); no
new commits. Lift table deterministic-identical (3 of 5). W-family A/B
still computing — the weighted deployment's per-decision candidate
scoring makes it several times slower than a standard screen; a
performance note will accompany the result (deployment cost is itself
a gate-relevant property for the W-family).

## 2026-07-25 20:12 PDT

No-change cycle for claims: FT128 implementation continues. The
script-skew finding is now CONFIRMED by pristine checkout — commit
`6a1c898` fails its own make test (exit 2) with all compiled suites
passing and only the CLI script's uncommitted-code assertions failing.
Priority for Codex stands: commit script and code atomically so every
commit is self-consistently green; this matters because both trees'
verification protocols pin to commits. Lift table
deterministic-identical (3 of 5). W-family A/B still running.

## 2026-07-25 20:04 PDT

No-change cycle for claims: FT128 remains declared-not-run
(implementation in progress). Verification closed this cycle: the
teacher audit in the challenger worktree is exactly deterministic
across repeated runs (0.070191 ± identical SE/CI both times, matching
canonical values), so the earlier cross-invocation discrepancy was
config difference between script steps, not nondeterminism. Lift table
deterministic-identical (3 of 5). W-family A/B and the pristine-commit
check still in flight.

## 2026-07-25 19:52 PDT

**P1R rejected — and the rejection is the most scientifically
interesting yet.** The revised optimizer fully solved the fit problem
(KL −61.9%, signed movement 90.5%, saturation 0.4% — all mechanism
gates passed), which isolates the new failure as pure signal quality:
the absorbed outcome credit OVERCORRECTS toward holding. P1R now
passes even on the LIVE Force Spike (0.1513 vs 0.0870 when the tax is
unpayable — inverting a decision the frozen model had right), and RU
regret ballooned +0.0586. Terminal advantage apparently cannot
distinguish "hold when the tax is payable" from "hold always" at this
signal density. Codex's FT128 declaration asks the right next
question: do full-game counterfactuals (both branches played to
termination) contain the discriminating signal at all? If they do
not, no outcome-credit recipe can work and the family is dead on
principle rather than on tuning.

**Process finding for Codex (from the orthogonal track's
verification):** commit `6a1c898`'s test_cli.sh asserts output markers
of code that is only in the uncommitted working tree — the committed
state fails its own `make test` in a pristine checkout. All engine and
unit suites pass at the commit; only the CLI-script skew fails.
Recommend committing script and code atomically so any commit is
self-consistently green; the reviewer has excluded the skewed script
from the worktree's green criterion meanwhile. (Also verified: the
teacher audit is deterministic in the worktree and reproduces
canonical values exactly.)

W-family: the same-policy guard now recognizes per-seat weighting
(same aliasing class as the old C16/G0 bug, fixed and committed); the
preregistered A/B at virgin seed 515151 is re-running.

Lift table deterministic-identical (3 of 5).

## 2026-07-25 19:05 PDT

**P1's failure is solved as a diagnosis: optimizer underfit, proven
three ways.** The fixed-shard matrix shows the canonical E8/R0.001
achieving 1.18% KL reduction while E128/R0.001 clears the original 30%
gate (30.93%, zero saturation), E512 reaches 71.3%, and E128/R0.003
hits 61.9% — monotone in budget, on every deck, with the numerical
oracle certifying that residual geometry alone could achieve 99.16%.
The interference hypothesis is explicitly refuted by its own
predeclared plateau criterion. This confirms the 18:20 review's
"underfit is the boring but likely candidate" with far better evidence
than the review suggested was obtainable.

**P1R is correctly preregistered as a minimal-change successor:**
exactly two optimizer fields move (epochs 8→128, lr 0.001→0.003),
everything else pinned to the immutable P1 shard, and the offline
behavioral gates (dev-v3 non-regression, Counterspell, live/payable
Force Spike flip, X=0) stand between P1R and any gameplay. The
validation-cache identity correction (regenerating the v1 cache under
the exact-environment Actor fingerprint) is the right pedantry.

W-family status: per-seat flag + benchmark CLI committed; the
preregistered same-model A/B at virgin seed 515151 is running.

Lift table deterministic-identical (3 of 5).

## 2026-07-25 18:53 PDT

P1 capacity diagnosis preregistered: a fixed-shard epochs ×
learning-rate matrix plus a zero-saturation numerical oracle, with
interpretation rules declared per outcome pattern (underfit vs step
size vs interference vs inconclusive) and the explicit rule that no
cell is accepted post hoc as a new recipe. The rejected P1 thresholds
stand.

Corrections accepted from Codex's audit of the 18:20 review note: P1
used eight optimizer epochs (not one) and retained 32 roots per
actor-game (not per game), and the original shard was not persisted —
so the reviewer's "measurable from recorded artifacts alone"
suggestion was wrong on availability; deterministic shard recollection
is the right substitute. Duly corrected here.

W-family status: increments 2+3 committed (observation hook +
deployment flag, bit-identical off-path), A/B preregistered at virgin
seeds 515151/626262. Next increment moves the flag per-seat so the
same-model A/B can run.

Lift table deterministic-identical (3 of 5).

## 2026-07-25 18:35 PDT

No-change cycle: the P1 measurement-only diagnosis is in progress (11
files in flux, no new notebook sections since the rejection). Lift
table deterministic-identical (3 of 5).

Orthogonal track progress for the dashboard: W-family increment 2
landed — each player's last public priority action is now recorded at
the engine's priority chokepoint, outside GameState (all 116 engine
tests unchanged, proving behavior neutrality). The deployment flag is
the next increment; after that, the W-family's uniform-vs-weighted A/B
becomes runnable and could matter for the same counter-war/tax
decisions the P-family is fighting.

## 2026-07-25 18:20 PDT

**P1 rejected at the mechanism gate — the cheapest possible failure.**
The AWR fit ran to completion with exact accounting (schedule, roots,
bit-identical non-Priority heads, per-deck conflict coverage,
saturation all passing), but the two learning-effect gates failed
decisively: newest-shard KL fell 1.18% against a preregistered minimum
of 30%, and signed chosen-probability movement was 54.94% against a
required >60%. The residual head is nearly inert under the declared
optimizer/budget. Correctly, no probes were scored, no P4 trained, no
gameplay run — and the thresholds stand. Next per preregistration: a
measurement-only diagnosis distinguishing optimizer underfit,
shared-feature interference between contradictory roots, and objective
error, before any new recipe is hypothesized.

Reviewer's observation for the diagnosis: with 40 games/generation, 32
roots/game, one epoch, and a ±0.10 bounded residual, the gradient
budget per legal action is tiny — underfit is the boring but likely
candidate, and it is measurable without retraining (loss-curve slope
and gradient norms on the recorded shard). Interference would show as
per-deck KL movement anticorrelated across strata. Both are
distinguishable from the recorded run artifacts alone.

Today's tally on the hold-versus-spend defect: S1, D0, D1, pure
distillation, and now P1's first fit — five rejections, every one
cheap, preregistered, and informative. The defect is the hardest
problem the project has faced; the process is handling it exactly
right.

Lift table deterministic-identical (3 of 5).

## 2026-07-25 18:14 PDT

No-change cycle: P1 implementation still in flux (same 8 files, no new
notebook sections). Lift table deterministic-identical (3 of 5).

## 2026-07-25 18:10 PDT

No-change cycle: P1 implementation continues (8 files in flux, no new
notebook sections since the execution-identity declaration). Lift
table deterministic-identical (3 of 5). Both tracks proceeding: P1
training on Codex's side; W-family engine hook and deployment flag on
the orthogonal track's next wakeup.

## 2026-07-25 18:05 PDT

Declaration-only cycle: P1's canonical execution identity is now fully
preregistered — exact schedule/root/rollout accounting with no
rootless actor-game, bit-identical critic and non-Priority heads,
per-deck advantage and search/outcome-conflict coverage, newest-shard
KL down ≥30%, signed chosen-probability movement >60%, saturation <5%
— with the explicit framing that a completed run missing a threshold
is a valid rejection, not a process error. The mechanism gates come
BEFORE any probe scoring, which comes before P4. Nothing to critique;
P1 training is the next event.

Orthogonal track update (for the dashboard): the W-family core landed
green in the challenger worktree — behavior-consistent world sampling
with rules-level illegality exclusion and priority-head likelihood
weighting, deployment-only, four unit tests. Notable discovered
property: even an untrained head discriminates on a Pass observation
(1/|legal actions| — option-rich hands are downweighted). The W and P
families share the same priority head and compose by construction.

Lift table deterministic-identical (3 of 5).

## 2026-07-25 17:35 PDT

Declaration-only cycle: before P1 collection, Codex fixed the
P-family's mechanism-metric definitions — residual saturation rate
(tanh magnitude ≥ 0.95, weighted so high-action-count roots can't
dominate), separately reported positive/negative advantage weights per
deck with a required nonzero search/outcome conflict share, and exact
seat-game/root accounting with nothing silently normalized. Fixing
these before data exists prevents the mechanism diagnostics from
becoming post-hoc knobs — the same pre-commitment discipline that has
held all day. No results yet; P1 collection is next.

Lift table deterministic-identical (3 of 5).

## 2026-07-25 17:20 PDT

The 17:10 entry's priority 1 was closed within minutes: the permanent
`--diagnose-force-spike-teacher` CLI landed, and the reviewer's
verbatim run reproduces the retired one-off harness to six decimal
places on every row — deltas, paired SEs, intervals, and K=8 block
counts (live +0.070191 PASS; payable −0.043767 FAIL 0/32; X=0
−0.119422 FAIL 0/32), against the same S0 fingerprint. The teacher
rejection is now independently reproducible on demand. Both trees'
verification story is airtight; P-family training proceeds per its
preregistration.

Also noted: the orthogonal track withdrew its pure-distillation P16
preregistration (falsified by this audit, as its own entry gate
required) and declared the W-family — behavior-consistent world
sampling, deployment-only, composing with all model families. Details
in CLAUDE-PLAN.md.

Lift table deterministic-identical (3 of 5).

## 2026-07-25 17:10 PDT

**Teacher audit: rejected, decisively and fast (4.77s).** At K=256/H=4
the frozen search teacher passes the live counter (+0.070, 32/32 K=8
blocks) but confidently prefers the dominated action in BOTH
option-value comparisons: payable Spike (−0.044 for Pass, 0/32 blocks)
and X=0 (−0.119 for Pass, 0/32). Deeper horizon widens the wrong
margins; the Actor reference fails at every K/H tried. Conclusion
verified in the numbers: cross-entropy distillation of this teacher
would make the two known failures MORE confident. Pure P16
distillation is dead before it was fit — the cheapest kill of the day.

**The escalation is well designed: outcome-tilted priority residual.**
S(a) = Q_parent(a) + 0.10·tanh(centered logit), advantage-weighted
regression from the recipe's own game outcomes, frozen parent
throughout, no weight sweep, and a zero-init identity guarantee (P0
must be bit-identical to S0 in actions, scores, and game records).
P1/P4 are mechanism checkpoints, not selectable endpoints, and the
full five-rung ladder (919191 → 271828 → 314159 → eight seeds → lift
gate) is attached. This is the "self-generated outcome signal" branch
the audit's decision rule demanded — outcomes know what the search
teacher cannot.

Priorities:
1. Land the permanent CLI reproduction of the teacher audit before P1
   training — the one-off harness was removed, so the rejection is
   currently unreproducible by a second party (this reviewer included).
2. The known historical risk for outcome-weighted policy signals is
   the four-deck-era actor collapse; the bounded residual over a
   strong value prior is the right mitigation, but watch the P1/P4
   checkpoints for early divergence on the two named probes rather
   than waiting for P16.

Lift table deterministic-identical (3 of 5).

## 2026-07-25 17:01 PDT

**The 2×2 is complete and fully rejected — and that is a landmark
result, not a setback.** All four cells scored against the frozen
controls with exact ordered attribution:

| Cell | Mean regret | Critic Brier | Payable-tax flip |
| --- | ---: | ---: | --- |
| S0 (control) | 0.0085 | 0.0528 | fail |
| S1 (context) | 0.0061 | 0.0711 | fail |
| D0 (dense) | 0.0032 | 0.0954 | fail |
| D1 (dense+context) | 0.0032 | 0.0985 | fail |

Dense collection halved pooled regret and fixed the RU land probe, but
the hold-versus-spend defect survived every value-side treatment: the
hypothesis "context representation + forced-pass data suffices" is
falsified with four fingerprinted artifacts. The value axis is
exhausted; the defect is an action-preference/teacher-target problem.

**Codex has adopted the policy-head path** (the orthogonal track's P16
preregistration) with exactly the right first step: a teacher
sufficiency audit — before distilling anything, prove the K=256/H=4
search teacher itself prefers Pass in the payable state. If the
teacher carries the defect, distillation would faithfully reproduce
it, and P16 must add a self-generated improvement signal
(counterfactual outcome advantage or recursive search). The
conjunctive decision rule is preregistered. This is the correct
convergence of the two tracks: value side closed by exhaustion, policy
side now mainline with a falsifiable entry gate.

Lift table deterministic-identical (3 of 5). Reviewer will execute the
teacher audit verbatim when its implementation commits.

## 2026-07-25 16:46 PDT

No-change cycle: D0/D1 dense-cell implementation continues in the
working tree (HEAD `19b7c61` remains mid-implementation — dense
challenger enums declared but unhandled, so the commit is not yet
green). Lift table deterministic-identical (3 of 5). Orthogonal P16
track synced to main and correctly holding for a green base.

## 2026-07-25 16:40 PDT

No-change cycle: no new notebook sections since the S1 rejection;
D0/D1 dense-cell implementation in progress (6 files in flux). Lift
table deterministic-identical (v2 baseline, 3 of 5; Red −3.7, Blue
−2.5). Orthogonal track remains blocked on main's next green commit;
P16 preregistration stands.

## 2026-07-25 16:35 PDT

**S1 rejected by its own gates — and the rejection is informative.**
The sparse/context-live cell improved pooled regret (0.0085 → 0.0061),
fixed the Green Giant Growth selection, and kept Blue at zero regret —
but failed the Force Spike payable flip (still casts into a payable
tax) and blew the White nonregression limit (+0.0246 regret, Brier
+0.0183). Codex's reading is precise: the context is representable,
but sparse collection contains too few forced-pass successor roots for
the critic to learn what passing buys. Per preregistration, D0/D1
(dense trace cells) are next, with no gameplay-seed selection. The 2×2
is functioning exactly as designed — one axis at a time, reject-only,
every artifact fingerprinted.

Also recorded: a learned-pilot deck-evolution route (engineering smoke
only) — evolution can now use a Learned pilot instead of Random, which
will matter later for metagame work.

Side note for the program: S1's failure on the payable flip supports
the orthogonal policy-head bet (the branch's P16 preregistration) —
if dense data (D1) also fails the flip, the action-preference path
becomes the leading candidate; if D1 passes, P16 becomes a
composition test. Either way the two tracks now bracket the defect.

Lift table deterministic-identical (3 of 5). Trees actively changing;
committed-state verification only.

## 2026-07-25 16:10 PDT

The paired live/payable Force Spike report ran (`c53ce34`), and it
sharpened the picture: both Value G0 and frozen S0 C16 cast Force
Spike in BOTH states — correctly when Red is tapped out, wastefully
when Red can pay the tax (C16 payable: Spike 0.2022 vs Pass 0.1534).
The behavioral gate is honestly marked failed for both controls. This
converts the S1 success condition into a single crisp behavioral flip:
prefer Pass in the payable state while retaining the live-state cast.
It is the cleanest operationalization of the hold-versus-spend defect
so far — better than the X=0 metric because both branches are
one-mana decisions on the same stack object.

Lift table deterministic-identical (3 of 5). S1 training remains the
next event; no new priorities.

## 2026-07-25 16:07 PDT

Two 2×2 prerequisites completed and recorded since the last entry
(S1 implementation continues, 14 files in flux):

1. **S0 control frozen** — the exact preregistered command published
   the state-only C16 control artifact (fingerprint `bda1ea44…`) and
   correctly discarded its 60-game smoke as strength evidence (23-37
   at n=60 is noise; the CLI's inconclusive exit was the right
   verdict). S0 is now the immutable comparison point for every 2×2
   cell.
2. **Force Spike offline audit passed** — the deployed state-only
   learner already selects Force Spike when the target cannot pay
   (100% top-1 on all four Blue fixtures), and the first attempt
   failed closed on a stale pre-expansion label cache, which is the
   schema machinery working as designed. Codex's own follow-up (a
   paired live/payable two-state report, required of every context
   candidate) closes the one gap in the fixture.

Lift table deterministic-identical (v2 baseline: 3 of 5; Red −3.7,
Blue −2.5). No priorities to add — the program is executing its own
preregistrations cleanly; reviewer's role is now verbatim verification
at each 2×2 cell and ladder rung.

## 2026-07-25 15:40 PDT

The 2×2 preregistration is complete and answers the 15:37 entry's
priority 2 exactly: cell selection is offline-only (S1 tried first;
D0/D1 only if S1 fails its gates; D1 must beat both D0 and S1 on
pooled regret, critic loss, and the pass-sensitive stratum; "do not
choose a cell based on one gameplay seed"), followed by a fully
predeclared promotion ladder — reject-only screen at seed 919191,
2,040 games vs S0 at 271828, 2,040 vs Handcrafted at 314159, then the
eight-seed panel and the mixed-field lift gate. Handcrafted is never
consulted during treatment selection. This is the promotion-grade
pipeline the project has been converging toward all day; nothing to
critique in the design.

Implementation in progress (6 files). Lift table
deterministic-identical to the 15:37 v2 baseline (3 of 5; Red −3.7,
Blue −2.5). Reviewer stands ready to execute each ladder rung verbatim
as it is reached.

## 2026-07-25 15:37 PDT

State reviewed: environment v2 (user-directed) — Blue gains Power pieces
(Mox Sapphire, Sol Ring, Ancestral Recall, Time Walk, Braingeyser),
Force Spike, Flying Men, Air Elementals; Red restructured into a curve
deck; seven new cards through the normal mana/priority/stack paths;
schema v2 fail-closed; 129 tests + ASan/UBSan clean. Plus: the
Counterspell probe audit passed (C16 100% top-1 on all Blue fixtures),
and the context-aware retrain is preregistered as a staged 2×2.

### Verdict

Three things deserve specific credit. (1) The Blue/White 90.4% random
imbalance is recorded honestly with a full-matrix regression guard
instead of quietly nerfing the requested lists. (2) The 2×2 design
(S0/S1/D0/D1: context × trace density) with zero-initialized appended
context weights and a bit-identical S0 guarantee is the strongest
experimental design of the project — it isolates both suspected root
causes without confounding them. (3) The context feature set is
minimal and clean (context bit, phase one-hot, relative priority, pass
count, sorcery bit — nothing card-specific).

### Lift table (seed 4242, environment v2, NEW baseline)

| Deck | Learned lift | Best rival | Verdict |
| --- | ---: | ---: | --- |
| Green | +41.2 | +33.8 (HC) | PASS |
| White | +37.5 | +35.0 (HC) | PASS |
| RU Aggro | +46.2 | +36.2 (HC) | PASS (flipped!) |
| Red | +62.5 | +66.2 (HC) | FAIL by 3.7 |
| Blue | +45.0 | +47.5 (HC) | FAIL by 2.5 |

3 of 5 at default G0/K=2 deployment. NOT comparable to any prior
table. Notable: RU passes in the new metagame, while the two misses
(Red, Blue) are both under 4 points — the closest the default view
has ever been to the crown, before any retraining in this world.

### Priorities

1. Environment churn is the top program risk again: this is the third
   world-reset today, and each one voids the promotion ladder
   mid-flight. Strongly recommend an explicit environment-version
   policy: batch card additions into scheduled releases, freeze
   between them, and only run champion promotions inside a freeze
   window. Otherwise "Learned is king" keeps being redefined faster
   than it can be proven.
2. The 2×2's four training cells at T=800/G16 are ~15 min each plus
   screens — predeclare the cell comparison gates (which deltas at
   what sample) before results arrive, so the 4-way readout doesn't
   invite post-hoc selection.
3. Extra-turn (Time Walk) and floating-mana states are new
   probe-corpus territory — the harvest pipeline should collect from
   the new mechanics before the next corpus version.

## 2026-07-25 15:10 PDT

No-change cycle: diagnostic work committed incrementally (`cce09a6`),
no new notebook claims since the context-alias demonstration the
reviewer verified at 15:07. Lift table deterministic-identical (2 of 5
default; 4-of-5 behind C16/K=8 flags). Awaiting two preregistrations:
the context-feature retrain (RU root-cause fix) and the promotion
ladder's virgin-seed run.

## 2026-07-25 15:07 PDT

Independent verification of the value-context diagnostic (reviewer ran
`--diagnose-value-context` from the built binary):

- **Context alias DEMONSTRATED.** In the paired lethal-stack contexts,
  the critic's state features are bit-identical while the rules engine
  proves the transitions differ maximally — pass at count 0 retains
  priority (root at 3 life, stack 1); pass at count 1 resolves the
  spell (root at 0, lethal). The First/Second Main pair aliases the
  same way. The neutral policy/action encoder distinguishes both
  pairs, and opponent hidden-card substitution remains bit-identical —
  the missing information is representable without any isolation
  breach.
- This is the deepest root cause found yet: the deployed Value critic
  cannot express the consequence of passing at all. It likely explains
  the Q(Pass)−Q(X=0) defect (and why evaluation noise made it worse),
  and bounds what ANY training-signal fix could have achieved. The
  epsilon failure is now fully explained rather than merely observed.
- Per the preregistration, the licensed next step is neutral
  phase/relative-priority/pass/sorcery context features plus dense
  decision-state traces, separately preregistered before retraining.
  Reviewer's one caution: that retrain changes the critic input schema
  — version the model artifacts (the fail-closed machinery exists) and
  expect probe-dev-v3 labels to need regeneration if the reference
  critic changes.

Lift table: default unchanged (2 of 5; 4-of-5 behind C16/K=8 flags).
Promotion ladder virgin-seed run still undeclared — flagged again as an
independent track that should proceed in parallel.

## 2026-07-25 14:50 PDT

Codex declared the follow-up to the epsilon failure: a structural
diagnostic (`--diagnose-value-context`) testing whether the Value
critic's feature vector aliases states that differ only in
`consecutive_passes` (spell resolves vs priority retained) or phase
(First vs Second Main). If confirmed, the critic literally cannot
represent the consequence of passing — a clean root-cause candidate for
the Q(Pass)−Q(X=0) defect that survives both failed treatments
(evaluation noise, and implicitly the training-signal-only theory: even
credited holds couldn't be learned if the state is unrepresentable).
This is the right escalation: representation before signal before
noise. Implementation in progress; reviewer will verify on commit.

Lift table: default unchanged (2 of 5; 4-of-5 behind C16/K=8 flags
pending promotion). Note the promotion ladder's virgin-seed run remains
undeclared — it should not wait on the RU diagnostic; the aggregate
promotion and the RU fix are independent tracks.

## 2026-07-25 14:16 PDT

Codex has preregistered the RU treatment: `--value-continuation-epsilon`
— epsilon-stochastic continuations during search evaluation, a
card-agnostic attack on the Q(Pass)−Q(X=0) option-value defect (a
deterministic mirror continuation can never realize the option value of
holding a spell; a 5% stochastic continuation can). Design is sound and
notably clever in one respect: the causal five-deck screen pits the
IDENTICAL frozen model against itself with only the deployment epsilon
differing (seed 919), isolating the behavioral change with zero
training confound. The probe gate remains reject-only. Implementation
in progress; no results yet.

Default lift table unchanged (2 of 5; the 4-of-5 requires C16/K=8
flags pending promotion).

Priority: unchanged from 14:14 — this epsilon experiment and the
promotion ladder are the two live threads; reviewer will execute both
preregistered commands verbatim when the implementation commits.

## 2026-07-25 14:14 PDT

State reviewed: the port is COMMITTED (`203bd08` — frozen C16
challenger, behavior probes, hardened artifact cache) and Codex's own
run of the preregistered lift screen matches the reviewer's
bit-for-bit: **4 of 5, Blue conquered, RU Aggro the last deck (−5.0)**.
Their notebook also honestly records a preregistration error (60 vs 80
games/cell) as a correction rather than a rewrite, and correctly
declines to dismiss RU as cell noise because the 2,040-game milestone
agrees. C16 is the accepted milestone challenger at commit anchor.

Process note: this cycle hit a transient link failure building the
working tree — Codex was editing concurrently (`7a5fd33` landed
mid-cycle). Reviewer verification will pin to committed states from
here on rather than racing the working tree.

Default-deployment lift table (legacy champion, unchanged): 2 of 5.
The 4-of-5 table requires the C16/K=8 flags until promotion completes
(2,000-game virgin seed → eight-seed panel → default flip).

Priorities:
1. RU learning experiment is the whole ballgame now — awaiting Codex's
   preregistration; reviewer stands ready for verbatim verification.
2. Complete the promotion ladder so the DEFAULT `make run` view shows
   the 4-of-5 table without flags — that's when the user's primary
   view improves without ceremony.

## 2026-07-25 13:40 PDT

No-change cycle: no new Codex notebook entries; the port remains
uncommitted (17 files); lift table deterministic-identical (2 of 5,
committed champion). Replication check: two of three seeds complete and
exact (51.2%/52.5%, matching the branch game-for-game); seed 707 in
flight. Standing recommendation unchanged: commit the port so the
passing seed-909 champion screen anchors to a hash.

## 2026-07-25 13:37 PDT

No new Codex notebook entries since the preregistrations (port still
uncommitted, 17 files). Committed champion's lift table
deterministic-identical: 2 of 5.

Two verification results for Codex:

1. **The seed-909 champion screen PASSED** (reviewer's verbatim
   execution, 13:35 dashboard entry): C16 55.7% over legacy G0, CI
   51.7–59.6, every deck slice ahead including RU. Fingerprints
   distinct. Awaiting Codex's own run for the bit-for-bit agreement
   check.
2. **The port replicates the branch exactly.** Ported C16 vs
   Handcrafted on the branch's seeds: 51.2% (424242) and 52.5% (101) —
   identical to the branch's numbers game-for-game, including RU
   slices (31.2%/35.4%). Seed 707 in flight (branch: 53.3%). The
   independent reimplementation is not merely behaviorally equivalent;
   it appears to consume randomness identically. Gate 6 replication is
   effectively proven.

Priority: with the champion screen passed, the promotion ladder is the
critical path — Handcrafted-seed reproduction is already half-done by
the replication check above; the 2,000-game virgin seed and eight-seed
panel remain. Recommend Codex commit the port promptly so the passing
screen is anchored to a commit hash rather than a working tree.

## 2026-07-25 13:13 PDT

State reviewed: the separate-family port has reached preregistration of
the two decisive experiments — (1) validation-v1 probe scoring of C16,
and (2) the canonical **C16 vs legacy G0** champion screen: seed 909,
600 paired games, equal K=8, pass only if C16 exceeds 55% aggregate AND
wins every deck slice. `learned-value-c16` and the threaded
`--learned-generations`/`--learned-rollouts` flags exist in the built
binary. The tree builds clean; tests pass; port commit imminent.

### Verdict

The preregistration is exactly right — in particular refusing to tune
against the one-state validation corpus on a failure, and requiring the
Handcrafted-seed reproduction plus a 2,000-game virgin seed before the
eight-seed panel. The reviewer has launched the preregistered seed-909
screen verbatim from the built binary as independent verification; the
result will appear in the next entry alongside Codex's own run — same
command, same seeds, so the two runs must agree bit-for-bit if the
port's determinism claims hold. That agreement check is itself a test
of gate 2 (deterministic same-seed artifacts).

### Lift table

Unchanged (2 of 5, deterministic-identical, committed champion).

### Priorities

1. The 55%-aggregate bar for the champion screen is notably stricter
   than the branch's evidence (C16-vs-C2 ran 51.8%; legacy G0 is
   presumably weaker than C2, but 55% plus every-deck is a high bar at
   600 games). If C16 lands at, say, 53% with four slices ahead, the
   predeclared verdict is FAIL — which is fine, but predecide what
   partial evidence feeds the next experiment so a near-miss isn't
   wasted.
2. RU remains the likeliest slice to fail the every-deck condition;
   the validation-v1 probe run may explain why before the champion
   screen forces the question.

## 2026-07-25 13:12 PDT

No-change cycle for claims: the separate-family C16 port continues (17
files in progress) and the working tree builds cleanly again with test
suites passing. Committed champion's lift table
deterministic-identical: 2 of 5.

Challenger results processed since the last entry, for context: G16
lift preview 3 of 5 at K=2 deployment (Blue gap 17.5 → 3.7); C16-vs-C2
depth comparison 51.8% pooled for C16; T=1333 data scaling rejected
(−4.8pp). Full details in CLAUDE-PLAN.md and the dashboard. The
champion-promotion question now waits on the port's legacy-G0
comparison and the K-threaded tournament path.

## 2026-07-25 12:40 PDT

No-change cycle: Codex's separate-family port still mid-flight (17
files, transient build error), no new claims. Lift table
deterministic-identical (2 of 5).

Challenger partials worth flagging: the RU T=1333 treatment's first
seed came in at 46.2% aggregate — five points BELOW the T=800 result
on the same seed (51.2%), with RU itself only +2pp. If the remaining
seeds confirm, naive train-games scaling hurts this recipe (an echo of
the project's oldest lesson: more data only helps when the objective
supports it), and the RU treatment should wait for v3 probe diagnosis
rather than burn more scalar sweeps. C16-vs-C2 depth comparison seed 1:
53.3% for C16.

## 2026-07-25 12:37 PDT

No-change cycle for claims: Codex's separate-family C16 port continues
(16 files in progress, transient build error, gates 1-5 declared
including golden legacy-G0 fingerprint and recipe/generation-keyed model
identity — gate 3 directly fixes the aliasing bug class the branch hit).
Also recorded: the X=0 Disintegrate decision — keep it legal, no
card-specific mask, harvest a hold-vs-X=0 validation state and pursue
generic soft-target/stochastic improvement instead. Correct call: the
fix belongs in the learning signal, not the rules.

Lift table: unchanged (2 of 5, deterministic-identical).

Challenger in-flight progress (partial): C16-vs-C2 depth comparison,
seed 424242: 53.3% for C16 — consistent with the old-environment
G8-vs-G2 gradient. RU T=1333 screen, seed 424242: RU slice 33.3% (vs
31.2% at T=800; within noise so far). Both jobs continue.

## 2026-07-25 12:26 PDT

State reviewed: commits `f2664ea`/`b573cfe` plus in-flight work — an
interactive play mode (`src/interactive.cpp`), RU blunder-harvest probes
under construction (transient WIP compile errors in the working tree,
not a regression), and a declared C16 recipe port with a true frozen G0
comparator.

### Correction: Codex falsified this review's G0 claim — they are right

The 11:51/12:23 entries stated that the challenger branch's explicit
`learned-value-g0` pins the frozen Codex champion. Codex audited this
with fingerprints and falsified it: the branch's modified trainer
changes labels (bootstrap), per-generation game allocation, and search
collection even at two generations, so the branch's "G0" is the new
recipe stopped at G2 — fingerprint `8852833…` versus legacy
`b2eec93…`. The in-flight branch head-to-head is therefore C16 versus
new-recipe C2, a recipe-internal depth comparison — NOT evidence of
superiority over the legacy champion. This entry corrects the record;
the reviewer's error, cleanly caught by the audit-over-assertion
discipline this project has been building. Codex's response — port the
challenger recipe as a separately named model family with legacy G0
golden-fingerprinted bit-for-bit — is the correct design, better than
the branch's parameter overload.

The virgin-seed milestones (55.1% four-deck, 53.5% five-deck vs
HANDCRAFTED) are unaffected: Handcrafted is not a learned comparator
and those runs used no G0 claim.

### Lift table

Unchanged (2 of 5, deterministic-identical; last good binary — the
working tree is mid-edit).

### Priorities

1. Codex's separate-family port supersedes the branch head-to-head as
   the canonical C16-vs-legacy-G0 evidence. The branch run, when it
   completes, should be reported as C16-vs-C2 only.
2. Gate 1 of their port (legacy trainer behavior unchanged, golden
   fingerprint pinned) is the load-bearing piece — everything else
   downstream inherits its validity.
3. The reviewer will verify the ported C16's five-deck screen
   reproduces the branch's 52.4%/53.5% numbers once the port lands —
   same recipe, independent implementation, ideal replication check.

## 2026-07-25 12:23 PDT

State reviewed: probe-dev-v3 landed and passed all five predeclared
engineering gates (20 fixtures, four per deck, complete/legal candidate
sets, healing-regression closures, v2 fail-closed, all-deck metric
coverage). Codex also opened a real-game blunder-harvesting thread from
user-observed errors: Bolt-to-face over Bolt-to-creature/hold, and X=0
Disintegrate — a legal no-op that is the sharpest generic
action-ranking failure yet observed. Both go to deep-reference labeling
without card-specific policy. This is the real-game corpus the reviews
have requested since the first probe cycle — the right response to the
right evidence.

### Challenger: five-deck milestone CONFIRMED

2,040 paired games on virgin seed 404, frozen recipe (16 generations,
K=8): **1091-949 (53.5%), 95% CI 51.3%–55.6% — beats Handcrafted at
95% confidence in the five-deck environment.** Slices: Green
37.3%/34.1%, Red 46.3%/36.3% (the historically unsolvable deck, now
clearly ahead), Blue 70.8%/51.5%, White 75.0%/64.7%, RU Aggro
38.0%/46.1% — the ONE failing slice. Combined with the four-deck
55.1%, the recipe now holds confirmed virgin-seed milestones in both
environments. The remaining distance to "Learned is king": RU Aggro,
and the mixed-field lift gate.

Also recorded honestly: the first C16-vs-G0 head-to-head produced no
data — a model-sharing bug tripped the same-policy guard and grep
swallowed the error. Fixed (explicit-G0 selections no longer share the
challenger's model) and re-running with independently trained frozen
models.

### Lift table

Unchanged (2 of 5, deterministic-identical) — no learned-path changes
in Codex's tree.

### Priorities

1. RU Aggro is now the single deck between the challenger and the full
   gate. Two complementary attacks: Codex's v3 RU probes + X=0
   harvesting (diagnosis), and the challenger's train-games scaling
   with pairing count (treatment). Coordinate so both score the same
   frozen artifacts.
2. When the v3 reference cache is generated, score the challenger's
   G16 artifact alongside the G0–G8 ladder — it is now the strongest
   known model and belongs in the pre-G16 baseline as the comparison
   point, not an afterthought.
3. The lift table still deploys Codex's committed champion. Once the
   head-to-head confirms C16 > G0 directly, the champion-promotion
   question (AGENTS.md champion rule) is live — that decision needs
   the full gate panel, not just these milestones.

## 2026-07-25 11:51 PDT

No-change cycle: no new EXPERIMENTS.md entries; probe-dev-v3
implementation continues (nine files in progress, tests passing). Lift
table deterministic-identical: 2 of 5, Blue −17.5 the confirmed gap.

Challenger status: the decisive pair is in flight — C16-vs-G0
head-to-head (three seeds, equal K=8 deployment, independently trained
frozen models) and a 2,000-game virgin-seed-404 confirmation vs
Handcrafted. A generation-semantics hazard was fixed first: the explicit
`learned-value-g0` name now always pins the frozen two-generation Codex
champion regardless of `--learned-generations`, so the head-to-head
cannot accidentally compare the recipe against itself.

## 2026-07-25 11:37 PDT

Codex: no new notebook entries since 11:19 (probe-dev-v3 implementation
continues, nine files in progress). Lift table deterministic-identical:
2 of 5, Blue −17.5 the confirmed gap.

### Challenger: the recipe transfers to five decks

First screens of the ported recipe on the merged engine (240 paired
games per seed vs Handcrafted, frozen train-seed 424242, K=8):

| Eval seed | Aggregate |
| --- | ---: |
| 424242 | 51.2% |
| 101 | 52.5% |
| 707 | 53.3% |
| Pooled (720 games) | **52.4%** |

Above 50% on every seed in an environment the recipe was never tuned
on. Pooled slices vs Handcrafted: Blue **72.9%/57.6%** (the old-world
Blue transformation carried over fully — directly attacking the lift
table's largest gap), Red 48.6%/41.7% (best challenger Red ever),
White 68.8%/63.2%, Green an exact 39.6% tie, RU Aggro 31.9%/39.6% the
one weak deck (least training data per pairing; Handcrafted has
hand-tuned Disintegrate rules there).

For Codex: this is the pre-G16 baseline's target. The frozen artifacts
live on `claude/challenger` (same engine, same schema) — score them
against probe-dev-v3 when it lands, and the recipe-vs-G0 head-to-head
plus a 2,000-game virgin-seed confirmation will settle whether this
becomes the new champion recipe under the AGENTS.md champion rule.

## 2026-07-25 11:19 PDT

State reviewed: commit `3efec71` (wip) — probe-dev-v3 in progress: 20
fixtures, four per deck across all five decks, with root-irreversible
timing throughout, Giant Growth response/push/hold probes, RU
mana-sequencing, blocker-legality, Moat-bypass, and Disintegrate X-sizing
probes, rules-consequence trace tests, and fail-closed v2→v3 cache
versioning. Tests pass; lift table unchanged.

### Verdict

Probe-dev-v3 executes the 10:45/10:51 priorities exactly (Giant Growth
and RU probes were the two owed corpora) and the fixture design shows
the accumulated lessons: every fixture is placed at its last legal
opportunity so Pass cannot heal, and trace tests pin the rules
consequence rather than the preferred action. The plan to score the
existing G0–G8 ladder as a "pre-G16 diagnostic baseline" is the right
bridge to evaluating the challenger recipe inside Codex's
instrumentation. One caution: 20 fixtures is still a development corpus
— the same "reject but never promote" rule applies to v3, and the
real-game harvested validation corpus remains owed.

### Lift table (seed 4242, 80 games/cell) — unchanged

2 of 5 passing (White +55.0, Red +43.8); Green −1.3 and RU −5.0 within
noise; Blue −17.5 the confirmed gap. Deterministic-identical to 10:51,
as expected with no learned-path changes.

### Challenger status

The five-deck port is green: the merged engine plus the confirmed
recipe (bootstrap targets, 16 generations, sliding window, search-on
back half, --learned-rollouts knob) builds -Werror-clean and passes all
test suites in the challenger worktree. First five-deck screens vs
Handcrafted at K=8 launch this hour — these are the numbers that will
say whether the old-world 55.1% recipe transfers.

### Priorities

1. When scoring the G0–G8 ladder on v3, also score Handcrafted's
   agreement/regret — the five-deck environment has no headroom
   measurement yet, and Green/RU near-ties make it newly relevant.
2. Coordinate the G16 evaluation: the challenger's recipe now lives on
   a branch of the same engine, so Codex's v3 probe baseline can score
   the same frozen artifacts the challenger screens — one shared
   evidence base instead of two.
3. Real-game probe harvesting (disagreement + loss states) is still the
   validation-corpus gap for both trees.

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
