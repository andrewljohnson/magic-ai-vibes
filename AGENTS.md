# Working on this

No handcrafted play knowledge in training. The bot learns from self-play
only. Rule-level *evaluation* helpers (e.g. flagging a misplay) are fine;
teaching the policy from a scripted bot is not. One named deviation exists
(below) and it is the only one.

Nearly every wrong conclusion in this project's history came from one of
three things: an unpaired comparison inside its own noise, a wall-clock
guard that silently decided the sample, or a fallback that looked like a
decision. The method section exists so the next agent does not add to that
list.

## Method: how a measurement earns the right to be quoted

**1. Say what the number is a number of.** Every rate carries: games
asked, games finished, how the unfinished ones were scored, sims, noise,
opponent model, mask on/off, which net(s). A rate without its drop count is
not reportable. If more than 5% of games were dropped, the guard decided
the sample — raise it and re-run before quoting.

**2. Pair everything you can.** The handcrafted gate is exactly
deterministic (fixed seeds, no root noise), so two nets on it are a paired
comparison: compute the per-game difference from the `per` vector
`az_gate` returns and quote the SE of *that*, not `sqrt(p(1-p)/n)`. Mirror
matches are paired by seed and seat swap the same way. An unpaired ±4.5 on
a paired measurement overstates the noise and hides real effects — and,
worse, lets two flat lines get read as a "disagreement".

**3. Two numbers inside their error bars are not a finding, and two flat
series are not a trend.** Before writing "X rose while Y fell", check each
series moved outside its own bar. 46→49 on ±3.2 did not; 45.0→39.2 on ±4.5
did not.

**4. Prove the change ran before believing it did nothing.** Count
something the change must increment (`spz_core.search_stats()`,
`expand_calls`, `iters_started`) and check the count moved. A null result
against a no-op is the most expensive kind of null.

**5. Run the same input twice before telling a story about noise.** If a
result is deterministic, re-running it reproduces it game for game; a
"lucky reading" cannot happen. If it is not deterministic, say what the
seed was.

**6. Ask what a measurement cannot see.** A masked-vs-masked mirror cancels
every cost the mask imposes symmetrically (auto-payer choices, for one);
only a third-party gate can see them. A mirror against the moving best
cannot see cycling; only a fixed anchor can. Before trusting a number, name
the failure it is blind to and check that with a second, differently-blind
number.

**7. Isolate one change per measurement.** Turn-out and the mask were
measured separately and that is the only reason we know which one moved
combat and which one moved burn. Two changes in one arm produce a number
nobody can act on.

**8. Prefer an error to a fallback.** `Option` collapsed to action 0 cost
a week; `except Exception` cost four days. Anything that can fail must
count its failures and print the first one.

## Compute discipline

Generation is the product; evaluation is overhead. Check the ratio: in
`az_pool` a 25-round evaluation (gate + pool) cost about as long as the 25
rounds it scored. Rules:

* Evaluate no more often than the loop can plausibly move — every 50
  rounds unless a run is being debugged.
* A guard that binds is not a guard, it is a scoring rule. `AZ_GAME_SECS`,
  `AZ_GATE_SECS`, `AZ_H2H_SECS` must sit far above the longest honest game
  at the current sims, and must be re-checked every time sims change.
* Count discarded decisions per round (the `drop=` field). Above ~30% the
  run is mostly generating waste; fix the cause (clock, pool fraction)
  before running longer.
* Measure a lever (sims, noise, mask) with a one-off h2h before spending a
  training run on it. A training run is the most expensive experiment we
  have and the slowest to read.

## Writing the brief (FOR_FABLE.md and any hand-off)

* Numbers first, each with its drop count and pairing. Then the claim.
  Then what you want ruled on.
* State which conclusions from the previous brief you are withdrawing, in
  their own section. Retractions are findings.
* Do not ask "which measurement is right"; ask "what is each one blind
  to". Bring the second measurement if you can.
* Keep the ruled-out table in `RESULTS.md` current. Every negative result
  goes there with its number, so nobody re-runs it.

## Traps we fell into — all of these cost hours

**A guard that binds becomes part of the result.** A per-game wall-clock
budget has now silently decided SEVEN separate measurements: it truncated
28 of 120 gate games and opened a 12-point band between "capped as loss"
and "capped as draw"; it taught both seats they had lost when it was our
clock that ran out; it made a rollout diagnostic read 0.0%; it dropped 69
of 320 pool-gate games — the longest ones, where mana and attrition matter
most. Set guards far above the workload, and re-check them whenever the
search budget changes.

**A fallback you cannot distinguish from a decision hides total failure.**
`search_obs` returned `Option` and callers collapsed `None` to "play action
0". The hosted bot played the first legal action at every decision and
scored 2.1% where the same net gated 42.5%. It looked like bad play. Prefer
an error.

*This one recurred four days later, in the same file.* A bare
`except Exception` in `AzSearchPolicy.choose` swallowed three different
fingerprint rejections, and the hosted bot played 1-ply for four days. The
only symptom was weak play; the tell was `median 0ms` per move in the
transcripts. **A fallback must announce itself.** Search failures now
print, and the counters (`spz_core.search_stats()`) are the way to prove a
search actually ran: if `iters_started` did not move, it did not.

**A gate that compares a net with itself reads 50% forever.** `az_h2h`
took one policy for both seats and varied only the iteration count, so the
mirror gate played current-vs-current and best-vs-best and averaged them.
It reported ~50% for 182 rounds — including 50.0% twice to the decimal —
while training was destroying the net (27.3% vs its warm start once the
gate was fixed). A gate must be able to fail: before trusting it, feed it
two nets known to differ and check it says so.

