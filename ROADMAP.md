# Roadmap

## Where it stands

The target-quality problem is FIXED and the loop now climbs. Self-play was
generating its targets with a 25% Dirichlet search that scores 44.2% against
the raw policy it teaches, and add-k=1 made the target flatter than the
prior, so the policy was trained toward a blurred copy of itself. With noise
at 0.05 (teacher 54.3%) and add-k 0, the run has taken four promotions and
zero reverts, and the gate moved 44.2 -> 49.2% against a 45.0% floor.

Two things now matter more than anything else:

1. **Round 124 diverged**: the handcrafted gate fell 8.4 points while the
   mirror promoted. The mirror compares against the MOVING best, so a chain
   of "beats its predecessor" does not prove absolute improvement. A
   fixed-anchor measurement against the original warm start is the test.
2. **The largest rule-level defect is a delayed self-inflicted cost**:
   making mana it cannot spend, 1.33/game, and that undercounts it because
   the waste sometimes hides in a null activation instead of a burn.

## The open problem, and how to split it

Three measurements, each on one round of self-play data with no training.
They decide everything below, and none of them needs a training run.

1. **How much signal is in the target?** Per searched decision, compute
   `KL(visit target || policy prior)` and how often search's argmax differs
   from the prior's argmax. If argmax differs on only ~5% of decisions and
   the KL is small, the policy gradient is mostly noise.

2. **Does the search that generates the targets beat the prior?**
   Experiment 1 was noise-free, but training targets come from 32 sims with
   25% Dirichlet noise (α=1, near-uniform over ~7 actions) *plus* add-k=1
   (7 pseudo-visits on 32 visits — about 18% more uniform). Re-run the
   mirror h2h with `root_noise=0.25` on the 32-sim seat. If noisy search
   ≈ prior, the targets carry nothing and the policy is being trained
   toward a flattened version of itself — which is exactly "gets a bit
   worse at r19, then hovers at 50%".

   Supporting evidence already in hand: across all 180 rounds `pol_ent`
   sits at 1.38–1.45 against `tgt_ent` ~0.92. The policy stays flatter than
   its targets and never converges.

3. **Is the policy fitting at all?** Train on a fixed buffer for 20 epochs
   and re-gate the mirror. If overfitting to the search targets still does
   not beat the warm start, the targets are the problem and not the
   optimiser. If it does beat it, the fix is more epochs and more games per
   generation — 960 games per promotion window is tiny for AlphaZero — and
   not any search change.

## The fixes those measurements will point at

* Noise 0.25 → 0.1, α closer to 0.3; add-k 0 at 32 sims (add-k was a fix
  for 8-sim search and was never revisited).
* Keep the root noise out of the prior the policy is trained toward. A
  second noise-free pass at the root is too expensive, so cut the noise.
* 2–4 epochs per round, promotion window ≥ 2000 games.

## After the signal question is settled

1. **Expand, then play the turn out.** On a first visit, continue with the
   prior's argmax to the opponent's next decision before leaf-evaluating,
   so every iteration sees its combat resolve. This improves the *teacher*,
   and the current problem is the student — so it is not next.
2. **Representation.** Bag-of-cards features cannot express a pairwise
   creature matchup; the bot attacks into strictly better blockers 0.86
   times a game. Deep-sets pooling over permanents. Invalidates every
   checkpoint.
3. **Why p29 costs ~10x p22 per game** and reaches an opponent decision on
   63% of iterations against 80%. Do **not** restore the p22 vendor for
   this — the p29 `our_nodes/iter` counter already answers what matters,
   and restoring p22 contaminated the tree once already.

## Cheaper things worth doing

* Gate on more games. 120 games is ±4.5; most of our arm comparisons were
  inside the noise.
* Cap the wall-clock guards far above the workload, or remove them. They
  have silently decided four separate measurements.
* The `apply_enumerated` PR is pushed to `andrewljohnson/penta` branch
  `apply-enumerated` and still needs opening. ~30% of search time.

## Tried and rejected — do not redo

| idea | outcome |
|---|---|
| Promoting on the mirror instead of the handcrafted gate | 0 promotions in 180 rounds — the ratchet was not the problem |
| First-play urgency | +0.0 ± 3.7 at 16 sims; −4.2 ± 3.6 at c_puct 2.5 |
| Sampled in-tree opponent instead of greedy argmax | no measurable change at the gate |
| Truncation-as-loss as the cause of the decline | clock timeouts were already dropped; only decision-cap games score as losses |
| Cold start on p29 | 23.3% after ~500 rounds, 20 points behind the transferred policy — but every round ran at 16 sims |
| Value-only retraining on p29 | plateaus at 43.3% after ~300 rounds, also at 16 sims |
| 8 sims for throughput | collapses the policy AND runs slower (degraded play grinds games out) |
| Rollout leaf instead of the value net | −15.0 ± 7.1; the value head is much better than rollouts |
| Raising `redeterminize_m` to 8 | 1.8x faster, −6.7 points |
| Bigger nets before the loop works | pointless while the policy does not absorb search |
