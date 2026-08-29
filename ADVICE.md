# 2026-08-28 — rulings on the four questions

1. **The "direction disagreement" is two flat lines.** Anchor 46→49 on
   ±3.2 per read and gate 45.0→39.2 on ±4.5 (which returned to its own
   floor) — neither series moved outside its bar. Report the paired SE from
   the per-game `per` vector, not `sqrt(p(1-p)/n)`. The pool eval dropped
   69/320 games and the mask mirror 33/200: the guard still binds on the
   longest games. Raise `AZ_H2H_SECS` until drops <5% before any other
   measurement.
2. **Mask: not on the mirror alone.** Masked-vs-masked cancels every
   symmetric cost; the auto-payer (which land the engine taps) is the
   obvious one and only the gate can see it. Settle with a `playout_log`
   audit of masked gate games (City of Brass damage, Factory tapped with a
   land available, colour-failed casts) plus an own-model gate masked vs
   unmasked, 300 games. Ship only on that, and flip the hosted bot at the
   same moment.
3. **add-k / entropy.** `tgt_ent` 0.23 with a 2.8-action mean is sharp,
   not collapse. The collapse metric is search-argmax ≠ prior-argmax rate
   per round; above ~90% agreement the search teaches nothing. Noise is not
   the lever (0.10 already eats the teacher's margin); sims are.
4. **3 points / 25 rounds is what this teacher gives, and 80% of compute is
   discarded.** Stop the waste: `AZ_GAME_SECS` so truncation never binds,
   pool frac 0.5→0.25, gate every 50 rounds. Raise the teacher: measure
   noise-free 64-sim vs 1-sim once; if ≈+10, generate at 64.
5. Play-the-turn-out: accept the null (neutral at 3x cost). Keep off.

Order: guards → mask audit → 64-sim h2h → restart `az_pool` with the above
and argmax-agreement logged → 50 clean rounds before anything structural.

---

# 2026-08-27 — rulings on the round-124 drift, mana burn, the mask, the gate

* Round 124 is real drift plus winner's curse: promotion on a >50% read of
  120 games biases every promoted delta upward, and best-response-to-
  predecessor cycles. Promote against a pool (anchor + last 3, ≥300 games,
  >55%) and put pool opponents in generation.
* Mana burn is the delayed-consequence case: play the turn out on
  expansion, then evaluate. (Measured next day: neutral; helped combat
  only.)
* Mask mana abilities as a flag, applied identically in generation, both
  gates and the hosted bot; mask, don't re-encode; verify the auto-payer;
  measure separately from the turn-out.
* Report three numbers: anchor mirror (progress), own-model gate
  (portable), handcrafted-model gate (exploit, diagnostic).

---

# 2026-08-26 (evening) — the mirror result changes the picture

The mirror result changes the picture and the truncation hypothesis does not
fit it well.

## Read of the result

Experiment 1 says search beats the policy (55.8%). The mirror says the
policy never absorbs it — and at r19 it got *worse* (35.3%) before hovering
at 50%. Search improving play while training does not move the net means
the loss is **between the search and the gradient**: the targets, or the
fit. That is a data/signal problem, but not the one proposed.

**Truncation-as-loss is a weak candidate.** It only touches the value
labels; the mirror shows the *policy* is not improving. And `az_train.py`
already drops clock timeouts (`r == -2` -> `keep=False`); only decision-cap
games (`-1`) are scored as losses. Check the `drop` column — if fin=85-90%
is mostly clock, those games are already out. Run the drop arm in parallel
if there is idle compute, but do not make it the next serial step.

## What to measure first (one round of self-play data, no training)

1. **How much signal is in the target?** Per searched decision:
   `KL(visit target || policy prior)`, and whether search's argmax differs
   from the prior's argmax. If argmax differs on ~5% of decisions and the KL
   is small, the policy gradient is mostly noise.
2. **Does the search that generates targets beat the prior?** Experiment 1
   was noise-free. Training targets come from 32 sims with 25% Dirichlet
   (alpha=1, ~uniform over 7 actions) plus add-k=1 (7 pseudo-visits on 32 =
   ~18% more uniform). Re-run the h2h with `root_noise=0.25` on the 32-sim
   seat. If noisy search ~= prior, the targets carry nothing and the policy
   is being trained toward a flattened version of itself — which is exactly
   "gets a bit worse, then hovers at 50%". Check `pol_ent` over rounds: if
   it drifts up, that is the signature.
3. **Is the policy fitting at all?** Train on a fixed buffer for 20 epochs
   and re-gate the mirror. If overfitting to the search targets still does
   not beat the warm start, the targets are the problem, not the optimiser.
   If it does beat it, the fix is more epochs / more games per generation
   (960 games per promotion window is tiny for AZ) rather than any search
   change.

## Likely fixes, in the order the measurements will point

- Noise 0.25 -> 0.1 and alpha closer to 0.3; add-k 0 at 32 sims (it was a
  fix for 8 sims).
- Train the policy toward visits with the noise not mixed into the tree's
  prior at the root (computing visits from a second noise-free pass at the
  root is too expensive; instead just cut the noise).
- 2-4 epochs per round, promotion window >= 2000 games.

Skip #4 (p22 vendor); the p29 `our_nodes/iter` counter already answers what
matters. "Play the turn out on expansion" stays worth doing but **after**
the signal question is settled — it improves the teacher, and the current
problem is that the student is not learning from a teacher we have now
shown is good.

---

# 2026-08-26 — answers to FOR_FABLE.md, and what to do today

Read FOR_FABLE.md, the corrected RESULTS/ROADMAP, and the diff since
yesterday (`4846310..30f6dfc`). The instrumentation and the paired
measurements are exactly right. The conclusion drawn from them is one step
too far, and that step matters because it decides what you build today.

## The gap between "the greedy-model gate is −2.5" and "self-play distils a weaker teacher"

The −2.5 was measured **against the handcrafted bot**. In that game the
greedy model is simply the *wrong* model of the opponent: search plans
against an opponent that plays like our policy, and then a different
opponent replies. A best-response to the wrong opponent losing to no search
is expected and says nothing about self-play, where the real opponent *is*
our policy. The caveat you flagged is not a caveat — it is the whole
measurement. So the claim "self-play's targets come from a search that is
worse than no search" is **not yet established**. It needs the paired
comparison in self-play's own domain:

> **Experiment 1 (today, first):** mirror match, alternating seats, same
> seeds, both seats our own net, opponent model = greedy. Seat A searches at
> 32 sims, seat B plays the raw policy at 1 sim (argmax, no noise). Paired
> score of A vs 50%. 200+ games. Then the same with A at 16 sims.

There is no tool for this: `head_to_head.py` plays the old AAC `.npz`
actors, and `az_stream_episodes` runs the same config on both seats. Add
one PyO3 entry (`az_h2h(iters_p1, iters_p2, opponent_model, specs)`) that
is `az::play_episode` with a per-seat `MctsConfig` and no noise — ~40 lines.

- If 32-sim-greedy **beats** the 1-sim policy in the mirror: the operator
  works in-domain, and the degradation has a different cause (see "other
  suspects"). Don't rebuild the opponent model yet.
- If it **loses or ties**: the operator is broken in its own domain and
  question 1 is the job. Go to Experiment 2.

Either way this is the number FOR_FABLE.md should have had, and it is one
gate's worth of compute.

## Question 2 — is this architecture only exploiting a known opponent?

Partly yes, and the part that is "yes" is structural, not a tuning problem.

The search is single-observer with a **non-branching** opponent: every
opponent decision is answered by a fixed model, never explored. That makes
the search a *best response to the model*. When the model is the exact
bot you are gated against (handcrafted, deterministic, and it sees the
same determinized hand), you get the answer key for its replies — the
+8.3/+11.7 and the p22 61% include that. Against a server opponent nobody
has modelled, the honest expectation is closer to the 40–42% raw-policy
band than to 50.8%. Report both numbers going forward: "handcrafted-model
gate" (exploit) and "own-model gate" (portable).

But "cannot self-improve at all" is too strong. Best-response-to-current-
policy is the fictitious-play operator: it can improve, but it is
**unstable** — it chases the current policy's weaknesses, the policy
moves, and the loop cycles or drifts instead of ratcheting. Monotonic
decline on every configuration is what that looks like from the outside.
The fix is the one AlphaZero itself uses: **branch the opponent**.

> **Experiment 2 / the real fix:** make opponent decision nodes tree nodes.
> Same policy head as prior, PUCT selecting to *minimise* our value
> (`(1−Q) + explore`), backprop unchanged (single value from our
> perspective). Key opponent actions by a determinization-independent
> descriptor — action type + **card definition** (not the hidden object id,
> which is fresh per world) + target descriptor — with the availability
> counts you already carry. This is ordinary SO-ISMCTS (Cowling 2012); the
> current design branches only our nodes.

Cost: roughly what the Greedy model already pays per opponent ply (one
state encode + `encode_all` + scores), so it is not the expensive option.
It also removes handcrafted knowledge from the *gate's* search, so the
teacher and the measured search can finally be the same search.

Order of the three candidates you listed:
1. **Sample from the policy instead of argmax** — one line in the Greedy
   arm (`mcts.rs`, the `best` loop). Do it today as a cheap point on the
   curve: argmax is the most exploitable possible model (deterministic, and
   wrong wherever the policy is unsure), and sampling gives the expectation
   over the opponent's policy instead of a best response to one line. Measure
   in Experiment 1's harness AND with `AZ_GATE_OPP`.
2. **Branch the opponent** — the principled fix, half a day.
3. **Previous best checkpoint as the model** — skip. It does not change the
   operator, only which fixed policy it best-responds to.
Giving the opponent its own nested search is branching done expensively;
don't.

## Question 3 — FPU

Yes, that is very likely why the threshold sits between 16 and 32. On a
[0,1] scale Q=0 for an unvisited child is the worst score in the tree, so
with a confident prior the exploration term `1.5·P·√N/(1+n)` for a P=0.03
sibling never beats a visited child's Q≈0.45 at N≤16; the search is the
prior's argmax with a few forced re-visits. Two changes, both cheap:

- **FPU = parent mean Q minus a small reduction**, Leela-style:
  `q_init = Q_parent − 0.2·√(Σ P of visited children)`. At the root before any
  visit use the value-net estimate of the root state.
- **`c_puct`** was tuned for [−1,1] values; on [0,1] the same constant is
  half as exploratory. Try 2.5–3.0.

Measure paired at 16 sims. If 16 becomes +5, training is 2x cheaper and the
threshold moves.

## Question 4 — 63% vs 80% reaching the opponent, and the 10x

Both probably have the same cause: **more of OUR decision points per turn
on p29**. `our_nodes/iter = 4.0`. An iteration stops at the first unvisited
action, so reaching the opponent requires every one of our decisions in the
turn to have been visited already; if p29 offers more branching plies per
turn (priority passes, the protocol-28 announced payment choices, more
non-forced decisions), fewer iterations get through, and every game has more
searched decisions — which is the wall clock. Check in an hour: with the
p22 vendor restored, read `our_nodes/iter` and *searched decisions per
game* on p22 and diff against p29. `d353bb4` already found the per-episode
search cost is close (10.7 vs 9.4 s) and the gap is in whole games, which
points the same way.

Two fixes that help regardless:
- **Expand, then play out the turn.** On a first visit, instead of leaf-
  evaluating immediately, continue with the prior's argmax until the
  opponent's next decision (or the start of their turn), then evaluate. Every
  iteration then sees its combat resolve and its spells land. The old C++
  recipe did this and `trainer.py`'s docstring records why ("attack-declare
  afterstates finally show their consequence instead of only their cost").
  Bounded cost — it's a handful of cheap policy plies.
- Collapse chains of our consecutive priority-pass decisions into one node
  where nothing but Pass is sensible (dominance already does part of this;
  it is off in the gate and self-play).

## Other suspects for the monotonic decline, if Experiment 1 says the teacher is fine

- **Temperature 1 for the whole game.** `az.rs` samples from visits at
  every decision to the end. AlphaZero uses τ=1 for the opening (~30 moves)
  and argmax after. Endgames played by sampling produce noisy outcome
  labels for the value head and random-looking states for the policy.
  Temperature 1 for the first ~8 searched decisions, then argmax.
- **Dirichlet α=1 on ~7 actions** replaces a quarter of the prior with
  near-uniform noise — that is a lot. AZ's α≈10/branching ≈ 1.4 is in the
  same range, so this is probably fine, but it stacks with the above.
- **The ratchet reverts on any decline vs the handcrafted bot.** Self-play
  improvement and gate-vs-bot strength are different axes (your own
  `head_to_head.py` docstring says so). Once Experiment 1's harness exists,
  add a mirror-match gate (new vs best, 200 games) and promote on *that*,
  keep the handcrafted gate as the reported number. Otherwise a policy that
  genuinely improves at self-play but loses a little exploit-value against
  the bot is discarded every 20 rounds — which would also look like
  "monotonic decline".

## Today, in order

1. `az_h2h` entry + Experiment 1 (32-sim greedy vs 1-sim, mirror, paired).
   Decides everything below. ~2 h including the gate.
2. Sampled opponent model (one line) — re-run 1 and `AZ_GATE_OPP` paired.
3. FPU + c_puct, paired at 16 and 32 sims on the handcrafted gate.
4. p22 counters for `our_nodes/iter` and searched decisions per game.
5. If 1 said the operator fails: branch the opponent. If 1 said it works:
   temperature schedule + mirror-match promotion, then resume training at
   32 sims (or 16 if step 3 rescued it).

Don't resume training before step 1 — every run so far has been spent
without knowing whether the teacher beats the student in the only domain
where it plays.

---

# 2026-08-25 — original recommendation

Read the whole search path (`spz-core/src/mcts.rs`, `az.rs`, `mcts_runner.rs`,
`det_runner.rs`, `pybridge.rs`, `az_train.py`), the vendor patches, and the
commit history. This is what I'd do next, in order.

## The framing is right, the suspect list is wrong-shaped

Your own data already argues **against** the value head being the bottleneck:
after retraining, the p29 value head separates siblings *better* than the p22
one (0.029/26% vs 0.022/29%), and search still adds nothing. A leaf evaluator
that ranks siblings fine but a search that gains nothing from it means the
**tree between root and leaf** is broken, not the leaf. That tree is:
determinize → descend our nodes → in-tree opponent model → apply → leaf.
Every step of that has a silent failure mode, and none of them is counted.

Two facts from git that constrain the story:

1. **The loss is at the port, not the speed patches.** `57012e7` (before
   `f900af0` / `b9d2a7e` rewrote the ply loop) already measured deploy_v1 on
   p29 at 32 sims = 42.0% vs 54.3% on p22. So enumerate-once /
   `apply_enumerated` / node cache are not the cause. (Re-measuring p22 with
   *today's* search code is still worth one gate — see step 0.)
2. `apply_enumerated` is sound: protocol expansion only rewrites
   `ChooseDecision`, which still goes through full `is_legal_action`. I
   checked; don't re-derive it.

## Silent failure modes in `mcts.rs` — instrument these FIRST

None of these is logged. Any one of them, if it fires on p29 and not p22,
produces exactly "search = prior":

| where | what happens on failure | line |
|---|---|---|
| `determinize()` | `from_observation_json(...).ok()` — reconstruction error is discarded, the **iteration is skipped**. 32 sims with 50% rejects is 16 sims; 90% rejects is 3. | `mcts.rs:710` |
| `run()` loop | `if let Some(world) = determinize(..)` — silently does fewer iterations than `cfg.iters` | `mcts.rs:817-823` |
| opponent ply | `opp.choose_action` returns None or `apply_enumerated` fails → **descent stops and leaf-evals the pre-opponent state**. Search then never sees the opponent's turn: no blocks, no burn, no combat consequence. That is where +15 came from on p22. | `mcts.rs:508-514` |
| our ply | `let _ = world.apply_enumerated(...)` — error dropped, world does not advance, the node is expanded and the **unchanged state is scored as the afterstate** | `mcts.rs:576, 613` |
| forced ply / avail==1 | same `let _ =` | `mcts.rs:463, 521` |

Add per-search counters (thread-local, like the `prof` module already there):
`det_ok`, `det_err{by error string}`, `opp_apply_fail`, `our_apply_fail`,
`max_depth_hits`, `terminal_leaves`, `plies_per_iter`, `our_nodes_per_iter`,
and **fraction of iterations that reach an opponent decision at all**. Print
them per gate. This is ~1 hour and turns the rest of this list from guesses
into a lookup.

The reconstruction's own rejection reasons are in
`vendor/penta/src/game/state_checkpoint.rs:631-984` and
`bot_game.rs:100-125` ("rebuilt a different legal-action list", "rebuilt a
different public observation field: X"). Log the string — the field name
tells you what p29 added that our hidden-state sampler doesn't satisfy.

## Three experiments that split the hypotheses (each is one 120–300 game gate)

Run them on deploy_p29 (the 43.3% net), gate at 1 sim vs 16 sims, same
seeds. The gate is deterministic, so compute the **paired** difference from
the per-game `per` vector `az_gate` already returns — the SE of a paired
difference on correlated games is much smaller than the ±4.5 you've been
quoting, and it is the number that answers "is search worth anything".

**A. Oracle determinization.** In `Ismcts::search` you hold `real: &BotGame`.
Add a debug env (`AZ_ORACLE_WORLD=1`) that makes `determinize()` return
`real.core_game().clone()` instead of sampling. This is the upper bound on
determinization quality — the true hidden state.
- If oracle search is worth +10 or more → determinization / reconstruction is
  the problem (your leading candidate), go to C.
- If oracle search is still ~0 → determinization is NOT the problem; it's the
  opponent model or the tree (B), or the counters above already told you.

**B. Rollout leaf, with the guard lifted.** `AZ_GATE_PLAYOUT=1` was tried
and returned 0W/60L because the wall-clock guard capped every game
(`5c1cae6`). Re-run with `AZ_GATE_SECS=100000`, 60 games, 8 sims. It isolates
the value head: if a rollout leaf makes search worth something, the value
head is it; if not, it isn't. This diagnostic was designed for exactly this
question and has never actually produced a number.

**C. True-deck determinization.** `az_gate(..., classify=False)` is already a
knob (`mcts_runner.rs:431`). It hands the sampler the opponent's real
decklist instead of the classification (which is 0% accurate on turn one and
falls back to *our own deck* via `unwrap_or(my_deck)`, `mcts_runner.rs:433`).
It's a cheaper, weaker version of A; run it if A says determinization.

Also note what the sampler ignores (`det_runner.rs:28-43`): `last_seen_hand`
(p29 reveals opponent-hand cards after some effects) and cards on the stack.
If A wins and C doesn't, the gap is these plus library composition.

## Step 0: confirm the p22 baseline on today's code

README says the p22 numbers cannot be re-measured. They can:
`git checkout 62948bf~1 -- penta-bot/vendor/penta && cargo build --release`.
Do it once, with the counters from above compiled in, and gate deploy_v1 at
1/32 sims. You get two things: (a) proof that today's search code still
produces +15 on p22 (closing the speed-patch bisect for good), and (b) the
**baseline counter distribution** — plies/iter, fraction of iterations
reaching the opponent's turn, det_err rate — to diff against p29. The diff
in those counters is very likely the answer by itself.

## Things that are wrong regardless of what the counters say

- **Self-play and the gate run different searches.** `az_stream_episodes`
  uses `opponent: Greedy` + `use_dominance: false` (`pybridge.rs:1130-1138`);
  `az_gate` uses `opponent: Handcrafted` (`pybridge.rs:591`). The teacher the
  loop distills is not the search the gate measures. Add `AZ_GATE_OPP=greedy`
  to `az_gate` and measure both; if search is worth +X with one model and 0
  with the other on p29, that's your answer too (the handcrafted bot changed
  substantially across the 173 vendor commits — X-spell scoring, Disk sweep
  scoring, "as many options as beneficial").
- **Q=0 for unvisited actions on a [0,1] value scale** (`mcts.rs:667`). It's
  the worst possible score, so with a trained (peaked) prior, an action with
  P=0.02 needs `1.5·0.02·√N` to beat a visited sibling's Q≈0.45 — it never
  will at 16 sims. That makes 16-sim search a 1-ply argmax whenever the prior
  is confident. Use first-play-urgency = parent's mean Q (or the value of the
  node itself). This is not the p22/p29 differentiator but it caps how much
  search can ever be worth at your budget. Log root visit entropy in the
  *gate* (no noise there), not just in self-play.
- **Make failures errors, not fallbacks.** You already learned this with
  `search_obs` (action 0 at 2.1%). `let _ = apply_enumerated` and
  `determinize().ok()` are the same shape. At minimum count them; better,
  panic in debug builds.

## What NOT to do until the above is done

- Don't touch the representation (deep-sets) yet. It invalidates every
  checkpoint and it won't fix a tree that never reaches the opponent's turn.
- Don't run more value-only retraining. 300 rounds plateaued at 43.3% and
  the sibling-spread numbers say the value head is already adequate.
- Don't quote a search-value number from fewer than 300 paired games.

## Order of operations, concretely

1. Counters in `mcts.rs` (1h). Gate deploy_p29 at 16 sims on p29, read them.
2. Oracle-world gate (A). 1 line in `determinize`, one gate.
3. If A ≫ 0: diff `det_err` strings and `seen_defs` coverage vs p29's
   observation; fix the sampler; re-gate.
   If A ≈ 0: rollout-leaf gate (B) and `AZ_GATE_OPP=greedy`; whichever moves
   the number is the component to fix.
4. Restore the p22 vendor once, with counters, for the baseline diff.
5. Only then resume training — with the gate and self-play running the same
   search config, and FPU fixed.
