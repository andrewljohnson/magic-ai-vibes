# Roadmap

## Where it stands (2026-08-28)

The loop is finally measured honestly, and it climbs slowly. Search beats
the policy in self-play's own configuration (+5.8 noise-free at 32 sims,
~+4 at the 0.05 root noise generation uses). Promotion is against a fixed
anchor plus recent champions, so cycling is visible. On that measure the
`az_pool` run moved 46% → 49% against its anchor over 25 rounds — the first
absolute progress this project has produced, and slow.

Two facts bound the rate:

* **The teacher is barely better than the student.** An AlphaZero loop
  climbs at the rate search beats its prior; +4 per round of targets is a
  low ceiling. The lever is sims, not noise (noise ≥0.10 already eats the
  margin).
* **~80% of generated decisions are discarded** — half to the 50% pooled
  opponent (only the learner's records are kept), the rest to clock
  truncation (`fin` 60–83%). Evaluation costs as long as generation.

## Next, in order

1. **Fix the guards, then re-baseline.** `AZ_H2H_SECS` / `AZ_GAME_SECS`
   high enough that drops are under 5%. Re-run the anchor mirror on the
   current best so there is one clean reference.
2. **Settle the mask.** Auto-payer audit via `playout_log.py` on masked
   gate games (City of Brass self-damage, Factory tapped for mana with a
   land available, colour-failed casts); own-model gate
   (`AZ_GATE_OPP=greedy`) masked vs unmasked, 300 games, paired SE. Ship
   or revert on that, and mirror the decision in the hosted bot.
3. **Measure the teacher at 64 sims.** One noise-free h2h, 64 vs 1 sim.
   p22 went 55 → 61 from 32 → 128 sims. If 64 is ≈+10, generate at 64:
   per-round gain roughly doubles at 2x generation cost, and fixed costs
   amortise.
4. **Restart `az_pool`** with: no clock truncation, `--pool-opponent-frac
   0.25`, gate every 50 rounds (handcrafted gate only on promotions), sims
   from (3), and search-argmax ≠ prior-argmax rate logged per round (above
   ~90% agreement the search is no longer teaching; that, not `pol_ent`,
   is the collapse metric).
5. Run 50 clean rounds and read the slope against the anchor before
   changing anything structural.

## After that, by expected leverage

1. **Representation.** Bag-of-cards features cannot express a pairwise
   creature matchup; the bot attacks into strictly better blockers. Deep-
   sets pooling over permanents (permutation-invariant, keeps per-instance
   state). Invalidates every checkpoint — a phase change, only worth it
   once the loop has a measured slope to compare against.
2. **Value-head resolution.** One life point is under the outcome-label
   noise. More un-truncated data helps first; auxiliary targets (life
   delta at end of turn) are a candidate but are a handcrafted signal and
   need the same scrutiny as the mask.
3. **Branching the opponent** (real two-player ISMCTS). Not needed while
   the fixed-model search beats the prior; becomes relevant if pooled
   opponents fail to stop cycling.

## Numbers to report

Three, always together, each with its drop count:

* **anchor mirror** — progress; the one the loop optimises.
* **own-model gate** (`AZ_GATE_OPP=greedy`) — portable strength against a
  fixed external opponent; best predictor of server play.
* **handcrafted-model gate** — exploit value; kept because it is the only
  deterministic paired external reference.

## Cheaper things worth doing

* The `apply_enumerated` PR is pushed to `andrewljohnson/penta` branch
  `apply-enumerated` and still needs opening. ~30% of search time.
* Paired SE everywhere the gate is quoted (`per` vector → per-game
  difference).

## Tried and rejected — do not redo

| idea | outcome |
|---|---|
| Play the turn out on expansion | 50.3% ± 4.0 at 3x search cost; moved combat errors 9→5, mana burn 16→20 |
| First-play urgency | +0.0 ± 3.7 at 16 sims; −4.2 ± 3.6 at c_puct 2.5 |
| Sampled in-tree opponent instead of greedy argmax | no measurable change at the gate |
| Root noise 0.25 / add-k 1 | teacher scores 44.2% vs its own prior; target flatter than prior. Use ≤0.05 / 0 |
| Promoting against the previous best only | four promotions, 45.9% vs the anchor — cycling |
| Truncation-as-loss as the cause of the decline | clock timeouts were already dropped |
| Cold start on p29 | 23.3% after ~500 rounds at 16 sims |
| Value-only retraining on p29 | plateau at 43.3%, at 16 sims |
| 8 sims for throughput | collapses the policy AND runs slower |
| Rollout leaf instead of the value net | −15.0 ± 7.1 |
| `redeterminize_m` 8 | 1.8x faster, −6.7 points |
| Determinization quality as the p29 cause | 0 reconstruction errors in 400k; oracle hidden state changes nothing |
| Restoring the p22 vendor for baselines | contaminated the tree; use a separate worktree if ever needed |
| Bigger nets before the loop has a slope | pointless |
