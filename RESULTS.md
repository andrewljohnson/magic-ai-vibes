# Results

What we learned, and what would waste your time to rediscover. Numbers
only; war stories cut.

**Reading a number.** The gate plays our bot against the engine's built-in
bot, alternating seats, rotating 14 decks. It reports OUR win rate, so
**50% is parity**. The gate is EXACTLY deterministic — fixed seeds, fixed
opponent, no root noise — so re-gating the same weights reproduces the
result game for game. Comparing two nets on the same gate is paired and
exact; the ±SE only matters for generalising to other opponents.

---

## Search beats the policy. The policy does not absorb it.

AlphaZero improves only while **search beats its own prior**. Measured
PAIRED on identical games (the gate is deterministic, so the per-game score
vector `az_gate` returns makes the comparison exact), 120 games, each net
against its own 1-sim play:

| protocol / net | 1 sim | 16 sims | 32 sims |
|---|---|---|---|
| 22, `deploy_v1` | 43.3% | 46.7% (+3.3 ± 4.3) | 55.0% (**+11.7 ± 3.8**) |
| 29, `deploy_p29` | 42.5% | 45.0% (−1.7 ± 6.1) | 50.8% (**+8.3 ± 4.2**) |

**Search needs more than 16 sims.** At 16 it is inside the noise on p22 and
negative on p29; at 32 it is worth +11.7 and +8.3. There is a threshold, and
FPU is not what causes it (below).

**In self-play's own domain, search is a real teacher.** Mirror match,
alternating seats, same seeds, both seats our own net, greedy in-tree
opponent — exactly the configuration self-play trains on. 32 sims against
the raw 1-sim policy, 207 finished games:

> **55.8% ± 3.5**

This retired an earlier conclusion of ours. We had measured the greedy
in-tree model at **−2.5 ± 3.8** *against the handcrafted bot* and concluded
"self-play distils a teacher weaker than its student". That measurement is
real but answers a different question: against the handcrafted bot the
greedy model is simply the wrong model of the opponent, and a best-response
to the wrong opponent losing to no search says nothing about self-play,
where the real opponent **is** our policy. Report both numbers going
forward: the handcrafted-model gate (exploitation of one known opponent) and
the own-model mirror (portable strength).

**Training does not fail to improve the net. It destroys it.** Measured
correctly — a net per seat, seats swapped, 32 sims both sides, greedy
in-tree opponent:

| comparison | score | games |
|---|---|---|
| warm start vs ITSELF (symmetry check) | 42.4% ± 6.1 | 66 |
| **`az_mir` round 182 vs its own warm start** | **27.3% ± 3.4** | 172 |

After 182 rounds and ~8700 self-play games the net loses at better than
2-to-1 to the checkpoint it started from. The handcrafted gate said the
same thing more quietly — nine gates, every one below the 45.0% floor,
45.8 → 37.9.

**A broken gate hid it the whole time.** The mirror gate meant to catch
exactly this reported ~50% for 182 rounds and never once reverted for
cause. `az_h2h` built ONE policy and used it for BOTH seats, varying only
the iteration count, so `mirror_vs_best` played current-vs-current on half
the specs and best-vs-best on the other half and combined them as though
the halves were opposite seats of one head-to-head. Both halves are
symmetric by construction. The tell was 50.0% twice to the decimal; the
symmetry check above reproduces it (42.4% ± 6.1, statistically 50%).

Two earlier claims die with it: "the policy never absorbs what search
finds" (it absorbs something, and that something is harmful) and "the
ratchet was not the problem" (it was never tested — the gate it ran on
could not see a difference).

So the open question is no longer whether the loop climbs. It is **why the
targets are actively harmful**, given that search itself plays better than
the policy (55.8% ± 3.5, a measurement that only varies iteration count and
so was unaffected by this bug).

**Why the targets are harmful: we train on a search we never measured.**
Experiment 1 measured search with NO root noise. Self-play generates its
targets with 25% Dirichlet noise, and that is a different, much worse
search. Same net, same mirror harness, 32 sims against the raw 1-sim
policy:

| root noise | alpha | score vs raw policy |
|---|---|---|
| 0.00 (Experiment 1) | — | **55.8% ± 3.5** |
| 0.05 | 0.3 | **54.3% ± 3.8** |
| 0.10 | 0.3 | 51.2% ± 3.9 |
| 0.10 | 1.0 | 52.1% ± 3.8 |
| **0.25** | 1.0 — what we trained on | **44.2% ± 3.8** |