**Measure the teacher that actually generates the targets.** Search was
+5.8 over the policy noise-free, and 44.2% — an anti-teacher — with the
25% Dirichlet self-play actually used. add-k=1 then made the target
flatter than the prior it came from. Every "search works" number had been
taken in a configuration self-play never ran. When you measure an
operator, measure it with every knob set as generation sets it.

**A chain of local wins is not progress.** Four promotions read 56, 55, 59
and 58 against their immediate predecessors; the final net scored 45.9%
against the warm start the chain began from. Promotion against a moving
best cannot see cycling. The anchor stays in the pool forever.

**A symmetric cost is invisible in a mirror.** The mana mask measured
+7.2 masked-vs-masked and −5.8 on the third-party gate. Anything the mask
costs *both* seats (the engine's auto-payer choosing which land to tap)
cancels in the mirror and shows only against an opponent that does not pay
it. One-sided measurements need a third party.

**Measure on the code path that ships.** A value-head diagnostic that
featurised without deck context reported 92.6% of decisions "blind"; on the
real native path it was 56.5%. The conclusion survived, the number was five
times too big.

**A conclusion measured through a broken component describes the breakage.**
"More search doesn't help" was measured with a saturated value head, where
extra simulations could only re-confirm the prior. With the sigmoid fixed,
each 4x of search bought ~5 points.

**Run the same input twice before believing a story about noise.** We
assumed three times that a promoted "best" net was a lucky reading and
built recalibration logic on it. The gate is deterministic: re-gating
reproduced 54.3% game for game.

**Two numbers a standard error apart are not a finding.** A whole diagnosis
("more search is worse on p29") rested on 18.3% versus 23.3% on 120 games.
"+15 to +22 from search on p22" compared 1-sim play of one net with 32-sim
play of another; paired, it was +11.7.

**A detector that fires on correct play is worse than none.** We flagged
"land played after combat" at 1.80/game as the systematic defect. Holding a
land until postcombat is better or neutral. Removed rather than kept with a
caveat.

**Check the baseline is a real measurement.** A "31.6% built-in bot"
baseline sat in five files for months. It was the lower bound of a
confidence interval from an abandoned C++ project. Parity is 50%.

**A test built on stale inputs tests nothing.** Replaying four-day-old
transcripts through the current build "passed" 12/12 — because those were
protocol-22 observations and the failure was elsewhere. Check that a test
can fail before trusting that it passed.

**`pkill -f <script>` matches the shell running the command.** Use
`pgrep -f '[h]osted_bot.py'` or a pidfile.

**A flag that is set is not a flag that is running.** Play-the-turn-out was
added, the env var was set, the strings were in the binary, and a 200-game
paired mirror came back 48.4%. The descent has TWO expansion sites and only
one had been patched. An `expand_calls` counter showed 0 and settled it in
a single run.

**Grep for every site, not the first one.** `git grep` for the surrounding
pattern (here `first_visit`) rather than trusting the first match.

**Restoring the old vendor contaminates the tree.** Checking out the p22
`vendor/penta` for a baseline left the working tree inconsistent once. If a
p22 number is needed, do it in a separate worktree, never in the main one.

## The one named deviation

No handcrafted play knowledge in training — except this, deliberately:

**`--mask-mana` removes `ActivateManaAbility` from the search's options.**
It encodes the strategic prior "never float mana". It is here because the
engine auto-pays costs (75% of CastSpell offers appear with an empty pool,
so nothing becomes uncastable), because mana burn makes floating close to
always wrong in this format, and because it measured **+7.2 ± 3.8** in a
paired mirror while also cutting branching 29%.

**It is NOT yet an established gain.** The same net gated with the mask
scores 39.2% against the built-in bot where the unmasked net scores 45.0%.
The mirror cannot see a cost both seats pay; the gate can. What settles it:
(a) a `playout_log.py` audit of the masked gate games for auto-payer
damage — City of Brass self-damage, Mishra's Factory tapped for mana with
a land available, casts failing for colour; (b) an own-model gate
(`AZ_GATE_OPP=greedy`), masked vs unmasked, 300 games, paired SE. Ship only
if (a) is clean and (b) is not negative, and turn it on in the hosted bot
at the same moment. Any number produced with the mask on is not comparable
to one produced with it off; the flag is a single env switch
(`AZ_MASK_MANA`) precisely so a run cannot half-apply it.

**Roll it back if** the format changes to one where floating mana matters
(no mana burn, storage lands, mana abilities with side effects), or if the
value head ever becomes sharp enough to learn the rule itself.

## Invariants

* `aac_lockstep.py` holds native rows bit-identical to the Python path.
  Any change to the native runner must keep it passing.
* Feature slots come from the sorted set of LEGAL card definitions.
  Changing legality changes the layout and invalidates every checkpoint.
* `action_dim` is 184. Restrict the action set by MASKING inside the
  availability filter, never by re-encoding, so checkpoints stay valid.
* Generation, both gates, and the hosted bot must run the same search
  configuration (sims aside). A knob applied in one and not another makes
  every number in the run incomparable; put such knobs behind one env
  switch.
* Vendor patches in `vendor/penta` are marked `SPZ VENDOR PATCH`. The
  fingerprint hashes every simulation source file **and the dependency
  closure**, so ANY patch changes it — additive accessors included — and it
  also pins an exact upstream revision. Consequences: an engine built
  elsewhere refuses observations from this one, and no build of ours ever
  matches the public server. Hosted play therefore requires
  `SPZ_ACCEPT_FINGERPRINT`; the three LOCAL-ONLY patches implementing it
  must never be proposed upstream.
* Keep the rebuild integrity check armed. The fingerprint-acceptance patch
  normalises that string only and still compares every other byte of game
  state, so genuine rules drift between us and the server is still caught.
