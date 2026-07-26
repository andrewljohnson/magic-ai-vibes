# Independent Review Log

A second agent maintains this file as a running external review of bot-strength
work. Entries are timestamped, newest first. Each entry critiques the latest
state of the repo and, where possible, contributes independently generated
experimental data. Numbers reported here come from actual runs of the checked-in
binary, never from extrapolation.

## Status at a glance

*Updated 2026-07-26 11:11 PDT — refreshed every review cycle.*

- **THE TARGET (user-precise): the all-decks LIFT GATE.** Best
  current read (pooled 240 games/cell over seeds 4242+7801): **1 of
  5** — Green +2.9 PASS; Red/White/RU/Blue trail by 3-5pp. The
  earlier "Blue is the last deck" read was a single-seed artifact;
  the true picture is a small diffuse HC edge across four decks
  (pilot asymmetry, per VC-0). Single-seed 4242 still reads 2/5,
  bit-identical nine cycles running. Certified pooled table one
  certify.sh run away once v3 commits.
- **The real asset: a recipe that keeps winning on retrain.** Bootstrap
  targets + 16 generations + K=8 has beaten Handcrafted in every
  environment it's been retrained in (55.1% four-deck, 53.5% five-deck,
  both at 95% confidence on virgin seeds; 4/5 then 3/5 lift gates as
  worlds grew richer). Each environment change is a ~15-minute retrain —
  a regression test the recipe keeps passing.
- **Qualification rule ADOPTED as policy; v3 gate passed.** Codex's
  notebook now requires strong-pilot outcome separation before any
  fixture label, targets X=0 first post-v3, and has declared the v3
  C16 control freeze. certify.sh (one-command gate panel) is
  committed and ready for v3's first certification run.
- **v3 BASELINE LOCKED: 47.42% pooled over three virgin seeds and
  both harnesses (47.5/46.8/47.9; 6,120 games). All four tested
  deployment components exonerated; VC-1 pinpoints a myopic value
  TARGET (trace-step-4 bootstrap; horizon-truncation mispricing,
  Blue -15pp / Green +18pp in self-play). First recipe fork
  C17-DB8 (bootstrap 4->8) is TRAINING in the challenger tree,
  seed 8629 reserved.**
- **ONE EVENT FROM THE COMPOSITE: PD0 is COMPLETE (all gates
  passed; verified bit-exact; accepted as component) and the fresh
  RB0-0 sealed census is declared. On its verdict the joint
  declaration follows — calendar-8 + continuation prune + PD0
  filter, plus density iff RB0 passes. All replication machinery
  frozen and ready.**
- **OSC-1 (continuation prune): rejected standalone at 47.8% BUT
  the registered instrument hit the night's best Blue mirror
  (55.9% vs ~49%). SIX single-change forms, six instrument
  confirmations, zero gate passes — the MULTI-FAMILY COMPOSITE
  (calendar-8 + continuation prune + RB0-0's component) is the
  evidence-mandated next form. Needs: user's CT8-R GO + RB0-0
  verdict + one joint declaration. Pay component struck as moot
  (engine already PayIfAble); near-tie defects go to component (d)
  pass-preference tie-break.**
- **RB0 DIAGNOSED: floating-point mass drift (uncompensated
  129,280-row summation vs 64-epsilon on arm64), found via
  named-invariant engineering preflight — both 08:32 process
  recommendations now standard practice. Fix scoped to diagnostic
  summaries only; clean post-fix capture licenses a fresh RB0-0
  seed. Composite timeline: preflight -> RB0-0 -> PD0 -> joint
  declaration.**
- **RB0-0 (Codex) was the one live mechanism: FEAT-0b
  came back unsupported with ZERO VARIANCE at the early stratum —
  early-Green optimism predates the board entirely, killing the
  whole board-feature class by construction. Representational
  branch closed. Cross-prediction half-confirmed; an RB0-0 pass
  crowns replay weighting by elimination + confirmation.**
- **CT8-0 SEALED: REJECTED on a 0.0024 RU zero-crossing — while
  delivering the program's strongest-ever diagnostic (early-Green
  +0.032 -> +0.007 past MDE, interaction confirmed as registered,
  pooled Brier dominant, Blue repaired). Verified by reproduction.
  Codex's frozen rule closes the target space; conditional C19 does
  not activate; FEAT-0 killed castability. FOR THE USER: a
  redeclaration with a tolerance-banded safety gate (PASS if
  |bias| <= max(|control|, 0.01)) would pass CT8 on every gate —
  it is the highest-value experiment available, and it needs your
  authorization to reopen.**
- **THE SINGLE-CHANGE ERA IS CLOSED: five preregistered
  interventions on the value surface (distance-8, anneal-0.8, TW75
  endpoint, calendar-turn targets, h8-collection), five sealed
  rejections, each with real but insufficient signal. TA4-0
  verified by reproduction; C18-H8 rejected with its prediction
  failed (Green untouched). NEXT: the jointly-declared COMPOSITE
  (C19) — the only mechanistically supported untested form.**
- **TW-C17 SEALED VERDICT: REJECTED on the conjunctive Green bias
  gate — reproduced bit-exactly by the reviewer. Cross-instrument
  confirmation: HOLD1 bias signs match the VC-2 mispricing map
  deck-for-deck (Green/RU optimistic, Blue pessimistic, Red
  honest); Blue's pessimism repaired as forecast. The
  distance/terminal-weight endpoint family is fully explored and
  unlicensed. GREEN PUZZLE CRACKED (early-game optimism) +
  CODEX FOUND THE TARGET-SEMANTICS DEFECT: the "4-state" bootstrap
  advances only ~2.95 calendar turns, deck-dependent (Blue 2.71).
  TA4-0 calendar-turn audit declared with interlocking predictions
  registered (Blue improves most; early aggro optimism survives
  partially -> h8-collection follow-on). Claude's v3 DEEP-REFERENCE
  CACHE FROZEN (SHA 817de21c) + DRC-1: deployment already agrees
  7/7 with the reference — the cache's role is a REGRESSION GATE
  for future candidates; game-level instruments remain the
  diagnostic tools of record.**
- **DC1 axis CLOSED, both artifacts cross-agent verified.** B0: bound
  90 reproduced byte-identically. B1: rejected on all-five-deck
  density, every scientific number reproduced by the reviewer.
  Signal geography finding: dominance positives concentrate in Blue
  (66/69) vs Red (0/4) — Blue's pair structure is where the signal
  lives. Fallback = Blue-held loss-decision harvest (see correction:
  must filter on decision-context owner).
- **Claude challenger (latest): C17-DB8 REJECTED per fixed gates
  (48.7%, CI includes control) — but the registered sensitive
  instrument moved as predicted (Red mirror +9pp, Green +12pp): the
  value-target surface is live, distance-8 alone insufficient.
  Next: annealed terminal weight. VC-2 four-deck mispricing map
  running. VC-1v3 replicated mispricing under v3.**
- **FIRST WITNESSED BLUE MISTAKE (context above still current):**
  harvest-v2 (Blue-held windows, exact context): 10 qualified; C16
  right at 7/10, but a dual-qualified turn-9 pass-vs-spend BINARY is
  picked wrong by 32.5pp — rare high-cost response errors, not broad
  misplay. QX-0 (stack quiescence before V) licensed and proceeding;
  gate panel on virgin seed 7801.
- **Signal science complete: mirror-outcome teachers are closed.**
  FT128 (rejected: payable wrong-signed at H=128) + HRC (signal band
  H≤8, saturation beyond) + the earlier teacher audit close every
  horizon. Both agents independently converged on the next axis:
  card/resource-advantage auxiliary credit, to be declared under v3
  once the cleanup-discard rules fix lands (v3 not yet live).
- **Pipeline DELIVERED and validated:** tools/certify.sh runs
  build+engine gates → frozen-artifact virgin-seed benchmark →
  all-deck lift table → probe metrics → one CERTIFIED/NOT-CERTIFIED
  verdict, proven end-to-end on a demo model. Every future
  environment version is one command from a certified verdict. First
  real run: v3 at T=800 after the artifact freeze + reload check.
- **Web track:** Old School Arena browser client in progress.

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

## 2026-07-26 11:11 PDT (review cycle)

**No-change cycle at the record level: the RB0-0 sealed rerun is
executing (declaration frozen at HEAD, capture pending).** Lift
table: bit-identical, sixty-fourth cycle. The program holds at one
in-flight sealed event from the composite's component
finalization.

## 2026-07-26 11:03 PDT (review cycle — PD0 complete; one event remains)

**PD0 has passed its COMPLETE gate set — mechanism (verified
bit-exact by me), hidden-information, default-off,
large-regression (51.2% filtered aggregate, floor 40%), and
runtime (+7.35% vs 25% cap) — and is accepted as a composite
candidate component. The fresh RB0-0 sealed rerun is declared on
searched seed 202607261047. The joint composite declaration is now
ONE SEALED EVENT away.** The descriptive smoke slices (48-game,
correctly not evidence) lean the right way: Blue 75.0% filtered vs
66.7% unfiltered, RU 54.2 vs 47.9.

Lift table: bit-identical, sixty-third cycle. My replication arm,
instrument panel, and frozen baseline stand ready; on RB0-0's
verdict (either way), the composite's component set finalizes and
the joint declaration should follow immediately — with density if
it passes, without if it fails.

## 2026-07-26 10:54 PDT (research thread — PD0 verified)

**PD0's mechanism pass is VERIFIED: my deterministic reproduction
returns exit 0 with both published selection hashes bit-exact (G0
control 0x74c3bb33..., C16 treatment 0xa40ef88a...). All eleven
comparator controls — including the user's redundant-counter
fixture (dominated) and payable Force Spike (retained) — behave
exactly as declared.** The dominated-line filter is proven at the
mechanism level. Remaining before the joint composite: your frozen
PD0 large-regression smoke and the fresh RB0-0 sealed census, both
licensed. My replication arm and instrument panel (baseline
profile frozen at 1/6/11 waste) stand ready.

## 2026-07-26 10:47 PDT (review cycle)

**PD0's benchmark admission repair is frozen (09b7e74) — final
pre-run preparation continuing after the RB0 preflight pass.**
Both sealed runs (PD0 diagnostic, fresh RB0-0) remain licensed and
queued. Lift table: bit-identical, sixty-second cycle. All
positions hold.

## 2026-07-26 10:40 PDT (review cycle — RB0 repaired)

**The repaired RB0 preflight PASSED (exit 0, all named invariants,
scientific hashes unchanged, capture 4432f4a2) — the compensated
accumulation fixed the arm64 mass drift with the fix's scope
verified by hash identity on every scientific artifact. A fresh
RB0-0 sealed seed declaration is now licensed with the original
hypothesis untouched.** Clean recovery: failure -> named-invariant
diagnosis -> scoped fix -> adversarial regression -> engineering
preflight -> license, without one scientific surface moving. Lift
table: bit-identical, sixty-first cycle.

Composite timeline now reads: fresh RB0-0 sealed run + PD0 sealed
diagnostic (both licensed and queued) -> joint declaration. My
baseline instrument profile completes its last probe track in
parallel.

## 2026-07-26 10:32 PDT (review cycle — RB0 diagnosed)

**Verdict: the RB0 mechanical diagnosis (9ade1a6) is exactly right
and exactly scoped — the sealed failure was floating-point mass
drift (uncompensated summation over 129,280 rows tripping the
64-epsilon tolerance on arm64), found via a named-invariant
engineering preflight on a permitted non-scientific seed. The fix
touches ONLY diagnostic mass summaries (compensated accumulation),
adds the >=129,280-row adversarial regression first, and licenses
nothing but a fresh RB0-0 seed declaration after a clean post-fix
capture with unchanged scientific hashes.** Both process
recommendations from my 08:32 entry are now operating practice:
named invariants and the dry-run preflight.

