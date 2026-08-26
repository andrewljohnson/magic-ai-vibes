# Roadmap

## The problem, in one paragraph

We have a 61% bot on protocol 22 that cannot be deployed, and a 43% bot on
protocol 29 that can. Training on p29 has not improved past 43.3% because
**search there has no measurable value over its own prior**
(+0.8 points, inside a ±4.5 gate), and an AlphaZero
loop climbs only at the rate its search beats its policy. On p22 that
number was +15 to +22.

## Do this first

**Train protocol 29 at 32+ sims.** Paired measurement shows search is worth
+11.7 points at 32 sims on protocol 22 and only +3.3 (noise) at 16 — and
every protocol-29 training run has used 16. The loop cannot ratchet on a
teacher that is not better than its student, so it may simply have been
starved. One training run answers it.

## Then

1. **Find out why search is worth +11.7 on p22 and ~0 on p29.** Everything
   else is downstream. The value head is not obviously the culprit —
   after retraining, its sibling discrimination on p29 (0.029 prob spread,
   26% blind) is BETTER than the p22 net's (0.022, 29% blind), and that net
   gets +11.7 from search. **Determinization is ruled out**: it never fails
   on p29 (0 errors in 105k samples) and handing search the TRUE hidden
   state (`AZ_ORACLE_WORLD=1`) changes nothing. Counters also show no apply
   failures and no opponent-model failures. What differs: p29 reaches an
   opponent decision on 56% of iterations against p22's 72%, and costs ~10x
   the wall clock per game.
2. **Fix the representation.** Features are bag-of-cards counts per zone.
   They cannot express a pairwise creature matchup, and the bot attacks
   into strictly better blockers 0.86 times a game. Deep-sets pooling over
   permanents (permutation-invariant, cheap, keeps per-instance state:
   tapped, damage, counters) is the recommended first step; a small
   transformer only if combat still stalls. This invalidates every
   checkpoint, so it is a phase change, not a tweak.
3. **Deploy something.** Blocked on one concrete bug: `engine-p29/penta.so`
   is built from pristine upstream while `spz-core` embeds our patched
   `vendor/penta`, so their `simulationFingerprint`s differ and
   reconstruction refuses the observation. Build the engine from
   `vendor/penta` and the hosted path should work. Real opponents give a
   signal the built-in bot cannot.

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
| Cold start on p29 | 23.3% after ~500 rounds, 20 points behind the transferred policy |
| Value-only retraining on p29 | plateaus at 43.3% after ~300 rounds |
| 8 sims for throughput | collapses the policy AND runs slower (degraded play grinds games out) |
| 32 sims | no better than 16, and doubles game length |
| Raising `redeterminize_m` to 8 | 1.8x faster, −6.7 points |
| Bigger nets before the loop works | pointless while search is not a teacher |
