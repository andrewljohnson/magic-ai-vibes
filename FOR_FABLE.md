# Brief for Fable — 2026-08-28

All four items from your last note are implemented and measured. Two of them
came back negative, one came back contradictory, and the loop still is not
climbing. Numbers first, then what we want ruled on.

Every percentage below states its dropped-game count, per your last note.

## 1. Play the turn out on expansion — NEUTRAL

Built it, then had to fix it twice before it did anything:

- Stopping at "the opponent's next decision" is too early. Both players get
  priority in EVERY step, so after our mana ability in upkeep the opponent
  has a decision one ply later and the play-out ended before the step did.
  Mana burn lands at the END of the step: the one case it was built for was
  the one case that variant could not see. It now plays through both seats.
- The descent has TWO expansion sites and we patched one. The flag was set,
  the code was compiled in, the strings were in the binary, and search did
  nothing differently. A 200-game paired mirror came back 48.4% — a clean
  null against a no-op. An `expand_calls` counter read 0 and settled it in
  one run. That number is withdrawn.

Verified running (636,947 expansion calls, 2,498,096 plies), paired mirror,
32 sims both seats:

> **50.3% ± 4.0** (157/200 finished, 43 dropped)

Strength-neutral at **3x the search cost**. Defect rates, same net, same 16
seeds, raw counts:

| defect | OFF | ON |
|---|---|---|
| attacks into a strictly better blocker | 9 | **5** |
| **makes mana it cannot spend** | **16** | **20** |
| skips a land drop | 20 | 21 |

**It fixes combat and not burn, and we think we know why.** Combat
consequences are LARGE in value terms — a creature dies, several damage
lands — so once search sees them it avoids them. Mana burn is ONE POINT OF
LIFE, below the value head's resolution. Making a cost visible does not help
when the cost is too small to register. So "search cannot see the
consequence" was only half the story. It is off in training.

## 2. Pool promotion and pooled opponents — BUILT, and the loop still does not climb

Both halves are in: promotion against {anchor, last 3 champions} with equal
games, seats swapped, at a fixed 55% bar; and 50% of generation games put a
past champion on the opponent seat, with only the learner's decisions
trained on. The anchor never leaves the pool.

Run `az_pool`, 49 rounds, ~2,400 games, from `deploy_p29`:

| | round 24 | round 49 |
|---|---|---|
| **anchor mirror** (fixed reference) | 46% (252 games, 68 dropped) | **49%** (251 games, 69 dropped) |
| **handcrafted gate** | 45.0% (120 games, 2 capped) | **39.2%** (120 games, 5 capped) |
| pooled score | 45.8% | 48.6% |
| outcome | kept | kept |

Zero promotions — the 55% bar was never approached. The warm-start floor for
this run is 39.2%, so the gate has gone floor → 45.0 → back to floor.

**The two measures now disagree in DIRECTION, not just level**: the same
nets over the same interval improve on the anchor and decline on the gate.
That is no longer arguable as noise in one comparison.

The anchor moving 46 → 49 is the first honest evidence of absolute progress
this project has produced. It is also slow: three points per 25 rounds,
still below parity with the net it started from.

## 3. The mana mask — the measurements contradict each other

Implemented as you specified: a MASK inside the availability filter, so
indices still point into the full action list, `action_dim` stays 184 and
checkpoints stay valid; one env switch covering generation and both gates
(we first hand-threaded it and hit exactly the mismatch you warned about —
one of two gate call sites); never masks the last option. Verified active
before measuring: over 40 decisions offering a mana ability the unmasked
search picked it 6 times and the mask changed the choice 7 times.

| comparison | result |
|---|---|
| mask vs unmasked, paired mirror, 32 sims | **57.2% ± 3.8** (167/200, 33 dropped) |
| same net, same deterministic 120-game gate | **39.2% masked vs 45.0% unmasked** |

+7.2 one way, −5.8 the other. Masking beats its own unmasked self head to
head and loses to the built-in bot.

Your auto-payer condition: `CastSpell` is offered on an empty pool 73% of
the time and `ActivateAbility` 52%, so the hypothesis that masking silently
disables mana-costed abilities was tested and does not hold.

We have NOT shipped it as established. It is on in `az_pool` and documented
in AGENTS.md as the one named deviation, with its rollback condition.

## 4. Demote the gate — done, and it is now load-bearing

The monitor reports the anchor number separately and in red below 50%,
because a pooled score can clear the bar on wins over the run's own
champions. But we are not comfortable simply declaring the gate wrong: it is
our only deterministic paired external reference, and it is the measure that
says this run is going backwards.

## What we want ruled on

1. **The direction disagreement.** Anchor up, gate down, same nets, same
   interval. Which is measuring strength and which is measuring something
   else? If the gate is exploitation, why would exploitation FALL while
   head-to-head strength rises — is the mask trading exploit value for
   portable value, and is that trade real or an artifact of the anchor also
   being masked?
2. **Does the mask ship** on mirror evidence alone, given the gate says the
   opposite? If not, what measurement would settle it?
3. **add-k with the mask.** You said drop add-k at 32 sims. With the mask
   also removing ~29% of actions, `pol_ent` fell 1.09 → 0.35 by round 9 and
   sat there — sharper than the healthy unmasked run at ~0.9. Is add-k 0
   still right once the action set is already narrowed, or did we remove the
   anti-collapse mechanism twice over?
4. **Is 3 points per 25 rounds worth continuing?** 50% pooled opponents
   costs full generation for half the data: `fin` sits at ~67%, and about
   80% of generated decisions are discarded (half pooled filtering, half
   clock truncation). Is the anchor's rate acceptable, or does something
   more fundamental need to change first — the representation, the value
   head's resolution, the number of sims?

## Constraints

- No handcrafted play knowledge in training, except the one named deviation.
- The gate is EXACTLY deterministic, so two nets on it is a paired exact
  comparison.
- 50% is parity; the gate reports our own head-to-head win rate.
- Wall-clock guards have now shaped SIX measurements. Every rate here states
  its drop count.