Composite path timeline is now concrete: post-fix preflight ->
fresh RB0-0 sealed run -> (with PD0's sealed diagnostic) -> joint
declaration. Lift table: bit-identical, sixtieth cycle. My
baseline instrument profile is one probe track from frozen.

## 2026-07-26 10:17 PDT (review cycle)

**No-change cycle: extraction accepted at HEAD, PD0 sealed run
still pending (capture directory empty).** Lift table:
bit-identical, fifty-ninth cycle. All positions hold; the program
is one PD0 run and one repaired RB0 from the joint composite.

## 2026-07-26 10:11 PDT (review cycle)

**The audit-common extraction is complete and accepted (fe24471) —
all four audit test binaries' stdout SHA-256s exactly equal before
and after, with audit-specific serializers correctly left local.
Textbook frozen-instrument refactoring. PD0's sealed run is now
unblocked and is the next event.** Lift table: bit-identical,
fifty-eighth cycle. My side: the baseline instrument-panel profile
is nearly complete (waste census validated against all three
field-report numbers exactly: 1/6/11 per 20-game run; final probe
tracks finishing).

## 2026-07-26 10:02 PDT (review cycle)

**No-change cycle: extraction refactor still in flight (HEAD
aebff8f), PD0 sealed run queued.** Lift table: bit-identical,
fifty-seventh cycle. All positions hold.

## 2026-07-26 09:47 PDT (review cycle)

**No-change cycle: the audit-common extraction continues (wip HEAD),
PD0 sealed run still queued behind it.** Lift table: bit-identical,
fifty-sixth cycle. All positions hold on the two-event composite
path.

## 2026-07-26 09:40 PDT (review cycle)

**The audit-common extraction declaration (74b18d3, user-requested
dedup) is properly scoped: leaf-only mechanical cleanup with a
before/after stdout-SHA equality contract on all four audit test
binaries, no scientific surface touched.** The right way to
refactor frozen instruments. PD0's sealed run queues behind it.
Lift table: bit-identical, fifty-fifth cycle. All positions hold.

## 2026-07-26 09:32 PDT (review cycle)

**No-change cycle at the notebook level: PD0 smoke control frozen
(e5a7461), sealed run still pending, working tree active.** Lift
table: bit-identical, fifty-fourth cycle. All positions hold.

## 2026-07-26 09:17 PDT (review cycle)

**No-change cycle: PD0 implementation continues (8 working-tree
files, fixture in), sealed run pending.** Lift table:
bit-identical, fifty-third cycle. My side staged the instrument
panel runbook (tools/instrument-panel.sh: one command per
candidate for waste/mirror/calibration profiles). All positions
hold on the two-event path: PD0 sealed diagnostic, repaired RB0,
then the joint composite.

## 2026-07-26 09:11 PDT (review cycle)

**Verdict: the redundant-counter fixture is IN (5ba1ef1) — the
user's Air Elemental game became a PD0 test case inside the freeze
window. Exactly how the three-party loop should work.** PD0
implementation continues (8 working-tree files); sealed run next.
Lift table: bit-identical, fifty-second cycle. My side added the
permanent OS_WASTE_CENSUS instrument (three dominated-line classes,
inert default) for the composite's before/after waste measurement.
Path unchanged: PD0 sealed diagnostic -> repaired RB0 -> joint
composite declaration.

## 2026-07-26 09:03 PDT (review cycle)

**PD0's diagnostic route is frozen (baae44f); implementation
continues in the working tree; the sealed run is next.** Lift
table: bit-identical, fifty-first cycle. One timing note: my 09:02
delivery (redundant-counter class, ~6/run at K=8, with the
settle-first-counter implementation flag) landed alongside your
freeze — if the focused tests haven't sealed yet, a
redundant-counter fixture is the one addition the field evidence
begs for; if they have, the class still serves as post-hoc
validation data. Composite path unchanged: PD0 -> repaired RB0 ->
joint declaration.

## 2026-07-26 09:02 PDT (research thread — field report #5: redundant counters)

**The user caught the bot stacking TWO Counterspells on one Air
Elemental. Census: ~6 real-game redundant counters per 20-game run
at K=8 (hundreds per turn bucket in continuations) — the THIRD
dominated-line class, and the best validation case yet for PD0's
general filter over any hardcode:** a redundant counter fizzles on
settlement, so Pass strictly dominates by your exact comparator —
but only if the bounded forced-pass settlement resolves the FIRST
counter before comparison. Your declared procedure settles the full
stack/window, which should cover it; flagging explicitly so the
implementation and its focused tests include a
redundant-counter fixture.

Dominated-line ledger (real-game, per 20-game mixed run): X=0 casts
11, redundant counters 6, own-spell counters 1 — ~18 wasted cards
per run, one general mechanism, one filter. The composite's
dominated-line component now has three independent field-confirmed
classes behind it.

## 2026-07-26 08:56 PDT (research thread — OSC-2 verdict)

**OSC-2 (X=0 class prune): REJECTED standalone at 48.5% (CI
46.4-50.7) — the SEVENTH single-change form with the identical
signature: +1.7pp point movement, mild instrument gains (Blue
mirror +3pp), no gate pass.** The class prior for PD0 is set:
dominated-line removal carries the same individual point mass as
every other confirmed component. Your general settlement filter
inherits this evidence and replaces my hardcode in the composite.
Seven-for-seven now says the composite is not optional — no single
knob will cross 50% alone; the summed point masses of the
confirmed components (~+1.5pp x 3-4 disjoint mechanisms, if they
stack even sublinearly) are exactly the size of the 2.6pp gap.
Awaiting your PD0 diagnostic and repaired RB0 for the joint
declaration.

## 2026-07-26 08:50 PDT (review cycle — authorization accepted; PD0 endorsed; composite is GO-pending-declaration)

**Verdict: two excellent moves. (1) The authorization record
(a687ef9) handles the user's delegation exactly right — calendar-8
reopens ONLY as a fixed component of the jointly preregistered
multi-family composite, with no standalone retry, no salvage, and
replay weighting admitted only behind a repaired sealed RB0. That
matches my C19-conditional structure precisely; consider this my
countersignature-in-principle for the composite frame. (2) PD0 is
the RIGHT formulation of the X=0 defect and supersedes my OSC-2
mechanism: exact-settlement Pass-dominance via the DC1 comparator
is general and card-agnostic where my census-driven prune hardcodes
the Braingeyser/Disintegrate class — and it already correctly
distinguishes dominated X=0 from incomparable payable Force Spike.**

Coordination note: my OSC-2 class-prune gate is IN FLIGHT
(seed 6871, unobserved, declared before your PD0 landed). It stays
— it reads out within the hour and provides the class-level
aggregate prior that PD0's general filter inherits; long-term the
COMPOSITE takes your settlement filter, not my hardcode. My other
verified toggles (OSC-1 continuation prune — your authorization
already names it; (d) pass-tiebreak) stand ready as composite
components.

Composite critical path is now: (i) PD0's sealed diagnostic,
(ii) the repaired RB0 rerun (with the per-invariant failure lines
from 08:32), (iii) the joint composite declaration. The user's
field reports have supplied four defects, two preregistered
experiments, and one authorization in a single morning — the
two-agent loop is at full speed.

Lift table: bit-identical, fiftieth cycle.

## 2026-07-26 08:32 PDT (review cycle — RB0-0 infrastructure failure)

**Verdict: the RB0-0 classification is correct and honestly handled
— exit 2 at the aggregate guard, no scientific value exposed, seed
202607260731 quarantined, no inference drawn. The reviewable lesson
is in the guard design: a fail-closed check that does not retain
WHICH invariant tripped costs a quarantined seed per mystery.
Twelve minutes of clean collection (all four captures, 129,280 rows
each, accounting printed) died in a guard that ate its own
diagnosis.**

Two recommendations for the redeclaration:
1. Per-invariant failure reporting: every nonthrowing boolean in
   the repeat/reverse/worker, schedule, weight, hidden-zone, and
   mechanical guards should carry its own named failure line — the
   fail-closed exit stays, but the transcript names the culprit.
2. A NO-SEED DRY-RUN mode: exercise the complete pipeline
   (collection + every guard) on an explicitly throwaway schedule
   before any declared seed opens — the analog of my artifact-hash
   and DRC pre-gates, which caught two wiring faults tonight before
   they could consume seeds. Guards are code; code needs its own
   smoke test.

Lift table: bit-identical, forty-ninth cycle. Program state
otherwise per 08:29: three confirmed live components across three
families; the composite awaits your fixed census, the user's CT8-R
ruling, and one joint declaration. The density family is now the
schedule's critical path.

## 2026-07-26 08:29 PDT (research thread — OSC-1 verdict; the composite case is now overwhelming)

**OSC-1 (deployment-only continuation prune from the user's field
report): REJECTED standalone at 47.8% (CI 45.7-50.0) — with the
registered instrument delivering the night's largest Blue-mirror
value: 76-60 = 55.9% vs ~49% control.** Removing own-spell counters
from rollouts materially improves Blue evaluation, exactly as the
mechanism predicted; aggregate movement (+1.0pp) is the familiar
single-change size.

Meta-pattern, now SIX-for-six: every single-change form across
three mechanism families moves its registered instrument as
predicted and adds +1-2pp, and none clears 2,040-game gates alone.
Each family has a confirmed live component: calendar-8 targets
(sealed, user authorization pending), the continuation prune
(confirmed today), and density (your census pending). The
multi-family composite — one knob from each family, jointly
declared, judged by certification v4 — is now supported by
eighteen hours of convergent evidence. It needs: the user's CT8-R
GO, your RB0-0 verdict, and one joint declaration.

My side proceeds meanwhile with the family's cheap component (d):
pass-preference tie-break on epsilon-Q-ties (fixes the two
near-tie field defects — Spike-into-mana and random blocks — at
the Arena-visible layer). Note also the 08:27 correction: the
engine already models Force Spike payment correctly everywhere;
the pay component was struck as moot.

## 2026-07-26 08:17 PDT (review cycle)

**RB0-0 implementation is FROZEN (9e2d48b) with no audit data
opened — the sealed census run is next.** Lift table:
bit-identical, forty-eighth cycle. Research side: the
continuation-policy family (from the user's field reports + my
OSC-0 census, 08:15 entry) is being taken up as my declared axis —
it is disjoint from your density census and from the closed target
space, and its first component (pruning own-stack-object counter
targets from continuation action sets) will be preregistered with
full gates before anything runs. Your census result may interact
(continuation misplay distorts label density); read-across noted
in both notebooks.

## 2026-07-26 08:15 PDT (research thread — USER FIELD REPORTS + OSC-0: a new mechanism family)

**The user played Learned manually and reported three defects; the
OSC-0 census turned them into the sharpest mechanism finding since
the mispricing map: THE CONTINUATION POLICY IS THE SYSTEMIC WEAK
LAYER.** Census (instrumented, then reverted): deployed K=8 play
counters its own spell just once in a full mixed run (in a decided
position) — but the K=0 one-world continuation choosers inside
rollouts do it 1,619 times, at healthy life, as a genuine
preference (ties=1). Every value the deployed search consumes is
computed through continuations that waste Blue's counters — a
direct mechanism for Blue's -9pp pessimism.

The other two reports fit the same layer: Force Spike into 5 open
mana is FT128's payability blindness live (and if the simulated
opponent in rollouts fails to pay 1, the Spike looks strong to the
search — same continuation defect); the random-seeming chump block
is the one-ply block scorer hitting near-ties on coarse material
aggregates.

Candidate treatments logged for joint declaration (card-agnostic,
gated, and DISTINCT from both the closed target space and RB0-0's
density story): (a) prune own-stack-object counter targets from
continuation action sets; (b) rules-derived dominant-branch
resolution for the continuation opponent (pay when paying strictly
dominates); (c) 2-ply or paired-world block scoring. Recommend we
fold this into the post-RB0-0 agenda — it may interact with your
census (continuation misplay also distorts the density of
optimistic labels).

## 2026-07-26 07:53 PDT (review cycle)

**No-change cycle: RB0-0 implementing.** Lift table bit-identical,
forty-seventh cycle. User is awake; program summary delivered in
session.

## 2026-07-26 07:47 PDT (review cycle)

**No-change cycle: RB0-0 implementation continues (4 working-tree
files), HEAD unchanged.** Lift table: bit-identical, forty-sixth
cycle. My research thread is formally quiescent (CLAUDE-PLAN
07:43): all branches assigned — RB0-0 yours, CT8-R the user's,
representational and deployment families closed with evidence.
Waiting on your sealed census.

## 2026-07-26 07:41 PDT (review cycle)

**Minor-change cycle: the replay-weight rerun protocol is frozen
(8979fb3, notebook-only refinement); RB0-0 implementation continues
in the working tree.** Lift table: bit-identical, forty-fifth
cycle. All positions hold — RB0-0 is the one live mechanism, my
FEAT-0b result (delivered 07:39) has closed the representational
branch in its favor, and the CT8-R authorization question awaits
the user.

## 2026-07-26 07:39 PDT (research thread — FEAT-0b result; representational branch closed)

**FEAT-0b: NOT SUPPORTED — and the early stratum returned something
better than a null: ZERO VARIANCE. Early-Green boards are EMPTY in
the sampled states, so board-vulnerability features (the entire
class) structurally cannot drive the early optimism — it exists
before any board does.** With castability dead by correlation and
board-exposure dead by construction, the representational branch is
closed on my side. Your RB0-0 cross-prediction is now half
confirmed: a pass on your census makes replay weighting the
successor mechanism by elimination + confirmation, and the
empty-board fact is friendly to your density story (what varies at
those early states is exactly the record-density context your
weighting equalizes).

Board state for the user's morning: baseline 47.42% locked; five
target-space forms sealed (CT8's strong diagnostic behind the
tolerance-band question, CT8-R package awaiting authorization);
representational family eliminated at zero training cost; RB0-0
(density) is the one live mechanism, implementing on Codex's side.

## 2026-07-26 07:32 PDT (review cycle)

**Verdict: RB0-0 is a first-rate declaration. The mechanism is
genuinely new — record-density imbalance (long actor-games and
dense turns get more gradient mass, overweighting optimistic
early-Green RO4 labels) is the third candidate family after
targets and features, and it could explain early-Green optimism
WITHOUT touching the closed target space. Three process points
deserve note: the 0.010 bias-safety band adopts the CT8-0
gate-design lesson prospectively (exactly the right way to use
it); the closed-family/user-authorization boundary is respected
explicitly; and FEAT-0's castability rejection is already wired
into your branch logic. The pre-run statistics correction
(b40f301) landing before implementation is also proper.**

Zero-training and load-only, so nothing for me to co-verify until
the sealed run; my deterministic replay will follow it as usual.
Registered cross-prediction from my side: if density imbalance is
the driver, my FEAT-0b (removal-exposure correlation, running)
should come back UNSUPPORTED like FEAT-0 — the representational
family would be the wrong tree entirely. Two unsupported FEAT
results + an RB0-0 pass would make replay weighting the clear
successor mechanism.

Lift table: bit-identical, forty-fourth cycle.

Priorities: implement and run RB0-0 as declared; nothing else
pending on my side except FEAT-0b's read-out.

## 2026-07-26 07:17 PDT (review cycle)

**No-change cycle since the CT8-0 record.** Lift table:
bit-identical, forty-third cycle. Status: the CT8-R authorization
package is drafted on my side (CLAUDE-PLAN 07:14) — the user's GO
activates the tolerance-banded redeclaration and, on its pass, the
calendar-8 C19 through the full pre-built gauntlet; until then the
target space stays closed on both sides and your replay-weighting
axis has the floor. Nothing else is pending.

## 2026-07-26 07:13 PDT (review cycle — CT8-0 verified; a gate-design lesson for the record)

**Verdict: CT8-0's sealed rejection is verified — I re-ran the
deterministic audit (exit 1, the RU gate-failure line and metric
rows reproduce) — and the execution was impeccable: sealed one-shot,
independent result audit, no salvage, seed retired. The rejection
stands. What deserves the record alongside it is that CT8-0's
diagnostic content is the STRONGEST positive signal this program
has produced:** early-Green bias +0.032 -> +0.007 (past the 1-point
MDE with the registered interaction confirmed — my 06:32 prediction
lands), pooled Brier dominating every arm, Blue -0.026 -> -0.008,
Green whole-deck +0.031 -> +0.007. It died on RU crossing zero:
+0.007 -> -0.009, absolute bias growing by 2.4 THOUSANDTHS.

Gate-design lesson (forward-looking only; CT8-0's frozen rule
stands and I do not contest it): an absolute-shrink gate on a deck
whose control bias is already near zero is structurally biased
toward rejection under any UNIFORM treatment — a deck at +0.007
cannot absorb a correction sized for decks at +0.031 without
crossing zero, and the card-agnostic constraint (correctly) forbids
per-deck dosing. Future safety gates should carry a preregistered
tolerance band: PASS if |bias_treatment| <= max(|bias_control|,
epsilon) with epsilon ~ 0.01. Under that band, CT8-0 passes every
gate. TW-C17's Green gate had the same shape.

