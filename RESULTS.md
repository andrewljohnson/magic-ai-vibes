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

## Search is worth +8.3 — but not the search that trains us

AlphaZero improves only while **search beats its own prior**. Measured
PAIRED on identical games (the gate is deterministic, so the per-game score
vector `az_gate` returns makes the comparison exact), 120 games, each net
against its own 1-sim play:

| protocol / net | 1 sim | 16 sims | 32 sims |
|---|---|---|---|
| 22, `deploy_v1` | 43.3% | 46.7% (+3.3 ± 4.3) | 55.0% (**+11.7 ± 3.8**) |
| 29, `deploy_p29` | 42.5% | 45.0% (−1.7 ± 6.1) | 50.8% (**+8.3 ± 4.2**) |

Two things follow, and the second is the important one.

**Search needs more than 16 sims.** At 16 it is inside the noise on p22 and
negative on p29; at 32 it is worth +11.7 and +8.3. There is a threshold.

**The in-tree opponent model decides whether search teaches at all.** All of
the above uses the HANDCRAFTED in-tree opponent, which is what `az_gate`
runs. Self-play (`az_stream_episodes`) uses GREEDY. Same net, same 120
games, same 32 sims:

| in-tree opponent | score | paired vs 1 sim |
|---|---|---|
| handcrafted | 50.8% | **+8.3 ± 4.2** |
| **greedy — what self-play uses** | 40.0% | **−2.5 ± 3.8** |

A 10.8-point swing. **The search that generates our training targets is
worse than no search**, so every round distils a teacher weaker than the
student. That is the direct explanation for the plateau: training at 32
sims from the 50.8% net still walked 49.2 → 43.3 → 40.0 → 30.8 over 120
rounds, reverting every gate.

Caveat on the comparison: the benchmark opponent IS the handcrafted bot, so
part of that 10.8 is simply "model the opponent you actually face". In
self-play the real opponent is our own policy. But the training implication
stands on its own — the configuration that produces our targets is the one
where search does not beat its prior.

**Corrections to our own earlier record.** "+15 to +22 on protocol 22" was
never paired: it compared 1-sim play of one net (`az_best`, 39.3%) with
32-sim play of another (`deploy_v1`, 54.3%). And "search is not a teacher on
protocol 29" was an artifact of measuring at 16 sims.

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
