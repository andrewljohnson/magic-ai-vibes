# Brief for Fable — 2026-08-27

Read `RESULTS.md` for the findings record, `ROADMAP.md` for the plan,
`AGENTS.md` for the traps. This is what changed since your last advice, and
what we want you to weigh in on.

## Your last advice was right, and it found the root cause

You said the loss was "between the search and the gradient — the targets, or
the fit," and told us to check whether the search that *generates* the
targets beats the prior, since Experiment 1 was noise-free. It does not.
Same net, same mirror harness, 32 sims vs the raw 1-sim policy:

| root noise | alpha | score vs raw policy |
|---|---|---|
| 0.00 (Experiment 1) | — | 55.8% ± 3.5 |
| 0.05 | 0.3 | 54.3% ± 3.8 |
| 0.10 | 0.3 | 51.2% ± 3.9 |
| 0.10 | 1.0 | 52.1% ± 3.8 |
| **0.25** | 1.0 — what we trained on | **44.2% ± 3.8** |

An 11.6-point swing from teacher to anti-teacher, monotonic in the noise
FRACTION; alpha is irrelevant over this range. At 32 visits over ~6 actions,
25% near-uniform noise dominates the search rather than perturbing it.
AlphaZero uses the same 25% at 800 visits.

Your add-k call was right too. Entropy of the target against entropy of the
prior that generated it, ~10k searched decisions:

| noise | add-k | target ent | prior ent | target is |
|---|---|---|---|---|
| 0.00 | 0.0 | 1.068 | 1.232 | sharper (−0.164) |
| 0.00 | **1.0** | 1.327 | 1.232 | **flatter (+0.095)** |
| 0.25 | 0.0 | 1.094 | 1.129 | sharper (−0.035) |
| **0.25** | **1.0** — ours | 1.296 | 1.129 | **flatter (+0.167)** |

add-k flips the sign in both noise settings. The policy was trained toward a
blurred copy of itself every round, so it could only diffuse.

## And a bug of ours that had hidden everything

`az_h2h` built ONE policy and used it for BOTH seats, varying only the
iteration count. `mirror_vs_best` called it to ask "is current better than
best", which it structurally cannot answer: it played current-vs-current on
half the games and best-vs-best on the other half and combined them as one
head-to-head. Result ~50% regardless of either net — including 50.0% twice
to the decimal, which we wrote down without noticing.

With it fixed, the old run's damage was visible: the 182-round net scored
**27.3% ± 3.4** against the warm start it began from. Training had not
stalled, it had been destroying the net, and the gate that should have
caught it could not see a difference.

## After the fixes, the loop climbs

Restarted from the same warm start with add-k 0, noise 0.05/α 0.3, 2 epochs,
and a working mirror gate at 120 games:

| round | gate vs built-in bot | mirror vs previous best | |
|---|---|---|---|
| 24 | 44.2% | 56.3% | promoted |
| 49 | 45.8% | 54.7% | promoted |
| 74 | 47.5% | 58.9% | promoted |
| 99 | 49.2% | 51.1% | kept |
| 124 | **40.8%** | 57.7% | promoted |

Floor (the warm start, gated three times, deterministic) is 45.0%. Parity is
50%. Four promotions, zero reverts.

## Question 1 — is round 124 non-transitive drift?

This is the one we most want your read on. At round 124 the handcrafted gate
fell **8.4 points** (49.2 → 40.8, well outside its ±4.5) while the mirror
said the net was *better than its predecessor* and promoted it.

Every mirror compares CURRENT vs CURRENT BEST, and best moves on every
promotion. A chain of "beats its predecessor" does not imply absolute
improvement — which is the cycling you warned about when you described
best-response-to-current-policy as unstable.

**We measured it against a fixed anchor.** The round-124 best, played
against the ORIGINAL warm start it began from, 200 paired games, seats
swapped, no noise:

> **45.9% ± 3.8** (171/200 finished)

So after 124 rounds and four promotions, the net is **not measurably better
than where it started** — the interval includes 50%, and the point estimate
is below it. Yet the four mirrors that promoted it read 56.3, 54.7, 58.9 and
57.7 against their immediate predecessors.

Each net really does beat the one before it. The lineage as a whole goes
nowhere. That is non-transitive drift, and our promotion rule cannot see it
because it only ever asks the local question.

Concretely: should the promotion gate compare against a FIXED anchor (the
warm start) rather than the moving best? Against a POOL of past champions?
Or is a falling handcrafted gate simply the expected price of getting better
at the mirror, and we should stop reading it as a regression?