An 11.6-point swing from teacher to anti-teacher, monotonic in the noise
FRACTION. Alpha is irrelevant over this range (51.2 vs 52.1 at the same
fraction), so the concentration of the noise does not matter — how much of
the prior it replaces does. At 32 visits over ~6 actions, replacing a
quarter of the prior with near-uniform noise does not perturb the search,
it dominates it. AlphaZero uses the same 25% at 800 visits, where it is a
small perturbation.

0.05 keeps essentially all of search's edge, and is the setting training
now runs at. **0.10 is already neutral (51.2%), and a run at 0.10 degraded
the net over 25 rounds and was reverted by the gate** — so "a teacher worth
1 point" is not enough to climb.

**And add-k flattens the target past the prior.** The policy target is
`(visits + k) / sum`, with k=1 — seven pseudo-visits on 32 real ones.
Measured over ~10k searched decisions, entropy of the target against
entropy of the prior that generated it:

| root noise | add-k | target ent | prior ent | target is |
|---|---|---|---|---|
| 0.00 | 0.0 | 1.068 | 1.232 | sharper (−0.164) |
| 0.00 | **1.0** | 1.327 | 1.232 | **flatter (+0.095)** |
| 0.25 | 0.0 | 1.094 | 1.129 | sharper (−0.035) |
| **0.25** | **1.0** — ours | 1.296 | 1.129 | **flatter (+0.167)** |

Add-k flips the sign in both noise settings. A target flatter than the
prior can only push the policy toward uniform: it is trained, every round,
toward a blurred copy of itself. Noise narrows the sharpening margin
(−0.164 → −0.035) but does not flip it; noise's damage is to the *play*
that produces the visits, per the table above.

Add-k was introduced to stop target entropy collapsing at **8** sims, where
a 8-visit histogram really is too coarse to trust. It was never revisited
when the budget moved to 32. Search still finds something real — it moves
the top action on 8.0% of decisions noise-free — but that signal is
delivered inside a distribution flatter than the prior.

**Corrections to our own earlier record.** "+15 to +22 on protocol 22" was
never paired: it compared 1-sim play of one net (`az_best`, 39.3%) with
32-sim play of another (`deploy_v1`, 54.3%). "Search is not a teacher on
protocol 29" was an artifact of measuring at 16 sims. And "self-play distils
a weaker teacher" is withdrawn, as above.

## The loop climbs locally and goes nowhere absolutely

After the target fixes (noise 0.05, add-k 0) the run promoted four times in
124 rounds with zero reverts, and each promotion was a genuine paired win
over the previous best:

| round | gate | mirror vs previous best | |
|---|---|---|---|
| 24 | 44.2% | 56.3% | promoted |
| 49 | 45.8% | 54.7% | promoted |
| 74 | 47.5% | 58.9% | promoted |
| 99 | 49.2% | 51.1% | kept |
| 124 | 40.8% | 57.7% | promoted |

Against a FIXED anchor — the original warm start the run began from, 200
paired games, seats swapped, no noise — the round-124 best scores:

> **45.9% ± 3.8**

**How the unfinished games were scored** (Fable asked, correctly, and this is
the sixth time a guard has shaped a number here): the 29 games that did not
finish inside the 900s budget were **dropped**, not counted as draws or
losses. 45.9% is over the 171 that finished. The bound that matters: if every
dropped game had been a win the figure would be 53.8%, if every one a loss
39.3%. So the exclusion cannot by itself manufacture the result — but it also
cannot rule out parity, which is the honest reading either way.


Not measurably better than where it started. The interval includes 50% and
the point estimate is below it, while the mirrors that promoted it read 56.3
through 58.9.

**Each net beats its predecessor; the lineage does not improve.** The
promotion rule compares against the MOVING best, so it only ever asks a
local question and cannot see this. It is the cycling that
best-response-to-current-policy is prone to, and it is why the handcrafted
gate could fall 8.4 points at round 124 while the mirror promoted.

Two lessons, both general: a ratchet needs a fixed reference somewhere in
it, and "beats its predecessor" is not a measurement of progress.

## Play-the-turn-out: helps combat, does NOT fix mana burn

On first visit the search now plays on with the prior's argmax through both
seats instead of evaluating the afterstate immediately, so an iteration sees
its own combat resolve and its own step end. Same net, same 16 seeds, 128
sims, raw counts (16 games is a small sample — read these as directional):

| defect | OFF | ON |
|---|---|---|
| attacks into a strictly better blocker | 9 | **5** |
| animated a tapped land | 9 | 6 |
| **makes mana it cannot spend** | **16** | **20** |
| skips a land drop | 20 | 21 |

