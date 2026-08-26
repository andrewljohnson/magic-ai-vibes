# Follow-up for review

`ADVICE.md` was followed in order. Its method found the cause; its suspect
was wrong. Here is what it produced and where we are stuck.

## Your suspects, tested and cleared

Counters on every silent failure you listed (`spz_core.search_stats()`),
protocol 29, 32 sims, 400k determinizations:

```
det_ok 386816   det_err 0
opp_action_none 0   opp_apply_fail 0   our_apply_fail 0
depth_cap 0   terminal 15490   our_nodes/iter 4.0
iterations reaching an opponent decision: 63%
```

| suspect | verdict |
|---|---|
| determinization failing | **no** — 0 errors in 400k |
| determinization implausible | **no** — `AZ_ORACLE_WORLD=1` (true hidden state) changes the result not at all |
| apply / opponent-action failures | **no** — 0 of each |
| value head | **no** — a rollout leaf is far *worse* (−15.0 ± 7.1) |

Experiment B needed `rollout_to_end` fixed first: it scored via
`greedy_index`, cloning and re-featurising per action, which is why it had
never produced a number in three attempts.

## What the paired measurement found instead

Your insistence on pairing is what cracked it. 120 games, each net against
its own 1-sim play:

| protocol / net | 1 sim | 16 sims | 32 sims |
|---|---|---|---|
| 22, `deploy_v1` | 43.3% | +3.3 ± 4.3 | **+11.7 ± 3.8** |
| 29, `deploy_p29` | 42.5% | −1.7 ± 6.1 | **+8.3 ± 4.2** |

Search works on both protocols above ~16 sims. Two of our recorded findings
were wrong: "+15 to +22 on p22" was never paired (1-sim of one net vs
32-sim of another), and "search is not a teacher on p29" was a 16-sim
artifact.

**But retraining at 32 sims still degraded monotonically** — 49.2, 43.3,
40.0, 30.8 over 120 rounds from the 50.8% net, every gate reverted. So the
budget was not the whole story.

## The actual blocker

The in-tree opponent model. Same net, same games, same 32 sims:

| in-tree opponent | score | paired vs 1 sim |
|---|---|---|
| handcrafted (`az_gate`) | 50.8% | **+8.3 ± 4.2** |
| **greedy (`az_stream_episodes`)** | 40.0% | **−2.5 ± 3.8** |

**Self-play generates its targets with the model where search is worse than
no search.** The loop distils a teacher weaker than its student, every
round. Every +8.3 we used to justify training was measured with a model
self-play never uses.

The gate may use the handcrafted bot; self-play may not — that is scripted
play knowledge and the pure-build rule forbids training against it.

Caveat we would flag: the benchmark opponent IS the handcrafted bot, so part
of the 10.8-point gap is "model the opponent you actually face". In
self-play the true opponent is our own policy. We do not think that rescues
it, because the target-producing configuration is still the one where search
loses to its prior — but tell us if that reasoning is wrong.

## What we would value your view on

1. **How do we get a self-play in-tree opponent that is actually good,
   without importing handcrafted knowledge?** Untested candidates: sample
   the opponent's move from the policy rather than greedy argmax; give the
   opponent its own small search; use the previous best checkpoint as the
   opponent model. Is one of these obviously right, or is the single-observer
   framing itself the problem?
2. **Is +8.3 with an accurate opponent model and −2.5 with our own policy
   telling us the search is only ever exploiting a *known* opponent** — i.e.
   that this architecture cannot self-improve at all, and the p22 61% was
   likewise exploitation of the built-in bot rather than strength?
3. **FPU.** Unvisited actions take Q=0, the worst score on [0,1], so a
   confident prior never explores at low budget. Is that the reason the
   threshold sits between 16 and 32 sims, and is FPU = parent mean Q the fix?
   Cheap search matters: p29 costs ~10x p22 per game.
4. **63% vs 80%** of iterations reaching an opponent decision (p29 vs p22 at
   32 sims), and the ~10x wall-clock gap. Both unexplained.

## State

`checkpoints/deploy_p29` — 50.8% at 32 sims on protocol 29 (parity),
deployable once one fingerprint bug is fixed. `checkpoints/deploy_v1` —
55.0% at 32 sims on protocol 22, which the server refuses.
`RESULTS.md` has the numbers and the ruled-out table, `AGENTS.md` the traps.
Nearly every wrong conclusion this week came from an unpaired comparison, a
wall-clock guard that bound, or a fallback that looked like a decision.
