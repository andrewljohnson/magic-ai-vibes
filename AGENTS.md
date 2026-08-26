# Working on this

No handcrafted play knowledge in training. The bot learns from self-play
only. Rule-level *evaluation* helpers (e.g. flagging a misplay) are fine;
teaching the policy from a scripted bot is not.

## Traps we fell into — all of these cost hours

**A guard that binds becomes part of the result.** A per-game wall-clock
budget silently decided four separate measurements: it truncated 28 of 120
gate games and opened a 12-point band between "capped as loss" and "capped
as draw"; it taught both seats they had lost when it was our clock that ran
out; it made a rollout diagnostic read 0.0%. Set guards far above the
workload, and re-check them whenever the search budget changes.

**A fallback you cannot distinguish from a decision hides total failure.**
`search_obs` returned `Option` and callers collapsed `None` to "play action
0". The hosted bot played the first legal action at every decision and
scored 2.1% where the same net gated 42.5%. It looked like bad play. Prefer
an error.

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
reproduced 54.3% game for game. The real phenomenon was simpler — training
from a good net reliably degrades it.

**Two numbers a standard error apart are not a finding.** A whole diagnosis
("more search is worse on p29") rested on 18.3% versus 23.3% on 120 games.

**A detector that fires on correct play is worse than none.** We flagged
"land played after combat" at 1.80/game as the systematic defect. Holding a
land until postcombat is better or neutral — it reveals less and costs
nothing. Removed rather than kept with a caveat.

**Check the baseline is a real measurement.** A "31.6% built-in bot"
baseline sat in five files for months. It was the lower bound of a
confidence interval from an abandoned C++ project. Parity is 50%. Every
chart drawn against it flattered its run by ~18 points.

## Invariants

* `aac_lockstep.py` holds native rows bit-identical to the Python path.
  Any change to the native runner must keep it passing.
* Feature slots come from the sorted set of LEGAL card definitions.
  Changing legality changes the layout and invalidates every checkpoint.
* Vendor patches in `vendor/penta` are marked `SPZ VENDOR PATCH`. They
  change penta's `simulationFingerprint`, so an engine built elsewhere will
  refuse observations from this one.