Record was 7-9 both ways. The paired strength mirror, 200 games at 32 sims
with the play-out verified running (636,947 expansion calls, 2.5M plies
played), came back:

> **50.3% ± 4.0** (157/200 finished, 43 dropped — the play-out makes games
> slower, so more hit the budget)

**Strength-neutral, at three times the search cost.** It changes behaviour
without changing outcomes, so it does not earn a place in training.

**The combat defect moved and the mana defect did not**, which is the
informative part. Combat consequences are LARGE in value terms — a creature
dies, several damage lands — so once the search can see them it avoids them.
Mana burn is **one point of life**, which is almost certainly below the value
head's resolution: making the cost visible does not help if the cost is too
small to register against the noise in a win-probability estimate.

So "the search cannot see the consequence" was only half the story. For
burn, the other half is that the consequence is tiny. That is an argument for
removing the action rather than waiting for the loop to learn it — the
credit-assignment fix is real but does not reach this defect.

## Ruled out, with evidence

Instrumented counters (`spz_core.search_stats()`) over 105k determinizations
on p29, plus targeted experiments:

| suspect | verdict |
|---|---|
| determinization failing | **no** — 0 errors in 105k samples |
| determinization implausible | **no** — `AZ_ORACLE_WORLD=1` (true hidden state) changes nothing |
| apply / opponent-model failures | **no** — 0 of each |
| the value head | **no** — a rollout leaf is far *worse* (−15.0 ± 7.1) |
| depth cap, terminal handling | **no** — 3 hits in 400k iterations |
| first-play urgency (FPU) | **no gain** — +0.0 ± 3.7 at 16 sims (c_puct 1.5); −4.2 ± 3.6 at c_puct 2.5 |
| the ratchet discarding good nets | **no** — promoting on the mirror instead changed nothing (0 promotions in 180 rounds) |
| clock-truncated games poisoning labels | **already handled** — `r == -2` sets `keep=False`; only decision-cap (`-1`) games are scored as losses |

## What actually moved the needle

* **The value head must rank SIBLINGS, not positions.** Trained only on the
  game outcome it learns "am I winning" — every state in a game shares one
  label — and never has to separate two positions one ply apart. That is
  what search needs from it.
* **A saturating sigmoid destroyed that ranking.** BCE against hard 0/1
  pushes logits outward; out there sigmoid is flat and sibling differences
  vanish. Label smoothing capped the logits and multiplied the usable
  spread by six. Search went from worth +0.7 to worth +13.3, and the gate
  from 44.0% to 49.2%. **Six separate bugs on this project have been an
  output SCALE rather than a learning failure. Print the range first.**
* **Canonical PUCT.** The exploration term was
  `c·P·sqrt(ln A/(1+N))` instead of `c·P·sqrt(N_parent)/(1+N)`. Unvisited
  actions default to Q=0 — the worst score on a [0,1] scale — so nothing
  could overcome it. The median decision put ALL 32 visits on ONE action.
* **Root Dirichlet noise.** Visit counts ARE the policy's training target,
  so a sharper policy makes sharper visits makes a sharper policy. Without
  noise, target entropy collapsed 0.965 → 0.226 in nineteen rounds.
* **Score truncation the way the evaluator does.** A game hitting the
  decision cap scored 0.5 paid a losing policy more for stalling than for
  losing. Scoring it as the gate does (a loss) fixed it. Third attempt was
  the right one.
* **Best-net retention with a floor.** Training from a good net reliably
  degrades it, and no training metric predicts the gate — losses and
  entropies improved while strength fell 12 points. Gate, keep the best,
  revert on any decline. **A warm start must gate its starting net first:**
  without that the first gate promoted 26.7% as "best" over the 48.3% net
  it started from, and the ratchet then defended the worse one.

## Protocol 29: the port was clean; the loop is what is stuck

The engine moved seven wire epochs and the server refuses protocol 22.
Porting was mechanical (`CardDefinitionId` privatised, `PermanentObservation
.definition` became an enum, `AbilityOrigin` split, `cost_object` became a
list), and **the trained nets survive it**: all 128 legal definitions kept
their ids, so pinning legality back to those 128 reproduces the exact
feature layout (`action_dim` 184 either way).

But:

* the **policy** transfers — 42.5% at 1 sim on p29 against 39.3% on p22
* ~300 rounds of value-only retraining plateaued at 43.3%
* a **cold start on p29 reached only 23.3%**, twenty points behind the
  transferred policy, at comparable compute

