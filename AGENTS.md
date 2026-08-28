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

*This one recurred four days later, in the same file.* A bare
`except Exception` in `AzSearchPolicy.choose` swallowed three different
fingerprint rejections, and the hosted bot played 1-ply for four days. The
only symptom was weak play; the tell was `median 0ms` per move in the
transcripts. **A fallback must announce itself.** Search failures now
print, and the counters (`spz_core.search_stats()`) are the way to prove a
search actually ran: if `iters_started` did not move, it did not.

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

**A test built on stale inputs tests nothing.** Replaying four-day-old
transcripts through the current build "passed" 12/12 — because those were
protocol-22 observations and the failure was elsewhere. Two of that day's
path checks also used the wrong path prefix and silently matched nothing,
which read as "no drift". Check that a test can fail before trusting that
it passed.

**`pkill -f <script>` matches the shell running the command.** Killing
daemons by pattern killed the calling shell mid-command, twice, which read
as a mystery exit code. Use `pgrep -f '[h]osted_bot.py'` or a pidfile.

**A flag that is set is not a flag that is running.** Play-the-turn-out was
added to the search, the env var was set, the strings were in the compiled
binary, and a 200-game paired mirror came back 48.4% -- a clean null result.
The descent has TWO expansion sites and only one had been patched, so the
search did nothing differently. An `expand_calls` counter showed 0 and
settled it in a single run. Before believing that a change did not help,
prove the change RAN: count something it must increment, and check the
count moved.

**Grep for every site, not the first one.** The same edit was needed at two
places 40 lines apart. `git grep` for the surrounding pattern (here
`first_visit`) rather than trusting the first match a search returns.

## The one named deviation

No handcrafted play knowledge in training — except this, deliberately:

**`--mask-mana` removes `ActivateManaAbility` from the search's options.**
It encodes the strategic prior "never float mana". It is here because the
engine auto-pays costs (75% of CastSpell offers appear with an empty pool,
so nothing becomes uncastable), because mana burn makes floating close to
always wrong in this format, and because it measured **+7.2 ± 3.8** in a
paired mirror while also cutting branching 29%.

It was taken only after the principled fix failed: play-the-turn-out was
built so search could SEE the burn land, and it changed nothing, because the
cost is one point of life and the value head cannot resolve that.

**Roll it back if** the format changes to one where floating mana matters
(no mana burn, storage lands that carry value, mana abilities with side
effects), or if the value head ever becomes sharp enough to learn the rule
itself. Any number produced with the mask on is not comparable to one
produced with it off; the flag is a single env switch precisely so a run
cannot half-apply it.

## Invariants

* `aac_lockstep.py` holds native rows bit-identical to the Python path.
  Any change to the native runner must keep it passing.
* Feature slots come from the sorted set of LEGAL card definitions.
  Changing legality changes the layout and invalidates every checkpoint.
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
