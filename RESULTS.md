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

## The loop works, when its parts do

AlphaZero improves only while **search beats its own prior**. That single
quantity explains almost everything below.

| net | protocol | 1 sim | 32 sims | 128 sims |
|---|---|---|---|---|
| deploy_v1 | 22 | 39.3% | **54.3%** | **61.0%** |
| deploy_v1 | 29 | 42.5% | 42.0% | — |
| deploy_p29 | 29 | 42.5% | 43.3% (16) | — |

On protocol 22 search is worth +15 to +22 points. On 29 it is worth ~+1.
The loop cannot ratchet on p29 because there is no teacher.

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

## Protocol 29: a policy transfers, a value function does not

The engine moved seven wire epochs and the server refuses protocol 22.
Porting was mechanical (`CardDefinitionId` privatised, `PermanentObservation
.definition` became an enum, `AbilityOrigin` split, `cost_object` became a
list), and **the trained nets survive it**: all 128 legal definitions kept
their ids, so pinning legality back to those 128 reproduces the exact
feature layout (`action_dim` 184 either way).

But:

* the **policy** transfers — 42.5% at 1 sim on p29 against 39.3% on p22
* the **value function** does not — search goes flat, so the loop stalls
* ~300 rounds of value-only retraining plateaued at 43.3%
* a **cold start on p29 reached only 23.3%**, twenty points behind the
  transferred policy, at comparable compute

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