We previously recorded here that "the value function does not transfer —
search goes flat". **That was wrong**, and it was wrong for a specific
reason worth keeping: it was measured at 16 sims, which is below this
search's usefulness threshold. At 32 sims search on p29 is worth +8.3 ± 4.2,
and both retraining runs above also ran every round at 16 sims. The p29
value head separates siblings slightly *better* than the p22 one
(0.029/26% vs 0.022/29%).

What genuinely differs on p29: iterations reach an opponent decision 63% of
the time at 32 sims against p22's 80%, and a game costs roughly 10x the wall
clock. Still unexplained, and ROADMAP argues it is not worth restoring the
p22 vendor to chase.

## Speed

Search is ~1.6x faster than it was, from two real wastes:

* **`apply` re-enumerated.** `is_legal_action` is
  `legal_actions(player).contains(action)` — a full second enumeration of
  the list search just chose from. Every ply enumerated twice.
  `apply_enumerated` (vendor patch, PR pushed upstream) cut total search
  time 30%.
* **`observe` on forced plies.** 63% of plies have exactly one legal
  action, and each was building a whole observation to discover it had no
  choice. Enumerate once, build zones only when branching.

Rejected after measurement: catalog cloning (an `Arc` bump), JSON observe,
table construction, the in-tree opponent model, descent depth, the
reconstruction checkpoint (~1%). The node-state cache is worth 1.25x at
`redeterminize_m=4`; m=8 buys 1.8x but costs 6.7 points of strength.

## Deployment: the hosted bot was never searching

For four days the hosted bot played a raw 1-ply pick. Three separate
fingerprint gates rejected every server observation and one bare
`except Exception` in `hosted_policy.py` swallowed all three, so the only
symptom was weak play. The tell was in the transcripts all along: `median
0ms, max 1ms` per move.

`simulationFingerprint` hashes **every simulation source file plus the
dependency closure**, so any patch of ours changes it — our additive
accessors included. It also pins an exact upstream revision, and three
different values are in play:

| build | fingerprint |
|---|---|
| our pinned upstream `12366c87` | `99d5d284…` |
| upstream HEAD (156 commits later) | `ae23099d…` |
| **what the server runs** | `267b227d…` |

So no build of ours can ever match the server, and rebuilding the engine
from our patched vendor only moves the mismatch (it fixes engine ↔
`spz_core` and breaks engine ↔ server).

The fix is an opt-in `SPZ_ACCEPT_FINGERPRINT` naming the exact fingerprint
to trust, applied in all three places that check it: the observation gate,
the checkpoint gate, and the field-by-field rebuild comparison. The third
normalises the fingerprint **string only** inside the nested checkpoint and
still compares every other byte of game state, so the integrity check that
would catch genuine rules drift stays armed. LOCAL ONLY — never propose
upstream.

Verified after the fix: 768 iterations over 6 decisions, 0 determinization
errors, 0.66s median per move at 128 sims (room clock is 60s), so the bot
now runs at 512 sims hosted.

**The lesson is the one this project keeps relearning:** a fallback that
does not announce itself is indistinguishable from working code. Search
failures now print.

## What the bot does wrong

From `playout_log.py`, rule-level errors only — judgement calls excluded.
24 games, current best net at 128 sims:

| rate | defect |
|---|---|
| **1.33/game** | **makes mana it cannot spend, and burns for it** |
| 0.46/game | skips a land drop entirely |
| 0.25/game | attacks into a strictly better blocker |
| 0.08/game | keeps an opening hand with no land, no Mox, no Lotus |
| 0.08/game | casts an X spell for X=0 |
| 0.04/game | pumps an opponent's creature |
| 0.00/game | discards a land it could have played |

**Mana burn is the biggest single defect, by a factor of three.** Mana burn
is a live rule in this format, so activating a mana ability with nothing to
spend it on is not idle — it deals that much damage to us. Found by playing
the hosted bot: it tapped Mishra's Factory for colourless during its OWN
upkeep and burned for 1. The detector fires when a mana ability is chosen
and the same decision offers no spell to cast and no ability with a mana
cost, so the mana provably has nowhere to go.

Why the loop does not train this away is the interesting part: the cost
arrives as one point of life at the END of the step, several plies after
the decision, and the value head sees only the eventual game result. This is
a credit-assignment defect with a rule-level signature, which makes it a
good test case for whether the loop can learn a delayed self-inflicted cost
at all.

The encoding is bag-of-cards counts per zone. It cannot express "my 2/2
versus their 4/4" as a pairwise relation, which is exactly the combat
defect above. Deep-sets pooling over permanents is the standard fix.
