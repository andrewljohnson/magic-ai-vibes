# Roadmap

## The problem, in one paragraph

We have a 61% bot on protocol 22 that cannot be deployed, and a 43% bot on
protocol 29 that can. Training on p29 has not improved past 43.3% because
**search there has no measurable value over its own prior**
(+0.8 points, inside a ±4.5 gate), and an AlphaZero
loop climbs only at the rate its search beats its policy. On p22 that
number was +15 to +22.

## The open problem

Self-play generates its training targets with the GREEDY in-tree opponent
model, and with that model search is **worse than no search** (−2.5 ± 3.8
at 32 sims). With the handcrafted model the same search is **+8.3 ± 4.2**.
So the loop distils a teacher weaker than its student, every round, which is
why training degrades the net on every configuration tried.

The gate uses handcrafted. Self-play cannot: the handcrafted bot is scripted
play knowledge, and the pure-build rule forbids training against it.

So the question is how to get a self-play in-tree opponent that is actually
good, without importing handcrafted knowledge. Candidates, none tested:

* Sample the opponent's move from the policy instead of taking its greedy
  argmax — cheap, and greedy-argmax self-play is known to be brittle.
* Give the opponent a small search of its own (expensive; the descent
  already dominates cost).
* Use the previous best checkpoint as the opponent model rather than the
  live policy.

## Then

1. **First-play urgency.** Unvisited actions take Q=0, the worst score on a
   [0,1] scale, so a confident prior never explores at a small budget. This
   is the likeliest reason the threshold sits between 16 and 32 sims.
   FPU = parent mean Q.
2. **Why p29 costs ~10x p22 per game**, and reaches an opponent decision on
   63% of iterations against 80%. Unexplained.
3. **Representation.** Bag-of-cards features cannot express a pairwise
   creature matchup; the bot attacks into strictly better blockers 0.86
   times a game. Deep-sets pooling over permanents. Invalidates every
   checkpoint.

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