Program state: your frozen rule closes the whole
record/calendar/terminal-weight/collection space on your side, and
my conditional C19 does not activate (its condition was your
factorial's PASS). Your declared next axis is replay weighting. I
will not unilaterally reopen the target space against the spirit of
your rule — but I am recording, for the user's morning read, that
the night's strongest evidence (CT8) now sits sealed behind a
2.4-thousandths zero-crossing, and that a user-authorized
redeclaration with a tolerance-banded safety gate is the single
highest-expected-value experiment available to this program.

Lift table: bit-identical, forty-second cycle.

## 2026-07-26 07:09 PDT (research thread — FEAT-0 result)

**FEAT-0: NOT SUPPORTED — castability is not the early-Green
driver** (early r = -0.111, CI -0.402..+0.205; support required
positive with CI excluding zero). The representational branch's
lead candidate dies at zero training cost; FEAT-1 as sketched will
not be declared. If your CT8-0 factorial fails on early-Green, the
redirect proceeds to removal-exposure/trajectory candidates, not
castability. If CT8 wins, targets remain the story and no feature
work is needed. Your audit's verdict now carries the full weight
of the branch decision — cleaner than before, since one wrong
alternative is already eliminated.

## 2026-07-26 07:02 PDT (review cycle)

**Verdict: the CT8-0 implementation freeze (d6f4e2e) is complete
and excellent — the four-arm evaluator scores per-root-turn strata
(<=3 / 4-7 / >=8), which builds the exact table I asked for at
05:02 directly into the sealed report; the factorial interaction is
formed row-by-row before clustering; double-construction and
hidden-repartition checks carry over; and the physical-turn
distance histograms will audit the unit/reach mechanism itself.
Seed 202607260621 remains unopened; the one-shot run is next.**
Lift table: bit-identical, forty-first cycle.

My side while you run: FEAT-0 (castability-bias correlation, the
zero-training test of the representational hypothesis) is running
on the Green track — its result will be waiting whichever way your
factorial lands. All prior positions unchanged.

## 2026-07-26 06:47 PDT (review cycle)

**No-change cycle: CT8-0 implementation continues (7 working-tree
files), audit not yet run.** Lift table: bit-identical, fortieth
cycle. Both of CT8-0's verdict branches now land on prepared
ground on my side: a CT8 win -> one-knob C19 (cal8 fork
hash-verified, countersignature + seed only); an early-Green
failure -> the representational program (feature inventory
complete: the value vector is all material aggregates, blind to
castability, removal exposure, and trajectory — FEAT-1
castability-features factorial sketched as its first experiment).

## 2026-07-26 06:41 PDT (review cycle)

**No-change cycle: CT8-0 declared but not yet run (working tree
active — presumably the audit implementation).** Lift table:
bit-identical, thirty-ninth cycle. My side is fully CT8-ready:
calendar reach parameterized (cal8 fork hash-verified, inert at
default), so a CT8 win activates the one-knob C19 with
countersignature + seed only. All predictions registered; the
audit's single run is the pending event.

## 2026-07-26 06:32 PDT (review cycle — CT8-0 endorsed; C19 draft amended)

**Verdict: your CT8-0 counter-proposal is RIGHT and I accept the
correction in full.** The three-knob C19 draft carried exactly the
researcher-choice risk you name — the +1.8/+1.9 estimates come
from different noisy seeds and may describe the same error mass,
and adding knobs after five observed results is post-hoc
composition. The 2x2 factorial (unit x reach) on one frozen corpus
is the disciplined way to choose the composite's components: it
tests the interaction at target level for ~62 seconds of audit
instead of a training seed, with terminal weight correctly pinned
at 0.50 as control.

C19 draft status, amended accordingly (recorded in CLAUDE-PLAN):
the component set is now CONDITIONED on CT8-0 — if CT8 wins the
factorial, C19 = calendar-8 targets (one knob), with the anneal
question deferred to its own subsequent factorial rather than
bundled. My composite smoke (06:23) remains valid as wiring
evidence for whatever set survives.

Registered prediction for CT8-0 from my data: GT-0's stratification
says early-Green optimism lives where a 4-turn window sees least —
CT8's 8-calendar-turn reach should cut early-Green bias by more
than CT4 (-0.0014 early) and more than RO8 (record-unit reach is
deck-skewed), i.e., the interaction should be positive exactly on
the early-Green cells. If CT8 fails there too, the early-game
problem is not a target-reach problem at all, and the remaining
suspect is the feature/representation itself.

Lift table: bit-identical, thirty-eighth cycle.

Priorities: run CT8-0 as declared; on its verdict, I will
countersign whatever one-knob C19 it licenses within one cycle.

## 2026-07-26 06:17 PDT (review cycle)

**No-change cycle (HEAD 86366e2; no new notebook sections).** Lift
table: bit-identical, thirty-seventh cycle. The C19 draft (06:14)
awaits your countersignature — it is the only open item on the
board; all machinery on both sides is verified and idle behind it.

## 2026-07-26 06:14 PDT (research thread — C19 draft delivered)

**The C19 joint declaration is DRAFTED and awaiting your
countersignature (CLAUDE-PLAN 06:13; inactive until signed, no
seed drawn).** Composite = champion recipe + exactly three
target-side changes: calendar-turn alignment, calendar distance 8,
terminal anneal 0.5->0.8. h8 collection sight is EXCLUDED on its
standalone regression. Pre-gates: hash inequality + DRC 7/7.
Judgment: your certification v4 on a fresh virgin seed you draw at
activation. Roles: you train + certify; I replicate (fingerprint
must match — determinism check) and run the instrument panel
(mirrors, bias signs vs the map, per-phase vs GT-0). Amend
anything you disagree with in your countersignature — component
set, gates, and roles are all negotiable; the only fixed principle
is one composite, jointly declared, judged by v4, with the five
single-change rejections as negative controls.

## 2026-07-26 06:11 PDT (review cycle)

**No-change cycle since the TA4-0 record (HEAD 86366e2).** Lift
table: bit-identical, thirty-sixth cycle. Status for your next
read: my composite toolkit is COMPLETE — OS_CALENDAR_TURN_TARGETS
(TA4-0 semantics, composing with the annealed weight) joins the
distance/anneal/collection overrides, all env-gated, hash-isolated,
and verified inert at default (champion fingerprint reproduces).
The C19 composite proposal (06:07) stands: you author under
certification v4, I run replication + the instrument panel. If no
declaration lands within a few cycles I will draft it for your
countersignature — the five single-change rejections are burning
daylight as negative controls.

## 2026-07-26 06:07 PDT (review cycle + research — TA4-0 verified, C18-H8 rejected, the composite moment)

**Two sealed verdicts this cycle, and together they close the
single-change era. (1) TA4-0: your rejection is verified — I
re-ran the deterministic audit (exit 1, the exact Green-MDE
rejection line and metric rows reproduce) — and its phase
breakdown (Green correction smallest at turns <=3) confirms GT-0
at your instrument. (2) C18-H8: the h8-collection axis you
endorsed was already declared and gated in my tree — and it is
REJECTED: 47.2% (CI 45.0-49.4) on seed 2861, no point movement,
and the registered prediction FAILED (Green mirror unrepaired).
Collection sight alone does nothing when the collected targets
still carry myopic/miscalendared semantics.** (Both pre-benchmark
gates passed first: artifact hash 99544b7d != control; DRC
regression gate 7/7 vs the frozen reference — its first live use
worked exactly as designed.)

The scoreboard after one night, all preregistered and sealed:
five single-change interventions on the value surface — distance,
anneal, TW75 endpoint, calendar-turn targets, h8 collection —
five rejections, each with real but insufficient signal. The only
mechanistically supported untested form is the COMPOSITE:
calendar-turn semantics + deeper/annealed terminal credit,
possibly + collection sight, trained as one recipe and judged by
your certification v4 pooled gates. Every component exists,
plumbed and verified, across our two trees.

Priorities:
1. JOINT DECLARATION of the composite (C19): I propose you author
   it (your sealed machinery + certification v4 should judge it);
   I run the independent replication arm and the DRC/mirror/bias
   instrument panel. All five single-change results are its
   negative controls.
2. The lift table is bit-identical (thirty-fifth cycle); the
   47.42% pooled baseline stands as the number to beat.

## 2026-07-26 05:47 PDT (review cycle)

**No-change cycle: TA4-0 remains implemented-but-unrun.** Lift
table: bit-identical, thirty-fourth cycle. All coordination state
is in place for the moment your audit fires: interlocked
predictions registered, h8-collection plumbing verified with a
~1.4x cost budget, DRC regression gate frozen. One gentle
priority: TA4-0's single run is the program's sole blocking event —
everything downstream on both sides is staged behind it.

## 2026-07-26 05:48 PDT (research thread — h8-collection feasibility)

**h8-collection is cheap and its machinery is verified: the cost
multiplier is ~1.37x training wall-clock at demo scale (T=800
extrapolation: ~375-400s vs the 291s control), and the demo-scale
h8 artifact hash-differs from default — the OS_COLLECTION_HORIZON
plumbing provably reaches collection training.** No science was
run (no benchmark seeds; feasibility only). Everything needed for
the post-TA4-0 h8-collection declaration now exists and is tested:
plumbing, isolated caches, protocol gate, cost budget, and the
registered per-phase prediction set (GT-0 + the map). Awaiting
your TA4-0 verdict to coordinate the declaration.

## 2026-07-26 05:40 PDT (review cycle)

**No-change cycle: HEAD unchanged, TA4-0 still unrun (audit
implementation in the working tree).** Lift table: bit-identical,
thirty-third cycle. My side added the OS_COLLECTION_HORIZON
plumbing (inert at default, champion fingerprint verified, isolated
cache) so the post-TA4-0 h8-collection declaration starts from
tested machinery — no experiment declared, no seed drawn.

## 2026-07-26 05:32 PDT (review cycle)

**No-change cycle on the bot track** (new commit 707f94e is
web-track — Arena stack-controller display; TA4-0 still unrun,
implementation active in the working tree). Lift table:
bit-identical, thirty-second cycle. Research side added a GT-0
refinement (05:22 entry in CLAUDE-PLAN): the early-game optimism
concentration replicates at SELF-PLAY bias — Green +0.237 early
decaying to +0.093 late, the sharpest cell in the map — further
strengthening the h8-collection case for whoever declares it after
TA4-0.

## 2026-07-26 05:17 PDT (review cycle)

**No-change cycle: HEAD unchanged (TA4 contract frozen), audit
implementation active in the working tree (6 files), audit not yet
run.** Lift table: bit-identical, thirty-first cycle. My side's
queue is clear (DRC-0 cache frozen, DRC-1 agreement report
delivered); the program's single pending event is TA4-0's one-shot
run and its interlocked predictions.

## 2026-07-26 05:15 PDT (research thread — DRC-1 result)

**DRC-1: deployed C16 agrees with the frozen deep reference at ALL
SEVEN cached fixtures (zero regret; my Blue-divergence prediction
refuted — logged as such).** The corpus of cleanly decisive
positions is already solved by deployment; the deficit lives where
every instrument tonight put it — diffuse, rare, in-game,
phase-dependent. Practical consequence for both agents: the DRC
cache's primary role is a cheap REGRESSION GATE — any future
candidate (h8-collection models included) that disagrees with the
reference at these fixtures has broken something the current
champion gets right. Recommend binding it in that role in your
sealed evaluators; fixture-level regret will not diagnose the
current deficit, so game-level instruments remain the tools of
record.

## 2026-07-26 05:11 PDT (review cycle)

**Verdict: the TA4-0 execution/gate freeze (022efd2) is thorough
and correct — a canonical option-less audit route with an internal
fixed seed, artifact snapshots on every exit path, the
double-construction determinism check (one dataset reproduced
internally, honestly labeled as such rather than claimed as two
samples), and bit-identity contracts binding the control arm to
the existing helper output.** The audit itself has not yet run.
Working tree active (4 files — presumably the audit
implementation).

Lift table: bit-identical, thirtieth cycle. My side: DRC-0
delivered (05:07 — the frozen deep-reference cache, SHA 817de21c,
is available for your evaluators to bind); otherwise staged on
TA4-0's result and the h8-collection decision that follows it.

## 2026-07-26 05:07 PDT (research thread — DRC-0 delivered)

**The qualified v3 deep-reference cache is built and frozen — the
infrastructure gap your sealed evaluators noted twice is closed.**
Seven of the 20 dev-v3 fixtures requalified as OUTCOME-RELEVANT
under committed v3 (spreads 12.5-100pp: Green's combat trick, both
Red decisions, all three Blue counter fixtures, RU's
Disintegrate-X); all seven then produced EXACT best-action-set
agreement between disjoint K=64/H=8 scout and confirmation passes —
a complete cache, no exclusions, 26 action rows.

Frozen artifact (committed on claude/challenger):
`drc0-v3-deep-reference-cache.txt`, SHA-256
`817de21cc5806035ded16d8e698623656df632b4b067e1b0316df1e9f71d616f`.
Append-never, regenerate-never; new environment versions require a
new DRC declaration. Your evaluators may bind this SHA to enable
action-agreement/regret claims. One clerical correction on the
record: stage 1 ran with the qualifier's preexisting seed rather
than my declared base (protocol otherwise as declared) — logged in
CLAUDE-PLAN.

## 2026-07-26 05:02 PDT (review cycle)

**Verdict: TA4-0 is an excellent find and a properly-scoped new
axis — the "four-state" bootstrap advances only ~2.95 CALENDAR
TURNS pooled (Green 3.13, Red 3.12, Blue 2.71, White 2.77, RU
3.15): shorter than the deployed four-turn search and
deck-dependent, because sparse trace records mix turn-starts with
priority roots. A concrete, card-agnostic target-semantics defect
found by reading the sealed HOLD1 data carefully. The
calendar-turn treatment (first record with turn_number >= root+4)
is the right minimal correction, and the measurement-only,
fresh-seed, reject-only design is clean.**

Cross-reference with GT-0 (my 04:55 entry, which your declaration
predates): the two findings interlock —
1. Your defect partially explains my map's deck skew: Blue has the
   SHORTEST effective target horizon (2.71 turns), consistent with
   its persistent pessimism being fed by the most truncated
   targets; Green/RU have the longest (3.13-3.15) yet still show
   the largest EARLY-GAME optimism.
2. Registered prediction for TA4-0 from the GT-0 stratification:
   calendar-turn correction uniformizes the horizon to a true 4 —
   expect Blue's pessimism to improve most (largest horizon gain)
   and Green/RU's EARLY optimism to shrink only partially, since
   GT-0 shows their error concentrates where even a true 4 turns
   covers little game. If early-stratum optimism survives your
   calendar fix, horizon-8 collection remains the follow-on.
3. Suggested cheap addition (measurement-only, within your audit's
   spirit): report the treatment-vs-control bias deltas BY TURN
   STRATUM (<=3 / 4-7 / >=8) — that single table will confirm or
   kill the early-game reading at your instrument.

Lift table: bit-identical, twenty-ninth cycle. My side: the v3
deep-reference cache build is my declared next item (04:55);
proceeding.

## 2026-07-26 04:55 PDT (research thread — the Green puzzle cracks)

**GT-0 (zero-compute stratification of the existing probe data)
cracks the Green anomaly: the aggro optimism is an EARLY-GAME
phenomenon** — Green bias +0.208 at turns <=3 decaying to +0.085
late; RU +0.191/+0.223/+0.051; Blue's pessimism phase-flat. Two
consequences with direct bearing on your next declaration:

1. The optimism lives exactly where h4 covers the least game, and
   early-state bootstrap targets inherit the parent's own
   early-state optimism at i+4 regardless of blend weight — which
   is WHY every blend-arithmetic change (your TW75, my distance-8
   and anneal) failed on Green while succeeding on Blue. The
   evaluation doesn't need different target weights; it needs to
   SEE further from early states.
2. **Horizon-8 collection targets is therefore the evidence-pointed
   next recipe axis** — and it is yours if you want it: your sealed
   shard/evaluator machinery is exactly built for it, and the
   mispricing map + this stratification give it a registered
   per-deck, per-phase prediction set (early-stratum optimism
   should shrink most; Blue improves less; Red roughly unchanged).

Division of labor: I am taking the QUALIFIED V3 DEEP-REFERENCE
CACHE as my next declared build (your evaluators have now twice
noted its absence blocks action-regret claims) — infrastructure in
my toolkit's verification style, no seed consumption, and it
unblocks a measurement class for every future sealed run.

## 2026-07-26 04:47 PDT (review cycle — TW-C17 verdict + cross-instrument confirmation)

**Verdict: the TW-C17 sealed rejection is correct, disciplined, and
scientifically rich. I reproduced the sealed evaluator bit-exactly
(every Brier/bias/delta to full precision, exit 1). The rejection
stands on the precommitted conjunctive Green gate even though every
pooled paired-loss comparison favored TW75 — exactly how reject-only
gates are supposed to work, and the refusal to reinterpret
calibration gains as gameplay evidence or reopen the retired seed
is textbook.**

The scientific richness is in the cross-agent agreement:
1. **Your HOLD1 per-deck bias signs match my VC-2 mispricing map
   deck-for-deck at a completely different instrument** (your
   record-weighted critic-vs-target on mirror traces; my Q(pass)
   probes): Green optimistic, RU optimistic-lean, Blue pessimistic,
   Red ~honest. Two agents, two instruments, one map — the
   mispricing structure is now as established as anything in this
   program.
2. TW75 moved biases in the map-predicted directions on Blue
   (-0.087 -> -0.058), White, and Red — and delivered precisely
   estimated pooled calibration gains. The mechanism (terminal
   grounding repairs truncation bias) is confirmed at clean
   attribution.
3. **Green is the residual puzzle**: the largest optimism (+17pp in
   my map, +0.078 in yours) did NOT shrink under TW75 (or under my
   full-recipe forms, by mirror proxy). Green's overpricing
   apparently does not live in the terminal/bootstrap blend at all.

Program consequence, per both agents' exhaustion rules: the
distance/terminal-weight endpoint family is now fully explored and
unlicensed (my two full-recipe forms rejected at gates; your paired
endpoint rejected on Green). The pre-committed remaining axis on my
side is HORIZON-8 COLLECTION TARGETS; the Green anomaly suggests a
second candidate: a Green-specific target diagnostic. I will
declare one of these two — with the mispricing map as its
registered prediction set — next research cycle, coordinated
against whatever axis you declare.

Lift table: bit-identical, twenty-eighth cycle.

Priorities:
1. Declare your next axis; I will avoid duplicating it.
2. Candidate axes ranked by the evidence: (a) horizon-8 collection,
   (b) Green target diagnostic, (c) qualified v3 deep-reference
   cache so action-regret claims become available to future sealed
   evaluators (your own noted gap, twice now).

## 2026-07-26 04:45 PDT (research thread — the complete mispricing map)

**VC-2 is complete and the four-deck map has clean structure the
original prediction missed: the deployed evaluation is OPTIMISTIC
about the aggressive decks whose mirrors C16 loses (Green +17pp,
RU +9pp self-play bias) and PESSIMISTIC about the control deck
(Blue -9pp), with Red honest.** Both halves of my registered
Red/RU-most-negative prediction are refuted — recorded as such.
The structure is mechanistically coherent with horizon truncation
cutting both ways: h4 sees aggro boards before their conversions
fail, and misses control's late wins.

For your arms and the joint combined-form declaration: this map is
now the registered prediction set. Terminal-grounded targets (your
TW75; my two rejected forms) should pull Green/RU optimism DOWN
and Blue pessimism UP — exactly the mirror-repair pattern both my
forms produced — while Red improves only through the policy
channel. If TW75's HOLD1 calibration moves match those signs, the
mechanism is confirmed at clean attribution and the combined form
(distance + anneal, or horizon-8 collection) inherits a concrete,
falsifiable per-deck forecast.

My ledger: VC-2 consumed no benchmark seeds (probe protocol only);
surface remains seed-frozen awaiting your arms.

## 2026-07-26 04:41 PDT (review cycle)

**Verdict: the TW-C17 one-shot training run is a valid paired
artifact — sealed evaluator frozen (b737e28), shard seed
202607260311 consumed exactly once via the capture wrapper, both
arms trained with distinct fingerprints (TW50 46afe9c6, TW75
247eae84) from the exact frozen parent, every balance and
target-algebra invariant green, and correctly recorded as
providing NO performance evidence yet.** The arms' decisive
read-out — HOLD1 calibration then sealed gameplay — is next and
imminent.

Reminder for that read-out (04:23 entry): the Red-honest result
predicts your arms may show calibration shrink on Green/Blue while
Red moves at gameplay without calibration movement — consistent,
not contradictory, under the two-channel reading. The joint
combined-form declaration follows your verdict.

Lift table: bit-identical, twenty-seventh cycle. My RU track (last
mispricing-map row) still computing.

## 2026-07-26 04:32 PDT (review cycle)

**No-change cycle: HEAD unchanged, sealed-evaluator work continues
in the working tree (7 files), no new notebook sections.** Lift
table: bit-identical, twenty-sixth cycle. My side: RU track
(the mispricing map's last row) re-running; all else staged on
TW-C17.

## 2026-07-26 04:23 PDT (research thread — Red mispricing verdict)

**VC-2's Red track refutes my registered prediction, and the
refutation matters for reading your TW-C17 arms: Red is priced
HONESTLY** (self-play bias +0.047, 95% CI -0.007 to +0.098, below
the 0.05 materiality rule) despite having the worst mirror in the
program (35-40%). The mispricing map now reads: Blue -9pp
underpriced, Green +17pp overpriced, Red honest; RU computing.

Interpretive consequence, registered before your arms open: target
changes improve play through TWO channels — calibration repair
(where bias exists: Blue/Green) and pure policy shift (Red's
+9/+11pp mirror repairs under my two forms cannot be bias
correction). For TW-C17 this predicts: your calibration-shrink
condition may pass on Green/Blue while Red shows gameplay gains
WITHOUT calibration movement — that pattern would be consistent,
not contradictory. And it strengthens stacking: distance and
anneal forms need not compete for the same error mass.

## 2026-07-26 04:17 PDT (review cycle)

**No-change cycle: HEAD unchanged, working tree active (6 files,
presumably the sealed evaluator), no new notebook sections.** Lift
table: bit-identical, twenty-fifth cycle. All positions hold.

## 2026-07-26 04:11 PDT (review cycle)

**No-change cycle on the bot track** (new commit 7058517 is
web-track only — Arena journey record; no new EXPERIMENTS.md
sections; TW-C17 awaiting its sealed evaluator). Lift table:
bit-identical, twenty-fourth cycle. Positions unchanged.

## 2026-07-26 04:03 PDT (review cycle)

**Verdict: the TW-C17 implementation freeze (2379443, efea2d0) is
accepted-quality work — the seal design deserves specific credit:
the one-shot atomic TW bundle with mutual loader rejection, the
refusal to retrain over an existing bundle, and the prohibition on
generic benchmark use of all three reserved seeds together make the
"exactly once" property mechanically enforced rather than promised.
128/128 engine tests, CLI and diff checks green, and the
independently-found overwrite gap fixed and regression-tested
before freeze.** No science yet, correctly: next step is the sealed
evaluator, then opening the raw-shard seed once.

Lift table: bit-identical, twenty-third cycle. My side: still
seed-frozen on the surface; VC-2's Red track is re-running in
shorter per-track units (long background jobs keep being reaped);
partial Red data hints the registered most-negative-bias prediction
may be refuted (Red trending POSITIVE bias like Green) — full
fixed-analysis verdict when the track completes, and either way it
sharpens the combined-form prediction your paired arms will decide.

Priorities: unchanged — sealed evaluator, open the shard, run the
arms.

## 2026-07-26 03:47 PDT (review cycle)

**No-change cycle: HEAD unchanged, TW-C17 implementation continues
in the working tree (6 modified files).** Lift table: bit-identical,
twenty-second cycle. All positions unchanged from 03:33/03:41 —
the program waits, correctly, on TW-C17's paired arms.

## 2026-07-26 03:41 PDT (review cycle)

**No-change cycle: HEAD unchanged (c3880a7), no new EXPERIMENTS.md
sections, TW-C17 implementation still mid-flight in the working
tree.** Lift table: bit-identical, twenty-first cycle (2/5 at seed
4242; operative headline remains 47.42% pooled). Program
configuration unchanged from 03:33: everything staged on TW-C17's
paired arms; my side seed-frozen; VC-2 Red/RU rows computing.

## 2026-07-26 03:33 PDT (review cycle)

**Verdict: TW-C17 implementation is proceeding correctly — helper
commits landed, evaluation metrics frozen, and the pre-run
clarifications are exactly what pre-run clarifications should be:
definitional closures (record-weighted losses, game-clustered CR1
intervals, the strict shrink condition, quartet gameplay panels)
committed before any reserved seed opens, changing no arm and
selecting no result. The stale-cache prohibition on action
diagnostics is especially good discipline.** Working tree is
mid-implementation (game.hpp/game.cpp/main.cpp); not building or
judging it. Lift table: bit-identical, twentieth cycle.

Reminder for the read-out (from 03:25): both of my full-recipe
single-change forms are now rejected standalone at ~48.6-48.7%
with repeated aggro-mirror repair — your paired arms decide the
stacking question, and the joint combined-form declaration follows
your result. My side is seed-frozen on this surface; VC-2's
Red/RU mispricing rows (running) will supply the combined form's
registered prediction.

Priorities: run TW-C17; everything else is staged on it.

## 2026-07-26 03:25 PDT (research thread — TW8 verdict)

**C17-TW8 (full-recipe annealed terminal weight) is REJECTED per
its fixed gates: 48.6% (CI 46.4-50.7) on seed 1471.** And the
pattern is now unmistakable: two independent single-change forms on
the value-target surface (distance-8, anneal-to-0.8) each deliver
~+1.8pp aggregate and large aggro-mirror repair (Red +9/+11pp, RU
+4/+7pp) without clearing gates at 2,040 games. The surface is
real; single changes are individually insufficient.

For your TW-C17 read-out, this sharpens what matters: your paired
arms give the clean per-target attribution that decides whether
these effects STACK. If TW75-TW50 shows a clear paired gameplay
delta, the joint combined-form declaration (per the 03:18
protocol: anneal + distance together, or horizon-8 collection)
should follow immediately — my +1.8pp/+1.8pp point movements are
exactly what two stackable half-effects would look like. I am
drawing no further seed on this surface until your arms land.

Consumed/reserved seed ledger (mine): 9317, 6733, 5077, 8629
(instrumentation-consumed), 3253, 1471.

## 2026-07-26 03:18 PDT (review cycle — COORDINATION FLAG)

**Verdict: TW-C17 is a beautifully controlled declaration — the
paired TW50/TW75 arms on one shared shard, the exact algebraic
invariant (TW75 - TW50 = 0.25*(z - v4)) checkable row-by-row, and
the fresh full-retrain equivalence audit (byte-identical artifact,
SHA 53aeb904) are all exemplary. One coordination flag before it
runs: your declaration predates my 03:16 entry — BOTH agents are
now on the terminal-weight surface.** My C17-TW8 (full 16-gen
retrain, terminal weight ANNEALED 0.5 -> 0.8 across generations,
seed 1471 reserved) is already training.

Proposed resolution, no redeclaration needed on either side: the
two designs answer DIFFERENT questions and should BOTH run —
- TW-C17 (yours) isolates the pure target-form effect at one
  incremental generation with paired control: clean attribution,
  small effect surface.
- C17-TW8 (mine) tests the deployed full-recipe form: noisy
  attribution, decisive deployment relevance.
If your paired arms show TW75 > TW50 and my full-recipe anneal
misses its gates, the surface is real but needs the full-recipe
integration done differently (e.g., your one-shard incremental
path becomes the recipe extension). If both hit, we have
convergent evidence at two scales. The distance-6 bracket arm
moves to whichever agent frees up first.

Also verified this cycle: your fresh 291-second full retrain
reproducing the byte-identical artifact is the third independent
determinism confirmation; the refactor-acceptance chain (fingerprint
+ goldens + my bit-identical lift replay) is complete and sound.

Lift table: bit-identical, nineteenth cycle.

Priorities:
1. Run TW-C17 as declared — the overlap is complementary, not
   duplicative; proceed.
2. Note my consumed/reserved seeds now include 1471 (TW8) — your
   202607260311/12 do not collide.
3. After both terminal-weight results: joint declaration on the
   combined form (anneal vs fixed 0.75 vs distance mix) before
   anyone draws another seed on this surface.

## 2026-07-26 03:11 PDT (review cycle)

**No-change cycle on the bot track** (new commit 11a5471 is
web-track Chromium tests only; no new EXPERIMENTS.md sections;
working tree clean). Lift table: bit-identical, eighteenth cycle
(2/5 at seed 4242; operative headline remains the 47.42%
three-seed pooled baseline).

Challenger status: Codex's certification-v4 + helper commits are
MERGED into the challenger tree (champion fingerprint verified
through the merged plumbing; suites green) — both trees now share
the value-target machinery. VC-2 (four-deck mispricing map) is
~40% through its state sample; the anneal variant implementation
follows on its registered prediction.

Priorities: unchanged from 03:03 (bracketing declaration; commit
leftovers — the latter now done).

## 2026-07-26 03:03 PDT (review cycle)

**Verdict: both priorities from 02:47 executed and verified. The
production bootstrap-label port (c1b790a) preserves the champion —
fingerprint 68126afc loads identically through the helper routing,
their tests cover it, and my strongest check confirms it: the
standing seed-4242 lift table is BIT-IDENTICAL through the
refactor (seventeenth consecutive identical cycle, now doubling as
a behavior-preservation proof at 2,000-game scale). Certification
v4 (pooled direct evidence) is implemented and accepted.** The
distance parameter now genuinely governs production training on
their side — the wiring trap is closed.

Program state in one paragraph: baseline 47.42% locked over three
seeds; value-target surface identified (VC-1/VC-1v3, replicated
under v3); first form (distance 8) rejected per fixed gates at
48.7% but with the registered sensitive instrument moving as
predicted (Red mirror +9pp, Green +12pp); anneal variant queued on
my side; VC-2 four-deck mispricing map computing; Codex now has
the API plumbing to run distance/anneal variants natively.

Priorities:
1. Declare the training-strength challenger with the bracketing
   split (02:55 offer): one agent takes annealed terminal weight,
   the other distance-6; both behind artifact-hash gates and fresh
   virgin seeds, gated by certification v4.
2. Commit the Makefile/WEB_ROADMAP working-tree leftovers before
   the next archived-tree certification.

## 2026-07-26 02:55 PDT (research thread — C17-DB8 verdict)

**C17-DB8 is REJECTED per its preregistered gates — and the
rejection is informative: the value-target surface is alive, the
specific distance-8 form is just insufficient alone.** Seed 3253,
2,040 games, hash-verified DB8 artifact (317f44e5): 48.7% (CI
46.5-50.8). Crown gate missed; the CI does not exclude the 46.8%
control upward, so per the fixed gates the form is rejected with no
dial-turning permitted on this seed.

The descriptive layer, recorded as such: +1.9pp point movement over
control, and the REGISTERED sensitive instrument moved exactly as
predicted — Red mirror 46.3% vs ~37% control (+9pp), Green mirror
53.7% vs ~42% (+12pp), RU +4pp. The decks with the worst
mispricing-map signs improved most. For Codex's training-strength
declaration: distance alone recovers roughly a third of the v3 gap
in point terms but cannot clear gates at 2,040 games; the
pre-committed next form on my side is ANNEALED TERMINAL WEIGHT
(generation-indexed blend). Bracketing offer stands: if you take
the anneal via your new n_state API (after porting the two inline
sites — see 02:47), I will run distance-6; or the reverse.

VC-2 (four-deck mispricing map, Red/RU tracks added to the probe)
is running; its per-deck biases forecast where any target-side fix
should gain, giving the next variant a registered prediction before
it runs.

## 2026-07-26 02:47 PDT (review cycle)

**Verdict: an outstanding Codex cycle — three clean moves. (1) The
certification closed as a valid scientific rejection at 47.94%
(978-1062, Wilson 45.78-50.11), the fourth consistent measurement
of the artifact (pooled family now 47.5/46.8/47.9 -> 47.42% over
6,120 games, plus their per-deck matrix showing White as the only
direct-sign pass). (2) The v4 pooled-direct metrology correction is
sound — applying the strict all-five requirement once to all
prespecified evidence instead of twice to noisy slices is exactly
the pooling discipline this program converged on; it is not
threshold weakening. (3) `n_state_bootstrap_targets` is in mainline
WITH TESTS — the value-target surface is now a first-class recipe
parameter.** Lift table: bit-identical, sixteenth cycle.

Coordination note on the bootstrap surface (my lane, 02:36-02:44
entries): my C17-DB8 fork (distance 8, single change, everything
else champion) is training now against reserved virgin seed 3253 —
note the instrumentation lesson logged at 02:44: the challenger
training path did NOT route through four_state_bootstrap_targets;
it has its own inline constants at two sites (game.cpp ~14828,
~15091). If your generalized API is to govern the actual C16
recipe, those two lambdas must be ported to call
n_state_bootstrap_targets — otherwise a distance parameter set
through the new API silently won't reach production training, which
is precisely the failure my artifact-hash protocol gate caught. The
hash-inequality check (fork artifact must differ from control
before any benchmark runs) is worth adopting alongside the API.

Priorities:
1. Port the two inline bootstrap sites to n_state_bootstrap_targets
   (with a fingerprint-preservation test at distance 4 — the
   champion must reproduce 68126afc bit-for-bit through the new
   API).
2. Implement + test certification v4 as declared.
3. My DB8 result on seed 3253 lands shortly — hold the distance
   choice for your first training-strength declaration until it
   reads out; one of us should also queue distance 6 to bracket.

## 2026-07-26 02:41 PDT (review cycle)

**Verdict: the replacement certification is executing exactly as
declared and its primary benchmark has landed — 978-1062-0 = 47.9%
(CI 45.8-50.1) on seed 202607260219. The declared
rejection-expected hypothesis is confirmed.** With my two panels
this makes THREE independent virgin-seed measurements of the same
frozen artifact: 47.5%, 46.8%, 47.9% — pooled 2,902/6,120 =
**47.42%** over three seeds and both harnesses. The committed-v3
baseline is now about as solid as a baseline gets; the remaining
harness stages (fingerprint re-check, report finalization) should
close it out cleanly. Lift table: bit-identical, fifteenth cycle.

The baseline being locked, the program's open question is now
purely constructive: which recipe change recovers the edge. Status
of the evidence-backed first candidate (my lane, 02:36 entry):
C17-DB8 — bootstrap distance 4 -> 8, single-change fork of the
champion recipe, isolated cache, virgin seed 8629 reserved — is
TRAINING now. Its rationale chain (QX-1 horizon conviction ->
VC-1 self-play mispricing -> myopic value target) is fully
preregistered, with the Red mirror as the sensitive instrument and
annealed-terminal-weight as the pre-committed next variant if flat.

Priorities for Codex:
1. Land and commit the certification result record — the 47.42%
   three-seed pooled baseline deserves to be the notebook's
   headline number.
2. For the training-strength challenger declaration: C17-DB8's
   result (imminent) is a free first data point on the value-target
   surface before you commit compute; the cross-agent pattern
   (one declares, the other replicates) has worked all night.

## 2026-07-26 02:34 PDT (review cycle)

**Verdict: the replacement certification declaration (40b96a8) is
exactly right — rejection-expected primary hypothesis grounded in
the two independent controls, the determinism result correctly
adopted to retain the frozen control artifact, fresh
repository-searched seed clear of every consumed one, and the
promotion path still requiring every gate if the primary surprises.
Nothing to amend. The attribution reconciliation (18d1729) is also
faithful.** Working tree is clean (REVIEW.md only). Lift table:
bit-identical, fourteenth cycle.

One addition since your declaration read (02:32 entry): VC-1
resolved the mechanism fork — the deck-signed evaluation errors
persist in PURE SELF-PLAY (Blue -15pp, Green +18pp), overturning
the pilot-asymmetry reading. The defect is the VALUE TARGET's
myopia (bootstrap at trace-step 4; horizon-truncation signature
matching QX-1's conviction). For the training-strength challenger
this upgrades the recommendation from "richer aggro self-play" to a
specific recipe surface: deepen/anneal the bootstrap distance or
collect horizon-8 targets, with mirror win-rate as the instrument.
VC-1v3 is running to confirm under committed v3.

Priorities:
1. Run the declared certification — it locks the baseline record.
2. When declaring the training-strength challenger, the value-target
   surface (02:32) is the evidence-backed first candidate; happy to
   run the independent replication arm of whatever you declare.

## 2026-07-26 02:32 PDT (research thread — VC-1 result; a self-correction and the training lever)

**VC-1 overturned my own VC-0 interpretation, and the correction
matters more than the original: the deck-signed evaluation errors
are NOT pilot asymmetry — they are genuine VALUE MISPRICING,
present even when C16 pilots both continuation seats itself.** Blue
self-play bias -0.154 (CI -0.219,-0.085): the deployed evaluation
under-predicts C16-Blue's own realized wins by 15pp. Green +0.182
(CI +0.117,+0.243): over-predicts its own Green play by 18pp.
(v2-rules binary; Green control partial n=25 — caveats logged.)

Mechanism: the prediction is the h4-bootstrapped evaluation, the
realization is the full game. The signature is horizon-truncation
value error — V at the 4-turn boundary cannot see slow decks' late
wins and over-credits early boards. QX-1's single 32.5pp blunder
(flipped by h8) was one instance; this is the systematic version.

**Concrete recipe recommendation for the training-strength
challenger (the strongest transferable idea of the night): the
value TARGET is too myopic.** The recipe blends terminal outcome
with frozen-parent V at trace-step 4 — the same 4-step scale as the
deployment horizon that misprices. Candidates, in declaration-ready
order: (a) deepen the bootstrap distance (8+ trace steps) with the
existing blend; (b) anneal the terminal weight upward across
generations so later gens see longer-range credit; (c) collect
search targets at horizon 8. Mirror win-rate (Red first) is the
sensitive validation instrument. VC-1v3 is running to confirm the
same structure under committed v3 with a registered Red/RU
prediction.

## 2026-07-26 02:18 PDT (review cycle)

**Verdict: the v3 control baseline record (commit 981d208) is
exactly right — external evidence adopted with correct provenance,
v2 claims annotated as environment-versioned, my reserved seed
protected, and a clean fingerprint decision rule for the next
step.** Lift table: bit-identical, thirteenth cycle (2/5 at seed
4242; the pooled/committed-v3 headline remains the operative one).

Closing your open decision rule with data you had not yet seen
(my 02:14 entry): the committed-source retrain is COMPLETE and its
fingerprint is **bit-identical** to the current artifact
(68126afc...). Per your own rule: proceed from the current artifact
with a fresh, noncolliding primary seed. Consumed seeds to avoid:
9317, 6733, 5077 (mine), 11235813 (yours), plus the historical set.

Also new since your last read (02:17 entry): DP-0 exonerated the
discard policy (45.3% with HC discards on seed 5077 — flat vs the
46.8% control). The attribution ledger now clears ALL FOUR tested
deployment components; the deficit is trained play quality under
v3, concentrated in aggro mirrors (Red 35%, RU 39%). Strongest
recommendation for the training-strength challenger: treat MIRROR
win-rate as the primary development instrument (15pp of signal vs
2pp in aggregates) and target aggro-deck self-play quality.

Priorities:
1. Redeclare certification from the current artifact, fresh seed,
   rejection-expected hypothesis — lock the baseline record.
2. Declare the training-strength challenger against mirror + pooled
   controls; the discard surface is cleared, don't spend effort
   there.
3. Commit the Makefile/WEB_ROADMAP working-tree leftovers.

## 2026-07-26 02:17 PDT (research thread — DP-0 result)

**DP-0: the discard policy is EXONERATED.** Swapping Learned's
cleanup discards for the Handcrafted heuristic scored 45.3% (CI
43.2-47.5) on virgin seed 5077 — within noise of the 46.8% control,
possibly marginally worse. The champion's greedy-critic discard
policy was already adequate; v3's new decision surface is not where
the edge went. Red/RU mirror losses persist unchanged.

The attribution ledger for the v3 deficit now reads: shallow-prior
blend exonerated (QX-1), world count exonerated (QX-1), stack
horizon exonerated at gate scale (QX-2c), discard policy exonerated
(DP-0). Nothing deployment-side has been convicted in four
preregistered experiments. Everything — the VC-0 pilot-gap biases,
the mirror pattern (Red 35%, RU 39%, White 65%), and now four
exonerations — points at TRAINED PLAY QUALITY under v3 dynamics:
recipe-level, concentrated in aggressive-deck piloting.

For the training-strength challenger declaration, the sharpest
available target is therefore: improve aggro-deck self-play (the
recipe's collection/search settings were tuned in slower v1/v2
worlds), and validate against mirror win-rate as the most sensitive
instrument (Red mirror moves 15pp where aggregates move 2pp). VC-1
lands imminently and will say whether value-target quality or
data richness is the specific lever.

## 2026-07-26 02:15 PDT (review cycle)

**Verdict: the certification dependency repair is correct and
properly scoped — root cause was the harness's forced-empty npm
cache, the fix pins the preexisting cache under a hard offline
contract, 42/42 self-tests, and no scientific surface changed.
Accept.** My 47.5% control result is now reconciled into the
notebook. Lift table: bit-identical, twelfth cycle (2/5).

Before the replacement certification declaration, two facts from my
02:12-02:14 entries that change what the declaration should say:
1. **Recipe determinism**: my full retrain under committed v3
   reproduced the frozen control fingerprint bit-for-bit. A
   replacement certification of the same artifact will measure the
   same model; expect the primary gate to reject (47.5% / 46.8% on
   two virgin seeds). That rejection is worth having ON RECORD via
   your harness — but the hypothesis should predict it, not predict
   >50%.
2. **DP-0 is running**: the v3 cleanup-discard surface is hot
   (21k events in a 20-game smoke) and mirror games localize the
   deficit to Red/RU. If the surface-swap restores >50% on seed
   5077, the "general training-strength challenger" has its first
   concrete component: a trained (or heuristic-fallback) discard
   policy inside the recipe.

Priorities for Codex:
1. Redeclare certification with the honest primary hypothesis
   (rejection expected at ~47%) — the value is the immutable
   baseline record, not the verdict.
2. Hold the training-strength challenger declaration until DP-0
   lands (imminent); it may hand you the first component for free.
3. Working tree shows Makefile + web/package-lock modifications —
   commit the offline-fix leftovers so the next certification
   archives a clean tree.

## 2026-07-26 02:14 PDT (research thread — retrain result + the v3 lead)

**The retrain regression test returned two facts. One: the recipe is
deterministic — my independent retrain under committed v3 produced
fingerprint 68126afc... bit-identical to the frozen control
artifact (same training seed -> same model, across trees). Two: the
sub-parity verdict is confirmed on a second virgin seed — 46.8%
(CI 44.6-48.9, seed 6733) beside 47.5% (seed 9317). Under v3, the
standard recipe loses to Handcrafted; retraining at the same seed
cannot change that. The recipe itself needs a v3-aware change.**

The lead suspect is v3's NEW DECISION SURFACE. The cleanup-discard
correction created a real choice (which cards to keep at 7+ hands)
and it is hot: 21,032 discard events in a 20-game/pair smoke,
firing in real turns and inside every search continuation. Learned
discards by greedy contextless critic-eval; HC by its card-value
heuristic. Mirror matches (purest pilot test, seed 6733): C16 loses
the Red mirror 54-82 and RU 60-76 while winning White 80-56 — the
deficit concentrates where discard/keep judgment interacts with
curve-out pressure.

DP-0 is running: an eval-only surface swap (--handcrafted-discards:
Learned delegates ONLY cleanup discards to the HC heuristic),
aggregate benchmark on virgin seed 5077, same 2,040-game protocol
as the control. If it restores >50% at 95%, the v3 regression is
substantially a discard-policy problem — and the durable, in-recipe
fix (a trained discard policy) becomes the top shared priority. If
it stays at ~47%, discards are exonerated and the search moves on.
Either way Codex gets a clean attribution result to build the
"general training-strength challenger" on.

Note for Codex's certification redeclaration: determinism means
your control artifact IS the retrained artifact under the standard
recipe — a "retrain and recertify" step at the same training seed
would be a no-op. Recipe changes (or a training-seed panel) are the
only way the aggregate moves.

## 2026-07-26 02:04 PDT (review cycle)

**Verdict: two Codex results this cycle, both handled with exemplary
integrity — BSR0 INVALID (design infeasibility, reproduced by me
exactly), and their certification attempt stopped at
infrastructure (npm/DNS in the archived tree) with seed 11235813
correctly consumed. Meanwhile my independent certification already
answered the question their harness never reached: 47.5% aggregate
(see 02:03). The program's next fact must be the v3 RETRAIN.**

Verifications performed:
- BSR0 reproduction: I ran the exact audit on the committed tree —
  exit 1, INVALID, retention FAIL, and the root cause confirmed:
  Blue 40-0 vs White in the source schedule (White losses=0), so
  the every-stratum retention rule was unsatisfiable. All scientific
  facts match Codex's capture (timing lines differ as always).
  Their handling — no retry, zeros reported but not accepted as
  evidence, root cause named, feasibility-aware retention required
  for any future audit — is textbook.
- The scored-but-invalid footnote (zero diagnostic mistakes >=0.05
  in 32 roots) is directionally consistent with the rare-error
  picture QX-2c established, but correctly carries no evidential
  weight.
- Lift table: bit-identical (2/5, eleventh cycle).

On their certification declaration: the hypothesis ("C16 passes
primary aggregate >50% on seed 11235813") is now unlikely on
evidence they had not yet seen when declaring — my seed-9317 panel
measured 47.5% (CI 45.4-49.7) on the same frozen artifact and
committed rules. When they repair provisioning and redeclare with a
new seed, the honest prior is rejection at the primary gate.

Priorities for Codex:
1. **Decouple certification from the network**: the archived-tree
   gate ran `npm ci` — engine certification should be engine-scoped
   (my certify-lite skips web suites for exactly this reason), or
   vendor the web deps. A bot-strength claim should not be able to
   die on registry DNS.
2. **Retrain first.** Before a new certification seed: the 47.5%
   stands on the frozen artifact; the scientifically interesting
   question is whether the RECIPE restores the edge under v3. My
   independent retrain regression test is running (seed 6733
   reserved); a same-recipe retrain on your side gives the
   cross-agent pair.
3. The declared "general training-strength challenger" direction is
   the right one and now has three independent supports (QX-2c
   rarity, VC-0 pilot asymmetry, 47.5% aggregate). Declare it
   against the pooled-gate control, not single seeds.

## 2026-07-26 02:03 PDT (research thread — CERTIFICATION RESULT)

**The first committed-v3 certification says NOT CERTIFIED — and the
aggregate is the headline: C16 970-1070-0 = 47.5% vs Handcrafted
(CI 45.4-49.7, virgin seed 9317, 2,040 games, load-only cached
artifact 68126afc). Learned is currently BELOW Handcrafted under
committed v3.** This is the first at-scale v3 aggregate ever run;
the cleanup-discard correction erased (at least) the whole prior
edge (53.5% in v2 five-deck). Pooled 3-seed gate: 2/5 with the
passing-deck identity flipped again (Red +0.4, RU +3.3 pass; Green
−2.4, White −4.6, Blue −7.1 fail) — further confirming the diffuse
unstable-margin picture.

This is not a disaster; it is the regression test the program was
built for. The artifact was never the asset — the recipe is. The
mandatory next step is RETRAIN under committed v3 + recertify, and
that run is already in flight in my tree (v3 merged, all six engine
suites green after adapting my eval seams to the new cleanup_turn;
fresh virgin seed 6733 reserved for the post-retrain panel).

Also merged and noted: Codex's certify.py convergently implements
pooled multi-seed panels (8 seeds) — the metrology conclusion
reached independently on both sides. One caution on its
PANEL_SEEDS (101...808): 202 and 404 are burned historical seeds;
fine for stability panels, but keep them out of any virgin-seed
confirmation claim.

Priorities for Codex (urgent reordering):
1. **Retrain C16 under committed v3 and re-run the aggregate
   benchmark before anything else.** The 47.5% stands on the frozen
   control artifact; if the retrained recipe restores >50% at 95%,
   the recipe survives its hardest regression test yet; if not, v3
   is a genuinely harder environment and the training recipe itself
   needs v3-aware work. Either answer reshapes all treatment plans.
2. BSR0 is still worth its one shot, but its corpus sources from
   the frozen artifact in an environment where that artifact is
   now sub-parity — interpret regrets accordingly.
3. The v2-era 53.5%/55.1% claims should be annotated in the
   notebook as environment-versioned facts, not current status.

## 2026-07-26 01:48 PDT (review cycle)

**Verdict: v3 IS COMMITTED (HEAD `48b8709`) — the top-priority item
all night — and the first full v3 certification is now RUNNING from
the committed hash on fresh virgin seed 9317.** The commit freezes
the v3 research state (EXPERIMENTS.md +805 lines, engine + web),
leaving only REVIEW.md (this file, mine) uncommitted. `make` is
current with the committed sources; the standing seed-4242 lift
table is bit-identical on the committed tree (tenth cycle, 2/5).

Also in the commit: two pre-execution BSR0 amendments, both sound:
1. The rare-error amendment recalibrates the PASS criterion to the
   observed error class (40 roots; PASS needs one stable root with
   regret >= 0.20 and paired 95% LCB > 0.10) and — excellent
   epistemic hygiene — explicitly labels my 32.5pp finding as a
   replication prior rather than claiming blind preregistration.
2. The capture wrapper (run_bsr0_once.sh) makes the one-shot rule
   mechanically enforceable (refuses overwrite; immutable capture +
   exit + SHA artifacts).

One integration note for Codex's next read: the amendment's PASS
outcome licenses "a stack-root deployment diagnostic such as bounded
quiescence" — that lane has since been run to completion on my side
and REJECTED at its preregistered gate (QX-1 convicted horizon;
QX-2c stack-conditional h8 gave Blue +1.2pp, no significance — see
01:24). A BSR0 PASS should therefore license the TRAINING-side
treatment (regret-weighted corpus) directly rather than re-walking
the deployment path; the pooled-gate recalibration (01:41: real
picture is 1/5 with four diffuse deficits) points the same way.

Certification in flight: sh tools/certify.sh 9317 from the
committed tree — build + engine suites, aggregate benchmark vs
Handcrafted at 95%, POOLED 3-seed lift gate (93171-3), probe
scoring, one verdict line. Results in the next entry.

Priorities for Codex:
1. Run BSR0 via its wrapper — everything is in place.
2. On my certification's pooled-gate result: adopt it as the
   program headline (single-seed reads flip; see 01:24/01:41).
3. Keep the working tree clean now that v3 is frozen — commit
   experiment results as they land.

## 2026-07-26 01:41 PDT (review cycle)

**No-change cycle (BSR0 still mid-implementation, ~13.2k inserted
lines; lift table bit-identical for a ninth cycle at 2/5) — but the
research thread has a recalibration Codex should see before writing
any more Blue-centric declarations.**

Pooled-gate preview: my certify pipeline now pools the lift gate
over multiple seeds (tools/pool_lift.py, validated to reproduce the
single-seed verdicts exactly). Pooling the two 100%-real panels in
hand (seeds 4242 + 7801, 240 games/cell) reads the gate at **1/5**:

| Deck | pooled HC lift | pooled Learned lift | gap |
| --- | ---: | ---: | ---: |
| Green | +35.4 | +38.3 | +2.9 PASS |
| Red | +59.2 | +56.2 | −2.9 |
| White | +46.2 | +43.4 | −2.9 |
| RU Aggro | +49.1 | +45.8 | −3.3 |
| Blue | +49.1 | +44.6 | −4.6 |

Caveat: the two runs straddle the two working trees, so treat this
as a mechanism-validated preview, not a certified claim. But the
shape is consistent and important: **the "Blue is the last deck"
narrative was a single-seed artifact. At pooled scale the picture is
Green ahead and four small (3-5pp) deficits.** This matches the
VC-0 pilot-asymmetry reading: HC's handcrafted play has a small,
diffuse edge nearly everywhere, largest in Blue.

Priorities for Codex:
1. Commit v3, then run one pooled-gate panel (3 seeds, one tree) to
   fix the program's true headline before any treatment planning.
2. BSR0 remains worth running as declared — but read its result
   against the diffuse-deficit picture: the crown will not come from
   fixing Blue alone.
3. Treatment planning after BSR0 should target the GENERAL lift gap
   (training-strength: richer self-play, regret-weighted corpora
   across decks), with Blue as the largest instance rather than the
   sole target.

## 2026-07-26 01:32 PDT (review cycle)

**No-change cycle: BSR0 implementation continues in the working tree
(~12.9k inserted lines now), no new EXPERIMENTS.md sections, HEAD
unchanged.** Lift table bit-identical for an eighth consecutive
cycle (2/5 at seed 4242; Blue −6.2 the only established deficit —
and see the 01:24 metrology note: seed 7801 reads 1/5 on the same
bot, so treat single-seed gate reads accordingly).

Research-side status for the dashboard: VC-1 (self-consistent
realization at the same 180 calibration states) is running; its
result either locks the "Blue gap = diffuse pilot strength ->
training-side lever" conclusion or reopens value mispricing.

Priorities for Codex: unchanged — commit v3; run BSR0 as declared
(its expected-result framing updated by QX-2c/VC-0: 1-3 replicated
mistakes would confirm rare-but-real, and the value-calibration
redirect should now read "training-strength redirect").

## 2026-07-26 01:31 PDT (research thread)

**VC-0 landed and killed its own hypothesis — with a finding
underneath that reframes Blue entirely.** Blue is NOT worse
calibrated than the Green control (ECE 0.156 vs 0.204; the
difference runs the other way, P=0.973). But the signed errors are
enormous and OPPOSITE: C16 under-predicts Blue pass-branches by
15pp and over-predicts Green by 20pp — and each sign tracks exactly
which pilot plays that deck better in the lift table (Handcrafted
outplays C16 on Blue; C16 outplays Handcrafted on Green). The probe
measured PILOT GAP, not value error.

Convergent picture across three independent experiments tonight:
QX-2c (discrete blunders real but too rare to move the gate), VC-0
(no relative miscalibration), and the bias signs (pilot asymmetry):
**Blue's lift-gate deficit looks like diffuse pilot strength — HC's
handcrafted Blue play is simply stronger across many small
decisions — not a fixable deployment defect or a value pathology.**
If VC-1 (running: same states, realization under C16's own pilots)
shows the bias vanish, that conclusion locks: the lever is
TRAINING a stronger Blue (regret-weighted corpora a la BSR0, or
counterspell-rich self-play), not deployment surgery. Directly
relevant to what BSR0 should expect and to any post-BSR0 treatment
declaration.

## 2026-07-26 01:24 PDT (review cycle + research)

**Review verdict: BSR0 implementation has landed in the working tree
(audit flag now in src/main.cpp, no run record yet) — awaiting its
single declared run. Research verdict: QX-2c was run against its
preregistered gate on virgin seed 7801 and is REJECTED — a validated
negative with two lessons the whole program should absorb.**

Standing lift table (seed 4242): bit-identical for a seventh
consecutive cycle (2/5). New paired data below is from the spent gate
seed 7801 at 160 games/cell.

QX-2c gate (stack-conditional horizon 8, C16 K=8, paired seed-7801
runs): Blue Learned lift +46.9 -> +48.1 (+1.2pp, predicted
direction); all four other decks' Learned cells unchanged to the
decimal — the surgical signature expected when only rare stack
windows change. But +1.2pp is a net two-game swing on 160: no paired
significance, gate 1 fails, QX-2 family closed. The witnessed
32.5pp blunder class is REAL (rung E flips it deterministically) but
too RARE to move the deck-level gate. Lesson one: fixing witnessed
discrete mistakes is not where Blue's remaining lift-gap lives;
value calibration (VC-0, now active) is the standing suspect.

Lesson two — gate metrology, and this one matters to the user's
primary view: baseline seed 7801 reads the gate at 1/5. White FAILS
−6.2 where seed 4242 reads White +3.8 PASS; Red −3.8 vs −1.2. Deck
verdicts flip sign between seeds at 80-160 games/cell. Any gate
margin under ~8pp is noise; only Blue's persistent same-sign deficit
is established signal. Recommendation: the certified all-decks gate
(certify.sh stage 3, and any Learned-is-king claim) should pool
multi-seed panels to 500+ games/cell or adopt a sequential test;
otherwise the gate will pass and fail on reruns of the same bot.

Priorities for Codex:
1. Commit v3 (unchanged, increasingly urgent — BSR0's implementation
   is now also sitting uncommitted).
2. Run BSR0 as declared. Note its zero-result interpretation is now
   the EXPECTED branch: QX-2c's rejection says discrete wrong picks,
   even when replicated, are too rare to explain the gap — BSR0
   finding 1-3 mistakes would CONFIRM the rare-but-real picture, and
   the redirect to value calibration is where both agents' evidence
   already points.
3. Adopt the multi-seed gate pooling for certification before any
   crown claim; a 1/5-vs-2/5 flip on reruns would be a credibility
   bug in the headline metric.

## 2026-07-26 01:02 PDT (review cycle)

**No-change cycle at the notebook level; BSR0 implementation still in
flight** (working-tree diff grew from ~9.5k to ~11.8k inserted lines
since last cycle; the audit flag is not yet in src/main.cpp; no new
EXPERIMENTS.md sections). Not building or judging the mid-edit tree.

Lift table: all ten cells bit-identical for a sixth consecutive cycle
(2/5; Blue −6.2 established, Red −1.2 / RU −3.8 in-noise, White +3.8
/ Green +7.5 PASS), regenerated on the last built binary.

For BSR0's implementation, one heads-up from the research thread
worth having before the run: QX-1 convicted HORIZON at the witnessed
blunder (blend and worlds exonerated; see 00:57 entry). BSR0's
horizon-8 reference is therefore well-positioned to find deployment's
h4 mistakes — but it also means a BSR0 zero-result would be
surprising and worth double-checking against the horizon prediction
before accepting the value-calibration redirect.

Priorities for Codex: unchanged (commit v3; finish and run BSR0 as
declared; decomposition pairing after).

## 2026-07-26 00:57 PDT (research thread)

**QX-1 verdict: HORIZON convicted at the witnessed 32.5pp blunder;
shallow-prior blend and world count both exonerated.** At the
dual-qualified turn-9 binary, the wrong pick survives blend-off and
64 worlds unchanged, but flips to the playout-best branch the moment
the continuation horizon goes 4 -> 8 (Q 0.495 vs 0.507). Mechanism:
the pass branch lets a threat resolve whose cost materializes past
the h4 boundary — V at the horizon reads the slow loss as
recoverable. My 00:41 flat-0.5 blend suspicion was WRONG for this
window; logged as such. The two relevance-only wrong picks convict
nothing (one noise-level, one anomalous exact Q-tie between branches
playouts separate — logged, not pursued).

Direct BSR0 relevance: Codex's reference runs horizon 8 — exactly
the convicted component. If BSR0 replicates mistakes, this predicts
the reference's advantage comes primarily from horizon depth, not
from its 64+64 worlds or its blend-free scoring. That is now a
registered prediction (rung C exonerates worlds at the witnessed
window; rung B exonerates blend).

Next: rung E (8 worlds, h8) is running to fix the exact treatment
shape; then QX-2c — horizon 8 at NON-EMPTY-STACK roots only,
card-agnostic, gated reject-only on reserved virgin seed 7801 (Blue
must improve with paired significance, no deck may degrade,
aggregate must hold). If the panel passes, this is the first
deployment-recipe change either agent has licensed against the lift
gate; if it fails, the negative is validated and the axis moves to
value calibration.

## 2026-07-26 00:47 PDT (review cycle)

**No-change cycle at the notebook level; implementation in flight.**
No new EXPERIMENTS.md sections since BSR0's declaration and the
`--audit-v3-blue-stack-regret` flag does not exist yet, but the
working-tree diff grew ~1,400 lines since last cycle — BSR0
implementation appears to be mid-edit. Per discipline, I am not
building or judging a mid-implementation tree; verification waits for
the run record (or better, a commit).

Lift table: regenerated on the standing binary — all ten cells
bit-identical to the last four cycles (2/5; Blue −6.2 established,
Red −1.2 / RU −3.8 in-noise, White +3.8 / Green +7.5 PASS). Note the
caveat: the engine binary predates the in-flight edits; the table
re-verifies against the last built state, which is the honest
comparison while HEAD is mid-implementation.

Challenger status (for the dashboard): QX-1 config-ladder
decomposition is implemented and running — per-branch Q under
deployment, no-blend, 64-world, and horizon-8 rungs at the three
positive-regret windows; first-flip-convicts rule fixed in advance.

Priorities for Codex: unchanged from 00:43 (commit v3; run BSR0 as
declared with its tests; decomposition pairing after).

## 2026-07-26 00:43 PDT (review cycle)

**Verdict: both reconciliations are faithful and BSR0 is the best
declaration Codex has written — a genuine independent replication
protocol for the witnessed Blue mistake, with ownership filters,
stratified sourcing, a stronger disjoint-pass reference, and
interpretation fixed in advance.** Not yet implemented (the audit
flag does not exist in src/main.cpp); declaration only. Lift table
regenerated: all ten cells bit-identical to the last three cycles
(2/5; Blue −6.2 established, Red −1.2 / RU −3.8 in-noise).

What deserves explicit credit in the record:

1. The harvest-context correction is recorded exactly right — the
   invalidated labels stay invalidated, opponent-held windows are
   demoted to relevance-only permanently, and the zero-result
   interpretation (value calibration, not invented treatments) is
   pre-committed.
2. The signal-geography note preserves B1's positive finding (Blue
   66/69 positives vs Red 0/4) instead of discarding it with the
   rejection.
3. BSR0 treats my harvest-v2 result as an external prediction to
   reproduce, not as its own discovery — the cleanest possible
   epistemic framing between two agents sharing a repo.

One design observation, offered for AFTER BSR0 runs as declared (not
as a modification): BSR0's reference differs from deployment on
exactly the three components my QX-1 decomposition suspects in the
32.5pp blunder — shallow-prior blend (reference: none), world count
(64+64 vs 8), and continuation policy/horizon (Learned-mirror at 8 vs
champion at 4). If BSR0 finds stable mistakes, its retained roots are
the perfect input corpus for a separately declared decomposition that
scores each root under deployment config per-component — convicting
blend, worlds, or horizon individually. My QX-1 harvester extension
(in progress in the challenger tree) will do this on my ten windows;
BSR0's twenty stratified roots would make the same analysis
cross-deck. Proposed division of labor: Codex runs BSR0 as declared;
I run the decomposition; the guilty component gets one targeted,
separately declared deployment experiment instead of three blind
A/Bs.

Also noted: BSR0's eligibility requires the stack TOP be
opponent-controlled, which excludes windows where Blue re-decides
with its own spell on top after an opponent response. Fine for the
declared class (pass-vs-counter); just keep the exclusion in mind if
the count comes in low.

Priorities for Codex:
1. Commit v3. Now five accepted/declared artifacts on uncommitted
   state; BSR0's implementation will make it six.
2. Implement BSR0 with its focused tests and run it once, as
   declared.
3. After BSR0: declare the component decomposition on its retained
   roots (pairing offer above) rather than any direct treatment.

## 2026-07-26 00:40 PDT (research thread)

**harvest-v2 landed: the first witnessed Blue mistake exists.** Ten
Blue-HELD windows qualified (decision ownership from exact trace
context; binaries admitted at stack windows). C16 picks the
best-outcome branch at 7 of 10 — the deficit is not pervasive misplay
— but at a DUAL-QUALIFIED turn-9 two-action binary (pass-vs-spend at
stack, exactly the predicted class) it takes the 0.22 branch over the
0.55 branch: a 32.5pp regret confirmed by both pilot families. Two
further wrong picks (13.5pp, 15.0pp) are relevance-only. Transferable
facts for the v3 Blue-loss harvest Codex declared: (1) ownership must
come from `decision_player` in the decision-trace context; (2) the
>=3-action gate hides the mistake class — the witnessed blunder is at
a 2-action window; (3) expect rare high-cost errors, not broad
misplay, so regret-weighted corpora beat volume corpora.

QX-0 update (same hour): code reading closed it before it ran — the
deployed deep continuation is ALREADY quiescent (stack resolves in
`continue_priority_window`; V applies only at turn boundaries).
Mid-stack V exists only in the shallow prior at ~11% blend weight,
including a flat 0.5 on window-ending passes under the context
schema. Since 11% rarely inverts 22-vs-55, the witnessed blunder's
suspects narrow to world sampling, the in-window opponent policy, or
V error at post-resolution states. QX-1 (declared): per-branch
component decomposition — shallow prior vs per-world deep
continuation vs blend — at every regret>0 window, to convict the
guilty component before any deployment change. Seed 7801 stays
virgin and reserved. Transferable to Codex now: the flat-0.5
shallow-prior return on window-ending passes is a plausible
systematic dilutant at exactly Blue's pass-vs-counter binaries —
worth checking whether the mainline blend has the same shape.

## 2026-07-26 00:34 PDT (review cycle)

**Verdict: DC1-B1 rejected correctly on all-five-deck density — and I
reproduce every scientific number in the capture. The DC1 axis closes
clean: the bound was repaired with evidence, and density still says
no.** The declared fallback (harvest frozen C16's actually-lost Blue
stack-response decisions) is the right move and now has a live warning
attached: see my 00:28 correction — decision ownership must come from
trace context, not state shape, or the corpus will be opponent-held
windows again.

Independent verification: I ran the exact B1 command against the same
C16 fingerprint. All aggregates match Codex's table exactly — training
18,543 roots / 3,705 multi / 2,448 retained / 6,346 pairs / 203,072
settlements, held-out 18,046 / 3,566 / 2,473 / 6,883 / 220,256; Red 0
training positives and 4 held-out; Blue 66/69 with coverage 25/4;
verdict REJECT, exit 1. Only the timing line differs (154.7s vs
159.7s), which fully accounts for the capture-SHA difference. Two
DC1 artifacts are now cross-agent verified in one night.

The density table is itself a finding worth keeping: immediate
resource dominance concentrates almost entirely in Blue (66/69
positives vs Red's 0/4) — dominance-comparable action pairs are a
counterspell-deck phenomenon in this environment. Whatever teacher
finally cracks Blue, this says its signal exists in Blue's pair
structure specifically, which is consistent with my harvest-v2
prediction that Blue's real decisions are pass-vs-counter binaries.

Lift table: regenerated, all ten cells bit-identical to the 00:15 and
00:21 cycles (2/5; Red −1.2 and RU −3.8 in-noise FAILs, Blue −6.2
established, White +3.8 and Green +7.5 PASS). Not re-tabulated here.

Priorities for Codex:
1. Before declaring the v3 Blue-loss harvest: read the 00:28
   correction. The v1 corpus's 8 states are opponent-held windows;
   the harvest must filter on the decision-context owner
   (decision_player == harvesting seat) and admit 2-action
   pass-vs-counter binaries at stack windows, or Blue's real decision
   class is structurally invisible. My harvest-v2 (running) is
   exactly this design — its output can serve as the independent
   replication target for your v3 declaration.
2. Commit v3. Four accepted fingerprinted artifacts now sit on
   uncommitted state.
3. DC1's closure note should record the Blue-concentration fact
   (66/69 vs 0/4) as a positive result about signal geography, not
   just a density failure.

## 2026-07-26 00:28 PDT (research thread — CORRECTION)

**Correction to the Blue harvest record, before anything downstream
consumes it: all 8 qualified loss-states are OPPONENT-held priority
windows, not Blue decisions.** The context-fixed harvester (exact
per-decision trace context, same 8 states reproduced, sanity gate
passed) shows `decision_player = 1` at every one. C16-Blue never chose
at these states; the branch spreads measured hypothetical player-0
interventions at windows Green actually held. What survives: the
states are genuinely outcome-live under both pilot families
(relevance 8/8 stands, and the dual-qualification method itself is
unaffected). What does not survive: "Blue's deficit is localized to
stack-response mistakes" — no Blue mistake has yet been witnessed.
Blue's real response windows are mostly pass-vs-counter binaries,
which the v1 >=3-action gate structurally excluded.

Relevance to DC1-B1's fallback branch: if B1 misses density and moves
to "the separately qualified v3 Blue stack-response corpus," note that
corpus does not exist yet in labeled form. harvest-v2 (preregistered,
running) rebuilds it from Blue-HELD windows only (exact context,
binaries admitted at stack windows); its registered prediction is that
qualified windows cluster on the pass-vs-counter class — and if none
qualify, Blue's deficit is value miscalibration, not discrete wrong
picks, which would redirect both agents' Blue plans.

## 2026-07-26 00:21 PDT (review cycle)

**Verdict: DC1-B0 accepted correctly, and I reproduce it bit-for-bit —
the exact bound 90 is now cross-agent verified. DC1-B1 is declared
cleanly with the single evidence-required change.** Lift table is
unchanged from the prior cycle (2/5 with Red/RU inside noise; Blue
-6.2 the only established deficit).

Independent verification performed this cycle: I ran Codex's exact
census command against the same C16 fingerprint. My scientific-section SHA-256 is
`c4cb2ddbb6d63599c9e56608d9502c7cbf0863f7a0dc78477b0d400232b74559`,
byte-identical to both of Codex's published invocations. Exit 0, same
18,543/18,046 root counts, same two over-64 roots (80 and 90 actions,
both X-spell explosions from Disintegrate at high mana in RU Aggro vs
White, turn 26). The bound census is therefore a three-run,
two-agent, byte-identical fact.

Review notes on DC1-B1 (declared, endorse with two observations):

1. The histogram says the over-64 tail is vanishingly thin: 2 roots
   in 18,543 (0.011%), both from one degenerate late-game seat. The
   bound change 64→90 is evidence-required for exactness, but the
   original DC1 rejection was a *bound trip*, not a density miss —
   B1's real risk is still the per-deck density gates (32+32 training
   / 16+16 held-out per deck), which the bound change does nothing to
   help. If B1 fails on density, the declared fallback (move to the
   qualified v3 Blue stack-response corpus, don't weaken DC1) is the
   right one — and my dual-qualification table already gives that
   corpus its label standard (relevance 8/8 both pilots, labels 3/8).
2. The two X-spell roots are themselves a small finding: action-count
   explosions in this environment come only from X costs at high
   mana. Any future action-set bound can safely be derived from
   max mana * X-templates rather than measured each time.

Bot benefit by deck (seed 4242, 80 games/cell, v3 working tree, C16
K=8) — unchanged from 00:15 cycle, all ten cells identical:

| Deck | HC lift | Learned lift | Gate | delta vs prior |
| --- | ---: | ---: | --- | --- |
| Red | +57.5 | +56.2 | FAIL (-1.2, in-noise) | unchanged |
| White | +42.5 | +46.2 | PASS (+3.8) | unchanged |
| Green | +36.2 | +43.8 | PASS (+7.5) | unchanged |
| RU Aggro | +45.0 | +41.2 | FAIL (-3.8, in-noise) | unchanged |
| Blue | +46.2 | +40.0 | FAIL (-6.2) | unchanged |

Priorities for Codex:
1. Unchanged and still first: commit v3 to a hash. Three accepted
   artifacts (v3 rules, C16 control, B0 census) now rest on
   uncommitted state; a stray working-tree edit invalidates all
   their fingerprint provenance at once.
2. Run B1 as declared — no notes on the design; the bound change is
   exactly scoped.
3. Pre-commit to the density-failure branch: if B1 misses density,
   the Blue stack-response corpus inherits the dual-qualification
   standard (labels only on dual-agreed states, relevance-only
   otherwise) rather than a new qualifier debate.

## 2026-07-26 00:15 PDT (review cycle)

**Verdict: the lift gate reads 2/5 on the current working tree — but the
two new FAILs (Red, RU) are inside sampling noise, while Blue improved.**
No new Codex commits (HEAD `b087536`) and no new EXPERIMENTS.md sections
since DC1-B0; the working tree is the declared-v3 state and `make` is
current. Regenerated the seeded table twice (stale `alpha-sim` binary and
freshly-checked `old-school-sim`): bit-identical, so this is the true
state of the tree, not a build artifact.

Bot benefit by deck (seed 4242, 80 games/cell, v3 working tree, C16 K=8):

| Deck | Random | Handcrafted lift | Learned lift | Gate | delta vs prior |
| --- | ---: | ---: | ---: | --- | --- |
| Red | 17.5% | +57.5 | +56.2 | FAIL (−1.2) | was PASS |
| White | 27.5% | +42.5 | +46.2 | PASS (+3.8) | held |
| Green | 25.0% | +36.2 | +43.8 | PASS (+7.5) | held |
| RU Aggro | 20.0% | +45.0 | +41.2 | FAIL (−3.8) | was PASS |
| Blue | 38.8% | +46.2 | +40.0 | FAIL (−6.2) | improved from −10.0 |

Reading: at 80 games/cell each lift difference carries roughly ±8pp of
noise, so Red at −1.2 and RU at −3.8 are coin-flips of the gate, not
regressions in the bot — the persistent, reproducible deficit remains
Blue, exactly where the harvested stack-response evidence points. The
gate needs either larger cells or a sequential design before its
per-deck margins are treated as signal.

Timestamp correction: my two prior entries are stamped 05:55/06:20 PDT
in error (clock misread); they were written shortly before this one.
Order on the page remains correct.

Priorities for Codex:
1. Commit v3 to a hash — every downstream artifact (certification,
   stratified harvest, DC1-B0 census) is blocked on it, and the working
   tree is drifting under uncommitted state while gates are being read.
2. When reading the lift gate, treat sub-noise margins (Red −1.2,
   RU −3.8) as undetermined, or raise cells to 200+ games; only Blue is
   an established deficit.
3. The dual-qualification survival table (research entry above) resolves
   the qualifier dispute: relevance 8/8 under both pilots, labels 3/8 —
   adopt dual-qualified-labels / relevance-only-corpus as the standard.

## 2026-07-26 06:20 PDT (research thread)

**The qualifier dispute is resolved with data.** Dual-pilot survival
table over the eight Blue loss-states: outcome-RELEVANCE is confirmed
by BOTH pilot families on all eight (C16-pilot spreads 20-50pp beside
the Handcrafted 23.5-66pp) — the relevance claim stands in full. But
best-branch agreement holds on only three (turns 4, 6, 10): those are
DUAL-QUALIFIED and can carry labels; the other five are
relevance-qualified but label-ambiguous — genuinely contested
decisions whose optimum depends on the continuation pilot. Proposed
standard (matches Codex's instinct and the reviewer's data):
dual-qualified states carry acceptance labels; single-pilot-relevant
states enter corpora as relevance-only. Three concrete, dual-agreed
Blue mistakes now exist for the v3 rerun.

## 2026-07-26 05:55 PDT

DC1-B0 declared: the legal-action census — an evaluation-only
diagnostic identifying the offending >64-action root and the full
action-count/kind distribution before any bound is changed. Exactly
the follow-up the DC1 rejection required; the reconciliation note
correctly scopes the reviewer's dual-pilot qualifier as a separate
future experiment. v3 remains uncommitted; lift table (v3 working
tree) 4 of 5, Blue −10.0, unchanged. Research thread: the dual-pilot
harvest reruns at declared reduced C16-pilot cost after the first
attempt proved intractable.

## 2026-07-26 05:15 PDT

No-change cycle: DC1's bound diagnostic is presumably in progress
after the >64-action rejection; v3 still uncommitted; lift table (v3
working tree) 4 of 5, Blue −10.0. Research thread: the two-pilot
qualification (the reconciliation to the qualifier dispute) is
implemented — a learned-pilot playout seam beside the Handcrafted one,
dual qualification requiring agreeing best branches — and the dual
harvest is running over the eight Blue loss-states. Its verdicts will
show which of the eight survive the strengthened standard.

## 2026-07-26 04:50 PDT

**DC1: rejected before density on its own legal-action bound** — some
root exceeds 64 legal actions (plausibly a Braingeyser/Disintegrate
X-enumeration state) and the guard failed closed in 48s with no model
touched. The declared follow-up is correct: an evaluation-only
diagnostic identifying the offending root and an evidence-based bound
replacement, no silent raise. Clean process.

**On the disputed harvest qualifier — the dispute is partly right and
here is the reconciliation.** Two legitimate objections: (1) the
harvest is v2-rules provenance (conceded from the start; the v3 rerun
was always declared); (2) branch values from Handcrafted-vs-Handcrafted
playouts are PILOT-RELATIVE — they measure outcome separation under
Handcrafted play, not under optimal play. The defense: pilot-relative
separation is still evidence of outcome-relevance (a fixture where the
strongest available pilot's result depends on the branch is relevant
by any standard), and the qualifier never assigns LABELS, only
relevance. Proposed reconciliation for v3, preregisterable: two-pilot
qualification — a state qualifies only if BOTH Handcrafted-pilot and
frozen-C16-at-K=8-pilot playouts show branch separation with agreeing
sign. Dual-family agreement removes the single-pilot objection while
keeping the outcome grounding. The stack-response localization (04:35)
is robust to this dispute: it is a claim about which states qualify,
not about which action is correct.

Lift table (v3 working tree): 4 of 5, Blue −10.0, unchanged.
Priorities: (1) DC1 bound diagnostic; (2) commit v3; (3) two-pilot
qualification standard for the v3 harvest.

## 2026-07-26 04:35 PDT (research thread)

**Blue's gap is localized: every qualified loss-state is a
stack-response decision.** The deterministic outcome-regret rerun
reproduced all 8 qualified states, and each one is Blue holding
priority over a resolving Green spell — counter/response timing worth
23.5-66.0pp per decision, zero qualified main-phase states. The
instant-speed thread that ran through the whole program (Blue/Green
lift failures, counter-war fixtures, K-sensitivity) now has
outcome-grounded confirmation and concrete instances. Implication for
both agents: Blue treatments should target stack-response evaluation
specifically — deployment-side (deeper/wider search at nonempty-stack
roots only — cheap, since such roots are rare) before training-side.
C16's actual picks at these states are one context-plumbing fix away
from measurement.

## 2026-07-26 04:05 PDT

DC1 implementation nears completion: the dominance comparator carries
29/29 proof coverage (exact X=0 and two-sided Force Spike ledgers,
priority-stop context, candidate-orientation reversal, Mox
Sapphire/Sol Ring payment ledgers with excess-pool clearing) and its
CLI fails closed unless the pinned v3 C16 artifact already exists —
it cannot silently train, write, or deploy. The mining run is the
next event. Lift table (v3 working tree): 4 of 5, Blue −10.0,
unchanged. The research thread's harvest headline (8 qualified Blue
loss-states, spreads to 66pp) stands in the 03:55 entry as the
concrete Blue agenda.

## 2026-07-26 03:55 PDT (research thread)

**The Blue gap is now a list of specific mistakes.** Full-power v2
harvest (400 games, 99 Blue losses, 668 candidate states, 40
playout-qualified checks): 8 QUALIFIED loss-states with branch
win-rate spreads from 23.5 to 66.0pp — including a turn-7 main-phase
decision where the best of three legal actions wins 72% and the worst
6%. All harvested from games C16 actually lost. v3 next steps
(declared): stratify checks across opponents (the 40-check bound
consumed on Green-first ordering), re-harvest under v3 rules, then
score C16's actual choices against these outcome-grounded branch
values. The corpus the authored fixtures never contained now exists,
and the pipeline that produces it is one command.

## 2026-07-26 03:25 PDT

No-change cycle: DC1 implementation continues; v3 still uncommitted.
Lift table (v3 working-tree binary): 4 of 5, Blue −10.0, unchanged.
Research thread since the last entry: the Blue loss-state harvester is
built, committed, and machinery-validated (with power fixes declared
before the real run); the complete Blue toolchain — harvester →
qualifier → outcome-regret — plus certify.sh all wait on the v3
commit. Standing request repeated: commit v3 to a hash so
certification and the full harvest can run.

## 2026-07-26 02:45 PDT

No new completed results: DC1 mining implementation continues (its
declaration correctly firewalls the reviewer's 7-of-20 qualification
from serving as DC1 inputs, and pre-licenses only a dominance filter
or pairwise residual — not critic-wide targets — if density passes).
v3 remains uncommitted. Lift table (v3 working-tree binary): 4 of 5,
Blue −10.0, unchanged from the 02:10 entry.

Research-thread relay: v3 C16 aces every existing Blue fixture (100%
top-1, ~zero regret across all policy views) while losing the Blue
lift gate — the gap lives outside the authored corpus. The declared
follow-up harvests Blue's real mixed-field losses, qualifies the
extracted states with the playout seam, and measures outcome-regret
there. This is the concrete Blue plan awaiting the v3 commit.

## 2026-07-26 02:10 PDT

**First v3 lift table: 4 of 5 PASS — the best gate status in the
current world.** Under the corrected rules with the two-agent-verified
frozen C16 artifact: Red +61.2 vs +60.0 (strictly best), RU Aggro
+53.8 vs +46.2 (strictly best — the long-standing RU gap resolved by
the v3 rules fix), White +48.8 vs +42.5 (strictly best), Green
+37.5/+37.5 (tie-pass). **Blue fails by 10.0** — the single deck
between Learned and the crown, again, but a different deck than v2's
endgame and in a fairer world.

| Deck | Learned lift | Best rival | Verdict |
| --- | ---: | ---: | --- |
| Red | +61.2 | +60.0 (HC) | PASS |
| RU Aggro | +53.8 | +46.2 (HC) | PASS |
| White | +48.8 | +42.5 (HC) | PASS |
| Green | +37.5 | +37.5 (HC) | PASS (tie) |
| Blue | +32.5 | +42.5 (HC) | FAIL by 10.0 |

Caveats: working-tree binary (v3 not yet committed to a hash), 80
games/cell (±11pp). The certified version of this table is one
certify.sh run away once v3 commits. Priorities: (1) commit v3;
(2) certify; (3) DC1 mining proceeds for the X=0 class; (4) Blue —
the v2 evidence says deployment/search depth moved Blue most, and the
qualified Blue fixtures (counter-war 50pp, counter-lethal 41pp) are
outcome-relevant, so Blue finally has both a real gap AND real
instruments to diagnose it.

## 2026-07-26 02:00 PDT (research thread)

**v3 freeze verified: the reload determinism check PASSES.** The
reviewer's independent no-refresh invocation loaded the v3 C16
artifact in 0.01s with fingerprint 68126afc5a3e...7413e2f — exact
match to the declared generation fingerprint. The v3 research parent
is frozen and two-agent verified. DC1 (dominated-action mining) is
endorsed: dominance-logic supervision is immune to outcome degeneracy
by construction — the right target for the X=0 class. Remaining for
the v3 era's first certified numbers: commit v3 to a hash, then one
certify.sh invocation on a fresh virgin seed.

## 2026-07-26 01:20 PDT

No-change cycle: the v3 C16 freeze remains declared-not-run (working
tree active; no v3 artifact in the model cache yet). Lift table (v2
binary, historical): 3 of 5. Reviewer standing ready on the assigned
reload determinism check and the first v3 certification run
(tools/certify.sh, committed) the moment the artifact lands.

## 2026-07-26 00:58 PDT

**Environment v3's correctness gate has passed, and the research
thread's framework is now Codex policy:** the notebook adopts
strong-pilot outcome qualification as a precondition for any fixture
label, names the X=0 no-op class the first post-v3 defect target
(contingent on corrected qualification), and declares the v3 C16
control freeze with its exact command. The two agents' methods have
fully merged: qualification rule (reviewer) + preregistration ladder
(Codex) + one-command certification (reviewer, tools/certify.sh,
committed and smoke-testing) = the autonomous improve-and-certify
pipeline the user asked for.

Division of labor on the v3 freeze: Codex generates the artifact; the
reviewer will run the second no-refresh invocation as the independent
reload/fingerprint determinism check once it lands, then certify.sh
is ready for v3's first full gate-panel run on a fresh virgin seed.

Lift table (v2 binary, historical): 3 of 5 — the next table that
matters is v3's first certified one.

## 2026-07-26 00:45 PDT

No new Codex notebook sections (v3 rules fix and web track continue).
Lift table (v2 binary, historical): 3 of 5. Standing items for Codex's
next read, in priority order: (1) the corpus qualification report
(00:35 — 7 of 20 fixtures outcome-relevant; v4 keep/rebuild proposal);
(2) the fixture-qualification rule for all future corpora; (3) X=0 as
the first qualified training target post-v3.

## 2026-07-26 00:35 PDT (research thread)

**Corpus qualification report: 7 of 20 dev-v3 fixtures are
outcome-relevant.** Full sweep with the branch-playout qualifier
(every priority candidate, K=200 strong-pilot games/branch; complete
table in the challenger worktree's docs_qualification_v3.log):
OUTCOME-RELEVANT: begin-combat-growth (17pp), bolt-face-lethal
(100pp), stack-race (100pp), counter-fire-elemental (12.5pp),
counter-lethal-bolt (41pp), counter-war (50pp), disintegrate-player-x
(37.5pp). DEGENERATE (8, including BOTH Force Spike controls — live
at 0.5pp) and FLAT (4, including all remaining White fixtures);
flying-men attack not assessable by the priority seam.

Implications for v4: (1) 65% of dev-v3 measured agreement with lore,
not with winning — top-1/regret on those 13 fixtures should be
demoted from gate metrics to descriptive notes; (2) the X=0 no-op
class is well-posed and qualified — it is the right first training
target post-v3; (3) White's whole probe slice never measured anything
outcome-real, which retroactively explains its metric paradoxes
(S1's White "regression" moved on an outcome-irrelevant scale);
(4) positional/development fixtures may need larger K or
closer-to-terminal states to qualify — a threshold question for v4
design, not a reason to drop the decision classes.

Proposal: keep the 7, rebuild the 13 under the qualification gate,
and add the qualifier to the standard corpus pipeline (it is a
committed one-command tool in the challenger worktree).

## 2026-07-26 00:00 PDT

No new Codex notebook sections (v3 rules fix and web track in
progress). Lift table (v2 binary, historical): 3 of 5. The research
thread's outcome-degeneracy verdict and fixture-qualification rule
(23:50 entry) are the standing items for Codex's next read — they
should gate probe-corpus v4 and all future harvested fixtures.

FT128 reproduction closed: the reviewer's independent verbatim run
(4,078s) reproduces Codex's published audit to the last digit on every
row — live +0.240234/117 blocks, payable −0.167969/1 block, X=0
+0.005859/53 blocks. The v2 era ends with its final result verified
bit-for-bit, like every major result before it.

## 2026-07-25 23:50 PDT (research thread)

**Verdict on the label question — and a deeper finding: the Force
Spike control fixtures are OUTCOME-DEGENERATE.** Ground-truth
playouts (Handcrafted piloting both seats, K=200 per branch, paired
redrawn libraries): Blue wins 0 of 800 games across both branches of
both fixtures. The HRC's mirror-pilot saturation (Q→0.000, zero
variance at H=32) independently agrees — these constructed positions
are dead lost under any competent play. Consequences:

1. No outcome-based signal could EVER score these fixtures — every
   teacher "failure" at the payable state (P1, P1R, distillation,
   FT128) was structurally guaranteed there, independent of teacher
   quality. FT128's +0.24 for Spike measured weak-mirror blunder
   frequency, not position truth.
2. The labels themselves are outcome-unjustifiable at these states;
   only material lore supports them. The label is not "wrong" — the
   question is ill-posed at a dead position.
3. **Fixture qualification rule proposed for v3 (both agents):** a
   behavioral fixture is trustworthy only if branch win rates under
   strong pilots differ and sit bounded away from 0%/100%. The
   reviewer's branch-playout seam (committed, `claude/challenger`) is
   the one-command qualifier; run it per candidate fixture — including
   harvested real-game states — before any label or gate is attached.
4. Scope after tonight: X=0 no-op spends remain a real,
   outcome-justifiable defect class; the payable-tax class needs
   outcome-relevant fixtures before it can even be posed.

## 2026-07-25 23:20 PDT

No new Codex notebook sections (working tree shows web-track styling
work and the v3 rules fix in progress). Lift table (v2 binary,
historical): 3 of 5. The research thread's payable-label finding (23:10
entry) stands as the top item awaiting Codex's read — it should shape
the v3 gate definitions before any new training targets the payable
fixture.

## 2026-07-25 23:10 PDT (research thread)

**Finding that may redirect the defect hunt: the payable Force Spike
fixture's label may be wrong.** The reviewer's material-credit
measurement (independent channel from FT128: end-state hands, life,
board power after K=64 paired mirror continuations) shows the Spike
branch materially AHEAD even in the payable state (life 9.58 vs 5.88
at H=8, opponent board power halved) while hand-advantage launders to
~0.1 card within a few turns. Combined with FT128's payable row
(+0.168 for Spike at full terminal, 2,048 natural terminals), three
independent measurements now agree the Spike branch wins more from the
payable state — the interpretations are (a) mirror-play laundering, or
(b) "Pass when payable" was card-lore, not measurement, and forcing a
payable tax has genuine tempo value. Under (b), eight rejected
treatments were chasing a partly mislabeled target, and the true
generic defect narrows to PURE no-op spends (X=0: no tax, no tempo,
strictly dominated — and FT128's X=0 row was correctly signed, weakly).

**Proposed for v3 (both agents): validate the label before training
against it.** Paired strongest-pilot playouts from both branch states
as a diagnostic-only ground truth (Handcrafted as evaluation
instrument only, per the existing diagnostic-row precedent). If the
label fails, redefine behavioral gates around the X=0 no-op class.
Also rejected honestly: the reviewer's own CA-credit hand-count gate
(card advantage at horizon end is nearly as saturated as win/loss —
do not build auxiliary credit on it; life/tempo channels are the
survivors worth measuring).

## 2026-07-25 22:35 PDT

**FT128: rejected — and the mirror-teacher question is now closed with
evidence at every horizon.** The live control passed (+0.240, 117/128
blocks); the payable control failed decisively in the WRONG direction
(−0.168, CI [−0.191,−0.145], 1/128 blocks), with perfect integrity
accounting (2,048/2,048 natural terminals per row, zero bootstraps,
bit-identical hidden repartition). The reviewer's registered
prediction (made before completion, from the HRC saturation law) is
confirmed exactly, and Codex's decision text independently credits the
convergence. Full-terminal Learned-mirror outcome credit joins the
closed list; the complete map now reads: H=0/4 wrong-signed, H≤8
live-only signal band, H=128 wrong-signed — no mirror-outcome horizon
carries the payable distinction.

**The two agents have converged on the same next axis independently:**
card/resource-advantage auxiliary credit (Codex's "leading generic
candidate"; the reviewer's CA-credit axis, preregistered with
reject-only gates and implementation in progress on the seam that
returns continuation end-state material summaries). To be declared
under v3 per the reset rule.

Also accepted: Codex's status reconciliation — the 21:48 entry called
v3 "live" prematurely; v3 is live only when the rules implementation,
cache invalidation, and tests land. Correct nit, adopted.

Lift table (v2 binary, historical): 3 of 5.

## 2026-07-25 22:15 PDT

No new Codex claims (v3 rules-fix implementation in progress; FT128
still scoring — with searching mirror continuations played to natural
termination at K=1024, wall time in hours is plausible; a cost note
belongs with the result). Lift table (v2 binary, historical): 3 of 5.

Research-thread finding delivered for Codex (full table in
CLAUDE-PLAN.md): the **HRC saturation law** — the live Force Spike
preference is horizon-stable through H=8 (+0.037 to +0.043) then
annihilated by terminal saturation (all worlds lost by H=32; zero
delta, zero variance). Combined with the teacher audit's wrong-signed
payable state at H=0/H=4, mirror continuations appear to have NO
horizon window that scores the payable state correctly. Registered
prediction: FT128's payable gate fails. Queued axes if confirmed:
card-advantage auxiliary credit (material delta at horizon end as a
shaped signal — the information terminal outcomes erase) and
opponent-tempo-aware asymmetric evaluation. Both ideas are available
to Codex now rather than after FT128.

## 2026-07-25 21:48 PDT

**Environment v3 declared: a cleanup-discard rules correction.** A
genuine rules bug (cleanup-step discard) affected real trajectories,
most for Blue and RU. The correction is non-negotiable — a rules
engine must be right before anything else — and the declaration
handles the fallout correctly: v2 artifacts/lift tables/strength
results are non-comparable and nothing v2 gets promoted; the running
FT128 remains valid strictly as a v2 signal-sufficiency audit, its
ANSWER (does terminal credit contain the hold/spend distinction)
transferring conceptually even though its artifact cannot.

Reviewer's note: this is the fourth world-reset today but the first
forced by correctness rather than design. The v1→v2→v3 pattern
strengthens the earlier recommendation — a rules-conformance fixture
suite (cleanup, priority, mana, SBAs against known-correct game logs)
would catch this class before it invalidates a day of artifacts.
Suggest adding it to the v3 gate list; the reviewer's research thread
can contribute fixtures.

Lift table (v2, now historical): 3 of 5. HRC extraction and FT128
continue; both will be recorded as v2 science.

## 2026-07-25 21:45 PDT

Two developments: (1) **Codex opened the web track** — "Old School
Arena," a browser-playable client with a disciplined three-level
verification ladder (data contract → structural UI invariants →
rendered browser journey, with only level 3 closing visual
milestones). The same evidence culture, applied to UI. (2) FT128
remains unrecorded on Codex's side; the reviewer's verbatim K=1024
run is deep in its scoring phase (~35 min — full-terminal
counterfactuals are expensive by design).

Reviewer's research thread (now permanently active per user
direction): the horizon-response curve sweep (H ∈ {2..32} on the
live/payable fixtures) is running; its curve plus FT128's endpoint
will bracket the terminal-credit question from both sides tonight.

Lift table deterministic-identical (3 of 5).

## 2026-07-25 21:16 PDT

No-result cycle: FT128 result still unrecorded (new wip commits landed;
Codex may be running it in parallel with the reviewer's verbatim run,
which is mid-scoring — 1,024 terminal worlds per fixture is the day's
heaviest single computation). Lift table deterministic-identical (3 of
5). If both runs complete, the bit-for-bit agreement check applies as
usual.

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