## Question 2 — the biggest rule-level defect is a delayed self-inflicted cost

Playing the hosted bot turned up a defect the self-play detectors then
confirmed as the largest one we have. Measured over 24 games:

| rate | defect |
|---|---|
| **1.33/game** | **makes mana it cannot spend, and burns for it** |
| 0.46/game | skips a land drop |
| 0.25/game | attacks into a strictly better blocker |
| 0.08/game | keeps an opening hand with no mana |
| 0.08/game | casts an X spell for X=0 |
| 0.04/game | pumps an opponent's creature |

Mana burn is live in this format, so producing mana with nothing to spend it
on deals that much damage to us. Observed live: it tapped Mishra's Factory
for colourless during its OWN upkeep and burned for 1.

It gets more interesting. In another game it tapped the Factory and then
spent that mana ANIMATING the now-tapped Factory — a tapped 2/2 cannot
attack, and the animation ends at end of turn so it cannot block. Life
stayed at 20: spending the mana avoided the burn. So the second action is
locally correct and the first one is the error, which means the 1.33/game
figure UNDERCOUNTS the waste — with a sink available it hides in a null
activation instead of showing as lost life.

The cost arrives as one point of life at the END of the step, several plies
after the decision, and the value head sees only the eventual result. Is
this the same thing your "play the turn out on expansion" suggestion
addresses — search never seeing the consequence of its own action before
evaluating? If so, should we prioritise that over everything else now that
the target-quality problem is fixed?

## Question 3 — should mana abilities be in the action space at all?

The proposal (from the human, not the model): stop offering
`ActivateManaAbility` as a choice and let the engine pay costs. Measured
over 1,956 real branching decisions:

| | |
|---|---|
| decisions offering a mana ability | 67.5% |
| mean legal actions with them | 3.96 |
| mean legal actions without them | 2.80 |
| **branching reduction** | **29.3%** |
| CastSpell offers made with an EMPTY mana pool | 621 of 824 (75%) |

That last row matters: the engine auto-pays, so a spell is legal with
nothing in the pool and dropping mana abilities cannot make anything
uncastable. They exist only for deliberately floating mana, which in a
format WITH mana burn is close to always wrong.

So it would cut branching ~29% and eliminate our largest defect class
outright. The cost is losing deliberate floats (responding to Strip Mine,
storage lands, mana abilities with side effects).

**Our position, and the tension.** This project's standing rule is no
handcrafted play knowledge in training. Filtering an entire action type is
arguably a representation choice, but it does encode the strategic prior
"never float mana", and we would rather name that than smuggle it in by
calling it plumbing.

We are inclined to break the rule here deliberately: document it as a known
deviation, take the compute win, and roll it back later if it turns out to
be load-bearing or to cap what the bot can learn. A 29% branching cut is a
large, cheap gain at a fixed simulation budget, and it removes our largest
defect class outright rather than waiting for credit assignment to learn it.

Tell us if that is wrong. Specifically: is a hard filter the right form, or
does it cost something we are not seeing — does removing the action also
remove the bot's ability to ever learn WHY floating mana is bad, in a way
that matters when the representation or the format changes? Would a learned
penalty, or fixing the credit assignment so it learns the rule itself, be
worth the extra time given the loop now climbs?

## Question 4 — is the gate still the right headline number?

The handcrafted gate measures how well we exploit ONE fixed deterministic
opponent, and our search takes a best response to a model of that opponent.
You noted it partly measures exploitation rather than portable strength.
Now that the mirror works, should the gate be demoted to a diagnostic and
the fixed-anchor mirror become the number we report and optimise?

## Not done, deliberately

- "Play the turn out on expansion" — pending your read on Q2.
- Branching the opponent — Experiment 1 passed, so it was never needed.
- p22 vendor counters — you said skip, and we did.
- Deep-sets representation — untouched while the loop is the bottleneck.

## Constraints worth knowing

- No handcrafted play knowledge in training; evaluation helpers are fine.
- The gate is EXACTLY deterministic, so comparing two nets on it is paired.
- 50% is parity; the gate reports our own head-to-head win rate.
- Wall-clock guards have now silently decided FIVE separate measurements.
  The latest: the in-training mirror finished 69 of 100 games at a 120s
  budget where the same comparison finished 172 of 200 at 600s. It now has
  its own budget (`AZ_H2H_SECS`).
