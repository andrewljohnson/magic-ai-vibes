# penta-bot: self-play value-net spike on the penta engine

A spike port of our SPZ recipe (TD(lambda) self-play outcome learning into a
small value MLP, greedy decision-time evaluation) onto
[penta](https://github.com/lacker/penta) by lacker (Apache-2.0), an
open-source deterministic engine for Eternal Central Old School 93/94 Magic.
Nothing here shares weights with our C++ bot -- different rules, simulator,
and card pool -- only the recipe.

Pinned versions: penta commit `c206ab3` (upstream `origin/main`,
"Integrate formats with bot protocol v2"), **`engine_version 0.3.0`,
`protocol_version 2`**, built with the repo's pinned Rust toolchain
(rust-toolchain.toml) from a pristine checkout. The built binding is
**vendored here as `penta-bot/penta.so`** and `extractor.import_penta()`
prefers that pinned copy; the fallback is the clone at
`~/proj/penta/bindings/penta-py` (override with `PENTA_PY_DIR`). Both
versions are also recorded in every saved net's metadata
(`meta_engine_version` / `meta_protocol_version`, printed by gate.py).

Protocol v2 adaptation notes (from protocol 0):

- Concede left `legalActions` in protocol 1, so all Concede-skip logic is
  gone; a single legal action is now the forced-move test directly, and
  candidates are `obs["legalActions"]` as-is.
- The v2 catalog lists every format's cards (244) with per-format
  legality; the extractor keeps feature slots only for cards legal in the
  catalog's own format (Old School 93/94: 128 definitions), so the
  feature layout stays 675 = 5x128 + 35. Protocol 0/1 catalogs have no
  legality flags and keep every card, as before.
- Observation compatibility fields (`seat`, `life`, `graveyards`,
  `battlefield[].definition/controller/tapped/power/toughness/attacking`,
  `legalActions[].index`) are unchanged in v2; new fields (`format`,
  `activeTurn`, `lastSeenHand`, `presentedPartId`, object IDs) are
  ignored by the extractor.
- Afterstate copies use the binding's `clone_game()` when present
  (upstream clone-api work, unmerged at pin time); on this pin the
  deterministic replay-reconstruction fallback is active, verified exact
  by checks.py D1 in both external and opponent modes.

## Throughput measurements (M1-class laptop, single thread unless noted)

| what | speed |
| --- | --- |
| pure Rust `penta-match`, handcrafted vs random, 200 games | 0.70 s wall (~285 games/s) |
| Python loop vs `handcrafted` opponent, random policy our side | 21.0 games/s (297 our-decisions/game) |
| external mode, random policy both seats, JSON parse every decision | 42.3 games/s, 383 decisions/game, ~16,200 decisions/s |
| `examples/python/first_bot.py` (their heuristic bot, 200 games) | 15.3 games/s; beats random 100/100, handcrafted 73/100 |
| self-play with afterstate search, untrained 64-hidden net | 0.72 games/s/process (~5.8/s across 8 workers) |

The Python decision loop is JSON-parse bound (~60 us/decision), not
engine bound.

## Rollout feasibility verdict

**No clone is exposed.** The Rust engine's `Game` struct does derive
`Clone` (`src/game.rs:453`), but the `BotGame` protocol wrapper does not
(it holds a `Box<dyn Policy>`), and neither the Python binding
(`bindings/penta-py`) nor the C FFI header
(`bindings/penta-ffi/include/penta.h`) exposes any clone/snapshot/fork.

**Replay reconstruction is cheap enough.** penta is deterministic, so
`Game(same decks, same seed)` + re-acting the recorded action indices
reproduces the live state exactly (this also works against the built-in
seeded opponents, replaying only our own indices). Measured cost per
reconstruction, external mode, Sligh vs The Deck:

| history depth | ms/reconstruction |
| --- | --- |
| 10 | 0.048 |
| 30 | 0.079 |
| 60 | 0.187 |
| 120 | 0.62 |
| 253 (full random game) | 2.47 |

Reconstruct-to-30 plus one `observe()` + JSON parse is 0.11 ms. So 1-ply
afterstate evaluation over k candidates costs ~k * 0.1 ms at normal depths:
**viable**, and that is what this spike ships. Cost is quadratic in game
length (each decision replays the whole prefix), which only hurts in the
degenerate 500+-decision games an untrained net produces; the trainer
bounds it with a 600-decision cap (scored as a draw) and a depth-shrinking
candidate budget. Full-width rollout SEARCH (hundreds of playouts per
decision, as in our C++ bot) wants a real clone: at depth ~100 a
reconstruction is ~0.5 ms, so 100 playouts * their prefixes would dominate.
The right fix is a one-line upstream `fn clone(&self)` on `BotGame` exposed
through the bindings; with that, rollout search ports directly.

## Files

- `extractor.py` -- observation JSON -> 675-dim float32 vector: 128
  card-definition counts (from `penta.catalog()`, fetched once) for each of
  own hand / own battlefield / opponent battlefield / own graveyard /
  opponent graveyard (5 x 128 = 640), plus 35 global scalars (turn, pregame
  flag, active-seat flag, 11-step one-hot, both lifes, hand sizes, library
  sizes, mana pool totals, stack size, and per-side creature count / total
  power / total toughness / untapped creatures / untapped lands / pending
  attackers). Own-library-remaining counts are skipped: the protocol does
  not expose the built-in decklists, so there is nothing to subtract from.
- `net.py` -- numpy mirror of our C++ `SpzNet`: one tanh hidden layer,
  sigmoid output, binary cross-entropy, minibatch SGD with momentum 0.9,
  uniform(+-1/sqrt(fan_in)) init, save/load as `.npz`. No torch.
- `trainer.py` -- **epsilon-greedy over 1-ply afterstates** (copies via the
  binding's `clone_game()` when present, else deterministic replay
  reconstruction), targets computed backward over each seat's recorded
  afterstate values with the terminal 0/1 (0.5 draw/cap) outcome, replay
  ring buffer, multiprocessing across seeds (8 workers, one `Game` per
  task). Forced single-action decisions are played without evaluation or
  recording. **Targets default to pure undiscounted outcomes
  (`--td-lambda 1.0`)** -- see the collapse post-mortem below.
- `gate.py` -- greedy (epsilon 0) evaluation vs `handcrafted` and `random`,
  alternating seats, rotating deck pairs, fixed seeds, Wilson 95% LCB.
- `checks.py` -- 27 standalone audit checks against the live engine: seat/
  perspective labels (recorded rows must be the learner's own view with its
  own outcome, in mirror, frozen-league, and handcrafted-league games), TD
  target recursion, candidate-sampling uniformity, afterstate/fork
  exactness, replay-ring arithmetic, league seat scheduling, capped-game
  exclusion. Run `python3 checks.py` after any trainer change.

## Collapse post-mortem (2026-08 audit)

Two league-guarded curve runs collapsed (handcrafted gate 16.5% -> 5%,
then 17.5% -> 5.8%). A full audit (checks.py) cleared every labeling
hypothesis -- no seat inversion, no TD off-by-one, no sampling prefix
bias, no wrong-perspective afterstates, no ring bug. Two compounding
data defects were found and fixed instead:

1. **Outcome-starved TD targets.** Per-decision TD lambda 0.9 on penta
   trajectories of 40-130 recorded decisions per seat leaves 55-78% of
   targets with under 5% outcome weight, so the net mostly regressed
   toward its own previous values; BCE fell while strength collapsed.
   The C++ SPZ recipe this port mirrors defaults to lambda 1.0 (its
   champion line trains on hard undiscounted outcomes), and its shorter
   effective trajectories are what made 0.9 viable there. Fix:
   `--td-lambda` defaults to 1.0 (pure outcome targets). Alone this was
   NOT sufficient: a lambda-1.0 curve still slid 14.2% -> 16.7% -> 8.3%
   -> 5.8% with random degrading to 71.7%.
2. **0.5-flooding from capped passing loops.** Games that hit the
   600-decision cap were scored 0.5 for BOTH seats' full trajectories. A
   losing seat prefers 0.5 to 0, so policies learned to stall to the cap
   (30-49 of 64 games per round; average length toward 550), and capped
   games -- the longest, hence the most samples -- flooded ~70% of the
   replay with 0.5 targets, flattening the value net. Both collapsed
   recipes shared this. Fix: capped games contribute no samples (true
   engine draws still score 0.5).

Secondary fixes from the same audit: frozen-league games record both
seats (true outcome-labeled data, learner-net bootstraps); league
learner seats de-aliased from the deck-pair rotation (both had even
periods, so each ordered pair always seated the learner on the same
side); upstream engine faults mid-game (0.3.0's handcrafted policy can
return no action; reproduced at seed 1003095, Counterburn vs The Deck)
drop that game instead of crashing the run, and gate.py scores such
stuck games 0.5.

## Training config (smoke run, v2 stack)

3000 self-play games, decks Sligh / White Weenie / The Deck / Counterburn
(all ordered non-mirror pairs, rotated), hidden 64, lr 0.01, batch 256,
momentum 0.9, td-lambda 1.0 (hard outcome targets), epsilon 0.25 -> 0.03
linear, replay capacity 150k, max 16 afterstates evaluated per decision
(8/4 past depth 200/400), 8 worker processes, capped games dropped.
Weights: `penta_net.npz` (engine/protocol version in the metadata).

## Gate results (fresh v2 smoke net, 120 games each, fixed seeds)

Greedy afterstate policy (epsilon 0), engine 0.3.0 / protocol 2, seeds
5000000+, deck pairs rotating over the four training decks:

| opponent | result | win rate | Wilson 95% LCB |
| --- | --- | --- | --- |
| `random` | 92 W / 0 D / 28 L | **76.7%** | 68.3% |
| `handcrafted` | 19 W / 0 D / 101 L | **15.8%** | 10.4% |

For scale, the protocol-0 smoke with 0.5-flooded targets gated 7.1% vs
handcrafted on the same seeds -- dropping capped games doubled the smoke
strength on its own.

## Validation curve (2026-08-07, all fixes, engine 0.3.0 / protocol 2)

League mechanics per curve.sh: 15% of games vs the built-in handcrafted
bot, ~25% vs the previous chunk's frozen snapshot, epsilon 0.10 -> 0.08,
2000 games per chunk, 60-game random + 120-game handcrafted gates on
fixed seeds between chunks, promotion of `penta_net.npz` only on a new
best handcrafted gate.

| cumulative games | vs random | vs handcrafted |
| --- | --- | --- |
| 3000 (smoke baseline) | 83.3% | 15.8% (promoted) |
| 5000 (chunk 1) | 80.0% | 9.2% |
| 7000 (chunk 2) | 79.2% | 10.0% |
| 9000 (chunk 3) | 81.7% | 8.8% |
| 11000 (chunk 4) | **93.3%** | **17.5%** (promoted, new best) |
| 13000 (chunk 5) | 90.0% | 15.0% |

Reading: no collapse. Every earlier curve slid monotonically to ~5-6% by
9000-11000 games with the random gate degrading in step (16.5 -> 5.0,
17.5 -> 5.8, and 14.2 -> 5.8 at lambda 1.0 alone). With capped games
excluded the line dips while the warm-started league chunks re-fill the
empty replay ring at the lower league epsilon (9.2 / 10.0 / 8.8, within
the 120-game gates' noise), then RECOVERS ABOVE the smoke baseline --
17.5% at 11000 games with random at its run-best 93.3% -- and holds
(15.0% at 13000). The gate-guarded deliverable `penta_net.npz` is the
ckpt-11000 net (17.5%, LCB 11.7%). The transient dip is worth chasing
next: persist the replay ring across chunk restarts, or ramp per-round
training steps with ring fill.

## Second collapse + forensic probe (2026-08-07, post-audit)

The audit's fixes validated (previous section), but the CONTINUATION run
collapsed again: 15.0 -> 13.3 -> 10.0 -> 3.3 vs handcrafted over chunks
at 3000-9000 games, vs-random sagging 83 -> 74. The one issue the audit
had flagged but not fixed was the driver: **every warm-started chunk
begins with an EMPTY replay ring** (trainer state was not persisted), so
each chunk's first rounds take ~27 full-LR momentum-SGD steps on a tiny
fresh buffer (~4k samples), repeatedly shocking the net. Each restart is
a coin flip (the validation run's dip-and-recover vs this run's slide).

`probe.py` (new) diffs two nets behaviorally and calibration-wise: 100
fixed-seed games vs handcrafted each, action-type distribution at
contested decisions, feature-level value transforms on 300 mid-game
observations, and a 10-bin calibration curve. Healthy (`penta_net.npz`,
the 17.5% ckpt-11000) vs collapsed (this run's ckpt-9000, 3.3%):

| metric | healthy | collapsed |
| --- | --- | --- |
| attacks declared /game | 1.39 | **0.00** |
| attack-opportunity take-rate | 100% | **0%** |
| lands played /game | 3.00 | 1.71 |
| spells cast /game | 4.21 | 3.01 |
| final life (own / opp) | -0.2 / 14.1 | -1.0 / 17.0 |
| mean value @ game start | 0.34 | 0.43 |
| mean value in eventually-LOST states | 0.18 | 0.25 |
| calibration bins 0.4-0.7 -> realized win | .23/.32/.41 (rising) | .18/.17/.16 (flat/inverted) |
| discard-a-card value delta | -0.025 | -0.029 (intact) |
| hand-size sweep slope | +0.101 | +0.087 (intact) |

The story: the collapse is NOT a feature-level sign flip -- the collapsed
net still knows cards are good (discard hurts, more hand is better) --
it is a mis-ranking of afterstates. The collapsed net never declares an
attacker (it always picks FinishDeclaringAttackers when attacking is on
offer), under-deploys lands/spells, and its value surface is optimistic
about passive losing states (start-of-game value drifts to 0.43, lost
states to 0.25, calibration flat-to-inverted through the 0.4-0.7 range).
Against handcrafted the entire win rate comes from racing; a net that
never attacks decays to ~3% (occasional opponent deck-outs/burn).

Fixes shipped:

1. **Replay-ring + momentum persistence.** `trainer.py --ring PATH`
   loads the ring at start (when the file exists) and saves it (live
   samples, insertion order, plus SGD momentum velocities) at exit;
   curve.sh threads `penta_ring.npz` through every chunk (fresh runs
   delete it first; `KEEP_RING=1` continues one). Chunk 2+ now trains
   against a full-size replay from update 0.
2. **Learning-rate warmup.** `--lr-warmup N` (curve.sh: 200) ramps the
   LR linearly over the first N SGD updates of a chunk, softening the
   residual warm-start shock (chunk 1 of a fresh run still starts on a
   small buffer).
3. curve.sh accepts `START_NET` / `START_TOTAL` to launch a curve from
   any checkpoint with non-overlapping seed ranges.

Side finding: the collapsed run's baseline step had silently overwritten
the 17.5% ckpt-11000 deliverable with the 15.0% ckpt-13000 (curve.sh
re-points `penta_net.npz` at its own starting baseline); the validation
run below re-promoted the deliverable to a 17.5% net, so the artifact is
whole again, but note that a fresh curve run demotes a better previous
deliverable by design.

Validation (4 chunks from the healthy 15.0% deliverable, ring persisted,
lr-warmup 200, fresh seeds from 1015000; chunk 1 of a fresh run
necessarily still starts on an empty ring):

| cumulative games | vs random | vs handcrafted | ring at start |
| --- | --- | --- | --- |
| 15000 (healthy start, re-gate) | 90.0% | 15.0% | -- |
| 17000 | 78.3% | 5.8% | empty (run start) |
| 19000 | 81.7% | 7.5% | resumed 110k + momentum |
| 21000 | 85.0% | **17.5%** (promoted) | resumed 150k + momentum |
| 23000 | 80.0% | 5.0% | resumed 150k + momentum |

**Verdict: the ring fix is real but NOT sufficient -- still no stable
curve.** The empty-ring chunk shocked the net as predicted (15.0 ->
5.8), the ring-resumed chunks recovered to tie the all-time best (7.5 ->
17.5, promoted), and then a fully ring-resumed, full-buffer,
momentum-carried chunk fell straight back to 5.0. Chunk-to-chunk swings
of +-12 points survive every fix. Across all five curve configurations
tried (lambda 0.9; lambda 1.0 alone; capped-exclusion + league;
validation rerun of same; + ring/warmup), continued 1-ply training past
the 3000-game smoke point has never produced a sustained gain -- the
gate-guarded promotion is what preserves the wins individual chunks
stumble into (17.5% twice). The passivity equilibrium the probe exposed
(stalling policies -> capped games dropped -> replay dominated by
passive handcrafted-league losses -> attack values erode) appears
intrinsic to 1-ply afterstate self-play at this scale. The structural
fix is the one already on the roadmap: rollout search once the upstream
clone API merges (the lever that pushed the C++ SPZ bot past
Handcrafted), not more 1-ply chunks.

## Honest assessment: what a full port needs

This spike proves the plumbing: observation -> features -> value net ->
afterstate-greedy actions, trained end to end from self-play outcomes on
penta's native API. To reach our C++ bot's level it still needs:

1. **A real clone upstream** (or FFI-level state snapshot), then port the
   rollout-lookahead layer -- rollout search was the key lever that pushed
   our SPZ bot past Handcrafted, and replay reconstruction is too slow for
   hundreds of playouts per decision.
2. **Embedding features** (per our task #45 design) instead of raw
   definition counts, so the net generalises across the 100+ card pool and
   future cards instead of memorising 128 columns.
3. **HC-league sparring**: gate-and-freeze checkpoints, train against a
   league of frozen opponents plus their handcrafted bot, not pure
   mirror self-play.
4. **Speed**: the Python loop caps at ~16k decisions/s on JSON parsing
   alone. Training at millions of games wants the C FFI
   (`penta_legal_action_count` etc.) from C++/Rust, reusing our existing
   SPZ machinery, with the Python stack kept for prototyping.
5. **Combat-decision shaping**: attack/block branching is where uniform
   candidate sampling loses information; ordered enumeration or a policy
   head would help.

## Attribution

penta is by [lacker](https://github.com/lacker/penta), Apache-2.0. This
directory contains only our own bot code; nothing from penta's source tree
is vendored here.
