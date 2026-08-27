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

**But the policy does not absorb what search finds.** 180 rounds, ~8600
self-play games, promotion gated on the mirror match rather than on the
handcrafted bot:

| | |
|---|---|
| promotions | **0** |
| reverts | 2 |
| best net after 180 rounds | **byte-identical to the warm start** |
| mirror (current vs best) | 35.3, 50.0, 50.0, 36.4, 50.0, 45.5, 51.5, 53.1 |
| handcrafted gate | 45.8 → 45.0 → 40.0 → 34.6 → 46.7 → 45.0 → 41.7 → 35.8 |

The mirror hovers at 50%: the trained net is indistinguishable from the net
it started at. So the loss is **between the search and the gradient** — the
targets, or the fit — not in the search.

This also ruled out the leading theory for the decline, which was that the
ratchet reverts on the *handcrafted* gate and so discards nets that genuinely
improve at self-play. Promoting on the mirror instead changed nothing: nets
are not being wrongly discarded, they are not improving. Worth one run to
learn.

The mirror gate does disagree with the handcrafted gate, in both directions —
at round 19 the gate read 45.8% (above the 45.0% floor, so it would have
**promoted**) while the mirror read 35.3%.

**The policy never fits its own targets.** Across all 180 rounds
`pol_ent` sits at 1.38–1.45 while `tgt_ent` is ~0.92, and `pol_ce` ~1.35–1.41.
The policy stays much flatter than the targets it is trained on and never
converges toward them. Whether that is too little signal in the targets or
too little fitting is the open question — see ROADMAP.

**Corrections to our own earlier record.** "+15 to +22 on protocol 22" was
never paired: it compared 1-sim play of one net (`az_best`, 39.3%) with
32-sim play of another (`deploy_v1`, 54.3%). "Search is not a teacher on
protocol 29" was an artifact of measuring at 16 sims. And "self-play distils
a weaker teacher" is withdrawn, as above.

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

What does differ on p29: iterations reach an opponent decision 63% of the
time at 32 sims against p22's 80%, and a game costs roughly 10x the wall
clock. Neither is explained.

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

From `playout_log.py`, rule-level errors only — judgement calls excluded:

| rate | defect |
|---|---|
| 0.86/game | attacks into a strictly better blocker (2/2 into an untapped 4/4) |
| 0.60/game | skips a land drop entirely |
| 0.14/game | keeps an opening hand with no land, no Mox, no Lotus |
| 0.07/game | casts an X spell for X=0 |

The encoding is bag-of-cards counts per zone. It cannot express "my 2/2
versus their 4/4" as a pairwise relation, which is exactly the combat
defect above. Deep-sets pooling over permanents is the standard fix.
