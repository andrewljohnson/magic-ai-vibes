# Roadmap

## The problem, in one paragraph

We have a 61% bot on protocol 22 that cannot be deployed, and a 43% bot on
protocol 29 that can. Training on p29 has not improved past 43.3% because
**search there has no measurable value over its own prior**
(+0.8 points, inside a ±4.5 gate), and an AlphaZero
loop climbs only at the rate its search beats its policy. On p22 that
number was +15 to +22.

## Do this first

**Training at 32 sims is running.** Paired measurement shows search is worth
+8.3 points at 32 sims on protocol 29 and −1.7 at 16, and every previous p29
run used 16. The loop was starved of a teacher. `deploy_p29` already plays
50.8% at 32 sims — essentially parity — so the question is whether the loop
can now ratchet from there.

## Then

1. **Why does p29 need ~10x the wall clock per game, and reach an opponent
   decision less often (63% vs 80% at 32 sims)?** Both unexplained, and the
   second bounds how much search can be worth.
2. **First-play urgency.** Unvisited actions get Q=0, the worst score on a
   [0,1] scale, so with a confident prior an action never gets tried at a
   small budget. That is very likely *why* the threshold sits between 16 and
   32 sims, and fixing it (FPU = parent mean Q) should make cheaper search
   work.
3. **Representation.** Bag-of-cards features cannot express a pairwise
   creature matchup, and the bot attacks into strictly better blockers 0.86
   times a game. Deep-sets pooling over permanents. Invalidates every
   checkpoint, so it is a phase change.

## Cheaper things worth doing

* Cap the wall-clock guards far above the workload, or remove them. They
  have silently decided four separate measurements.
* Gate on more games. 120 games is ±4.5; most of our arm comparisons were
  inside the noise.
* The `apply_enumerated` PR is pushed to `andrewljohnson/penta` branch
  `apply-enumerated` and needs opening. ~30% of search time.

## Tried and rejected — do not redo

| idea | outcome |
|---|---|
| Cold start on p29 | 23.3% after ~500 rounds, 20 points behind the transferred policy — but every round of it ran at 16 sims |
| Value-only retraining on p29 | plateaus at 43.3% after ~300 rounds, also at 16 sims |
| 8 sims for throughput | collapses the policy AND runs slower (degraded play grinds games out) |
| Rollout leaf instead of the value net | −15.0 ± 7.1; the value head is much better than rollouts |
| Raising `redeterminize_m` to 8 | 1.8x faster, −6.7 points |
| Bigger nets before the loop works | pointless while search is not a teacher |
