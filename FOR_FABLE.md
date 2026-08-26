# Follow-up for review

Your previous advice (`ADVICE.md`) was followed in order. It was right about
the method and wrong about the suspect, and the method found the cause.

## What your advice produced

**Counters first (your step 1).** Instrumented every silent failure you
listed, exposed as `spz_core.search_stats()`. On protocol 29, 16 sims,
105k determinizations:

```
det_ok 104848  det_err 0
opp_action_none 0  opp_apply_fail 0  our_apply_fail 0
depth_cap 3  our_nodes/iter 3.3  reached_opponent 56%
```

None of the silent failure modes fires.

**Experiment A (oracle world).** `AZ_ORACLE_WORLD=1` hands the search the
true hidden state. Identical result, paired −1.7. **Determinization is
exonerated twice**: it never fails, and perfect information buys nothing.
Your leading candidate, and mine, is dead.

**Experiment B (rollout leaf).** Needed `rollout_to_end` fixed first — it
scored via `greedy_index`, cloning and re-featurising per action, which is
why it had never produced a number in three attempts. With the factorised
head it runs: **−15.0 ± 7.1**. The value head is much *better* than
rollouts, so it is not the bottleneck either.

**Your step 0 (p22 baseline) is what cracked it** — but not via the counter
diff. Computing the search value PAIRED, as you insisted, on one net:

| protocol / net | 1 sim | 16 sims | 32 sims |
|---|---|---|---|
| 22, `deploy_v1` | 43.3% | 46.7% (+3.3 ± 4.3) | 55.0% (**+11.7 ± 3.8**) |
| 29, `deploy_p29` | 42.5% | 45.0% (−1.7 ± 6.1) | 50.8% (**+8.3 ± 4.2**) |

**Search works on protocol 29. It just needs more than 16 sims.** Every p29
training run had used 16, so the loop was starved of a teacher. That was the
whole mystery.

Two of our recorded findings were also wrong: "+15 to +22 on p22" was never
paired (it compared 1-sim play of one net with 32-sim play of another), and
"search is not a teacher on p29" was an artifact of the budget.

## Where we are now

`deploy_p29` plays **50.8% at 32 sims** — parity — and training is running
at 32 sims from that checkpoint, ratcheted (gate, keep best, revert on any
decline, floor set by gating the starting net).

## What we would value your view on

1. **The threshold.** Search is worth ~0 at 16 sims and +8 to +12 at 32, on
   both protocols. We suspect FPU: unvisited actions take Q=0, the worst
   value on a [0,1] scale, so with a confident prior a low-P action is never
   tried at a small budget — making small-budget search a 1-ply argmax. Is
   FPU = parent mean Q the right fix, and would you expect it to move the
   threshold down enough to matter? Cheaper search is worth a lot to us: p29
   costs ~10x p22's wall clock per game.
2. **The 63% vs 80%.** Iterations that reach an opponent decision at 32
   sims: 80% on p22, 63% on p29. Unexplained. Does that gap look like a
   cause of the 8.3-vs-11.7 difference, or a symptom of something else?
3. **Ordering.** Given search now demonstrably teaches on p29, is training
   at 32 sims the right thing to spend a machine on, or would you fix FPU
   and the representation first? Our known rule-level defects: attacks into
   a strictly better blocker 0.86/game, skips a land drop 0.60/game, keeps a
   no-mana opening hand 0.14/game. Bag-of-cards features cannot express a
   pairwise creature matchup.
4. **Anything in the counters we are not reading correctly.** They are in
   `spz-core/src/mcts.rs` (`mod stats`), and the harness that prints them is
   in the session scratch, easily rebuilt from `az_gate` + `search_stats`.

Repo state: `RESULTS.md` has the corrected numbers and the ruled-out table,
`ROADMAP.md` the ordering, `AGENTS.md` the traps. One caveat we would
flag about our own data: nearly every wrong conclusion this week came from
an unpaired comparison, a wall-clock guard that bound, or a fallback that
looked like a decision.
