# penta-bot: self-play value-net spike on the penta engine

A spike port of our SPZ recipe (TD(lambda) self-play outcome learning into a
small value MLP, greedy decision-time evaluation) onto
[penta](https://github.com/lacker/penta) by lacker (Apache-2.0), an
open-source deterministic engine for Eternal Central Old School 93/94 Magic.
Nothing here shares weights with our C++ bot -- different rules, simulator,
and card pool -- only the recipe.

Pinned versions: penta commit `feb3355` (upstream `origin/main`,
"Add an enters-the-battlefield hook, and Augur of Bolas on it"),
**`engine_version 0.5.0`, `protocol_version 2`**, built with the repo's
pinned Rust toolchain (rust-toolchain.toml, 1.97.1) from a scratch
worktree of `origin/main`. This pin includes the merged clone API
(upstream PR #5, merge `6809d9a`): `game.clone()` forks a live game --
built-in opponent state included -- into an independent copy, measured at
~4 us per clone at depth 120 (vs 0.62 ms for the old
replay-reconstruction), verified byte-identical and independent by
checks.py D1 and the repin smoke. The previous pin (`c206ab3`, engine
0.3.0) is kept as `penta.so.engine-0.3.0.bak`. The built binding is
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
- Afterstate copies use the binding's `clone()` (engine >= 0.5; the
  pre-merge spelling `clone_game` is still accepted, and the
  deterministic replay-reconstruction fallback remains for older
  bindings) -- verified exact by checks.py D1 in both external and
  opponent modes.
- The 0.5.0 catalog lists 244 definitions across formats; 128 are legal
  in Old School 93/94, so the 675-feature layout is unchanged and 0.3.0
  nets remain shape-compatible.

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

**Update (engine 0.5.0 repin): the clone merged (upstream PR #5), and the
rollout search is ported.** `game.clone()` costs ~4 us at depth 120, so
the 1-ply policy jumped from 0.72 to ~11.7 games/s/process on its own,
and the search layer below became affordable.

## Rollout search (the C++ recipe, ported)

Rollout lookahead was the lever that pushed our C++ SPZ bot past
Handcrafted (+12-15 points, cured passivity/drift); penta's 1-ply
collapse post-mortems (below) show why this engine needs it even more:
penta's decision granularity means an attack-declare afterstate shows
the tapped attacker (the cost) but never the blocks and damage (the
consequence), so a 1-ply net can literally never see why attacking pays.

Per greedy decision (`trainer.choose`, `--search-topk 0` disables):

1. **Myopic screen**: every legal action (up to `--max-eval`) is played
   on a clone and the afterstate is scored by the net, as before.
2. **Top-k playouts**: the best `--search-topk` (default 4) candidates
   each get `--playouts` (default 1) PLAYOUTS on fresh clones: both
   seats play greedy 1-ply with the same net (epsilon 0, up to
   `--playout-max-eval` candidates per step) until the deciding seat's
   NEXT TURN START (penta turns alternate seats, so this plays through
   blockers, damage, and the opponent's full response turn) or a
   `--playout-budget` (default 120) decision cap; the boundary
   observation is scored by the net from the deciding seat's
   perspective, terminal results score exactly. The engine and greedy
   policy are deterministic, so extra playouts (2+) add
   `--playout-epsilon` uniform noise to decorrelate.
3. Argmax over the refined top-k (myopic and playout values are not on
   one scale, so non-top-k candidates cannot win).

**HONESTY CAVEAT: clones carry the true hidden state** (both libraries
and the opponent's hand), so playout outcomes leak information the
deciding seat could not see. That is acceptable for TRAINING /
self-improvement -- both seats are us -- but evaluation vs the scripted
bots inherits the leak through the search policy, so gate numbers here
are flagged accordingly; upstream issue #11 tracks determinized clones
for honest search-time evaluation. Gates are still run and reported
(the scripted opponents cannot be exploited deliberately by a value
net, so the leak's practical edge is bounded), with this caveat.

Measured cost (M1-class laptop, one process, trained 64-hidden net,
shared with a 6-thread C++ benchmark):

| policy | games/s/process | ms/decision |
| --- | --- | --- |
| 1-ply (search off), clone forks | 11.7 | 0.15 |
| search topk4, budget 120, playout-max-eval 8 | 0.32 | 6.4 |
| search topk2, budget 120, playout-max-eval 8 | 0.49 | 4.1 |
| search topk4, budget 60 | 0.32 | 6.5 |

Budget 60 saves nothing over 120 because the turn-start boundary almost
always arrives first (~25-50 decisions). Defaults ship at topk 4 /
1 playout / budget 120 / playout-max-eval 8: ~2 games/s aggregate
across 6-8 workers, i.e. a 3000-game smoke in well under an hour even
on a shared machine.

## Aggression prior (exploration)

Epsilon-exploration no longer draws uniformly: a `--prior-frac`
(default 0.5) slice of exploration draws follows a first_bot-shaped
ordering -- play a land, else cast the biggest castable thing, else
declare an attacker, else uniform -- with the rest kept uniform so every
action retains support (checks C3/H4). This is a credited adaptation of
the action ordering in penta's `examples/python/first_bot.py` (lacker,
Apache-2.0; their 40-line heuristic beats their handcrafted bot 73%).
Uniform exploration was the second half of the passivity trap: aggression
never entered the replay data, so the value net had no attack
consequences to learn from even when they would have scored well.

## Lever package (2026-08-09, the v3 era)

Four levers plus a scouted-blunder fix, each validated before the v3
full-package run (results at the bottom of this file / progress.txt):

1. **Feature schema v2 (825 inputs, versioned).** Saved nets carry their
   input size; `Extractor.for_inputs()` dispatches old 675-input nets
   onto the v1 layout, so both eras stay loadable. v2 appends: exact
   CASTABLE-NOW counts per definition for our hand (from the enumerated
   `CastSpell` legalActions -- free and exact, zero when this seat holds
   no decision), hand + castable-now counts per converted-cost bucket
   (0..5, 6+), and 8 race-math scalars (castable count, max castable
   cost, life delta, flying power both sides from catalog keyword text,
   turns-to-kill both directions at current power, attackers available).
2. **Search config sweep** (120-game fixed-seed handcrafted gates on the
   37.5% deliverable): topk4/1-playout 37.5% @ 3.56 games/s, topk8/1
   40.0% @ 1.96, topk4/2 37.5% @ 2.20, topk8/2 35.0% @ 1.50. The +2.5pt
   for topk8 is inside 120-game noise at 1.8x cost: **topk 4 / 1 playout
   stays the train+gate config**.
3. **Per-turn TD(lambda)** (`--td-lambda-per-turn 0.9`): lambda decay at
   TURN boundaries (turn index recorded per decision; decisions within a
   turn share one decay step). A 65-decision/10-turn trajectory keeps
   ~39% outcome weight in its deepest target, vs ~0.1% at the
   per-decision 0.9 that collapsed (post-mortem below).
4. **League richness:** DECKS widened to 8 built-ins -- Sligh, White
   Weenie, The Deck, Counterburn + Goblins (aggro), Erhnamgeddon
   (midrange), Mono Black (disruption), Jeskai Aggro (tempo). 56 ordered
   pairs; `PENTA_DECKS` env override reproduces old 4-deck gates.
   Random-pilot smoke over the new decks: 120 games, 0 engine faults.

## Dominance prune (no upside versus pass)

The scouting corpus (scout.py) caught owner-blind blunders the 1-ply
screen + short playouts misprice inside noise: Ancestral Recall cast AT
the opponent (seed 9101102 t13), Fireball aimed at our own Goblin
Balloon Brigade (9100103 t20), Strip Mine cracking our own Mountain
(9100200 t3). All are strictly dominated by passing, so the C++
engine's dominance prune is ported into `choose()` (trainer, gate, and
scout all inherit it; exploration draws keep full support).

Before the value screen, each CastSpell/ActivateAbility candidate is
played on a clone and SETTLED (all-pass stack resolution, bounded); it
is pruned when versus the status-quo baseline the opponent is nowhere
worse (hand count, life, battlefield multiset keyed by definition AND
current stats, graveyard/exile) and we are nowhere better with at least
one strict loss (hand, life, library, battlefield). Every ambiguity
fails CLOSED: non-empty starting stack, settling that branches /
terminates / leaves the turn-step context, stat-changed permanents
(combat tricks are incomparable, not worse), mana-pool gains (rituals),
and cards whose rules text signals snapshot-invisible effects (extra
turns, until-end-of-turn grants, prevention, regeneration; text-derived
from the catalog, 39 definitions, future cards covered automatically).
The only non-pass action is never pruned. checks.py section I replays
all three scouted decisions (blunder pruned, legitimate targeted plays
in the same state survive) and audits the invariants over live games.

**Land-drop rule** (added mid-continuation at 80k games; the mirror
direction, our C++ engine's "spend=-1 / land-drop-never-worse"): the
8-deck scout sweep on the 48k net showed The Deck at 7/128 wins,
skipping 2.9 land drops per losing game and dying at median turn 17
holding the City of Brass / Tundra it never played -- pass looks
value-equal to a free drop, so frugality starves every big spell. Now,
in our own main phase with an empty stack, PASS ITSELF is pruned when a
legal PlayLand settles to pure development (battlefield strictly grows,
the hand spend is exactly the one land, our life/library and the entire
opponent side untouched). The net still chooses WHICH land; any side
effect (an Ankh-style life hit, a lost permanent) makes the drop
incomparable and Pass survives -- no caps, no card lists, fail closed
(checks I5/I6).

Fix-1 verdict (128-game re-gate of The Deck pairings, scout seed block
9200000, ckpt-v3-48000, land rule on): land skips per losing game 2.9
-> 0.2 -- the mechanism works and the rule stays -- but wins 4/128 vs
the 7/128 baseline and spells/game 5.4 vs 5.5, both unchanged inside
noise: mana was NOT the binding constraint. The residual pathology is
DEPLOYMENT HOARDING -- Serra Angel / Jayemdae Tome still die in hand
with the mana to cast them available. A casting-dominates-pass prune is
NOT safe (holding counter mana is legitimately correct for a control
deck), so the fix moves to the training side: deployment-aware features
(schema v3) and pilot-seat oversampling, the next run's package.

## Files

- `extractor.py` -- observation JSON -> float32 features, VERSIONED
  schema. v1 (675): 128 card-definition counts (from `penta.catalog()`,
  fetched once) for each of own hand / own battlefield / opponent
  battlefield / own graveyard / opponent graveyard (5 x 128 = 640), plus
  35 global scalars (turn, pregame flag, active-seat flag, 11-step
  one-hot, both lifes, hand sizes, library sizes, mana pool totals,
  stack size, and per-side creature count / total power / total
  toughness / untapped creatures / untapped lands / pending attackers).
  v2 (825, default): v1 plus the castability + race-math block (lever 1
  above). Own-library-remaining counts are skipped: the protocol does
  not expose the built-in decklists, so there is nothing to subtract
  from. Nets dispatch by input size via `Extractor.for_inputs()`.
- `net.py` -- numpy mirror of our C++ `SpzNet`: one tanh hidden layer,
  sigmoid output, binary cross-entropy, minibatch SGD with momentum 0.9,
  uniform(+-1/sqrt(fan_in)) init, save/load as `.npz`. No torch.
- `trainer.py` -- **epsilon-greedy ROLLOUT SEARCH over afterstates**
  (myopic 1-ply screen behind the dominance prune, then top-k playouts
  to the deciding seat's next turn start; `--search-topk 0` restores
  plain 1-ply; copies via the binding's `clone()`, else deterministic
  replay reconstruction), with a first_bot-shaped aggression prior on
  exploration draws. Targets computed backward over each seat's recorded
  afterstate values with the terminal 0/1 (0.5 draw/cap) outcome, replay
  ring buffer, multiprocessing across seeds (one `Game` per task).
  Forced single-action decisions are played without evaluation or
  recording. **Targets default to pure undiscounted outcomes
  (`--td-lambda 1.0`)** -- see the collapse post-mortem below;
  `--td-lambda-per-turn` applies decay per turn boundary instead.
- `gate.py` -- greedy (epsilon 0) evaluation vs `handcrafted` and `random`,
  alternating seats, rotating deck pairs, fixed seeds, Wilson 95% LCB.
- `scout.py` -- instrumented mistake-review games vs handcrafted (full
  transcripts, blunder counters); the corpus behind the dominance prune.
- `checks.py` -- 42 standalone audit checks against the live engine: seat/
  perspective labels (recorded rows must be the learner's own view with its
  own outcome, in mirror, frozen-league, and handcrafted-league games), TD
  target recursion (per-decision and per-turn), candidate-sampling
  uniformity, afterstate/fork exactness, replay-ring arithmetic, league
  seat scheduling, capped-game exclusion, aggression-prior ordering,
  playout boundary/determinism, search taking immediate wins, and the
  dominance-prune blunder regressions + invariants. Run
  `python3 checks.py` after any trainer change.
- `curve-v3.sh` -- the v3 full-package run harness (fresh v2-schema net,
  4000-game ring-persistent league chunks, promotion-on-beat from a 0
  bar, one-time cross-era reference gate, early stop on two consecutive
  gates below 60% of the running best).

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

## v3-era curves and the peaked continuation (2026-08-10)

The v3 full-package run (fresh 825-input net, 11 x 4000-game chunks)
climbed monotonically to the 44k deliverable: 37.5 -> 42.5 -> 45.8 ->
47.5 -> 49.2 -> 55.8% with no collapse (cross-era reference: the old
37.5% net gates 37.9% on this yardstick). The continuation from 44k
promoted once more at 48000 games -- **57.5% / 120, confirm-400 54.8%
(LCB 49.9%), a hair from certified handcrafted parity** -- then PEAKED:
five gates without a promotion (46.7 / 55.0 / 53.3 / 48.3 / 55.0 /
53.3 / 53.3 / 51.7 through 80000), stopped by hand mid-chunk-21.
Deliverable: penta_net.npz = ckpt-v3-48000. The land-drop rule shipped
at the 80000 gate boundary and was outcome-null on the curve (51.7%
before/after class); its mechanism verdict lives in the fix-1 note
above (skips 2.9 -> 0.2/loss, wins unchanged -- hoarding binds, not
mana). Next lever: the deployment package (schema v3 + seat
oversampling), A/B-gated and grown from the 48k net via --grow-init.

**Deployment-package verdict (v4 run, 2026-08-10): NULL -- features
without signal.** The A/B smokes tied (v3 features 51.7%, + seat
oversampling 50.0%; oversampling bought nothing) and the v4 full run
from the grown 48k net declined (46.7 -> 43.3 over two chunks, killed).
The behavioral re-gate of The Deck on ckpt-v4-56000: 3/128 wins,
spells/game 5.7 vs the 5.5 baseline -- after 8k games the net does not
USE the deployment features. The lesson: The Deck's seat loses its
games regardless of which action it takes, so no outcome gradient ever
favors deploying; REPRESENTATION alone cannot break a hoarding
equilibrium that the VALUE SIGNAL never punishes. The fix must inject
signal, not features: a counterfactual deploy-axis aux loss (weight
0.01 hard max -- 0.1 destroyed the C++ net), or deeper training
playouts so Tome/Serra payoffs become visible to the value target
(playout-max-eval 8 hides them; the same family of lever that cured
passivity last era). Both are A/B'd BEHAVIORALLY (The Deck 128-game
sweep: spells/game, win-condition deploys, wins), not gate-first.
Deliverable stays ckpt-v3-48000 (57.5% / confirm-400 54.8%).

**Signal-injection verdict (arms C/D, 2026-08-11): both NULL --
stopped.** Seed-locked 3000-game smokes from the 48k net, behavioral
acceptance on the The Deck 128-game sweep (deck-sweep.py, scout seed
block):

| The Deck seat | wins | spells/g | big-threat casts/g | engine casts/g |
| --- | --- | --- | --- | --- |
| baseline ckpt-v3-48000 | 4/128 | 5.4 | 0.03 | 0.10 |
| arm C: deploy-aux 0.01 (v3 grow) | 3/128 | 5.4 | 0.10 | 0.15 |
| arm D: playout-max-eval 24 | 4/128 | 5.6 | 0.05 | 0.17 |

Sanity gates 52.5% / 50.0% -- no collapse, no monkey's paw at weight
0.01. The deploy counters nudge the right way (the aux arm tripled the
big-threat cast rate off a near-zero base; both arms +50-70% engine
casts), but spells/game and wins are FLAT: The Deck still loses
124+/128 with the cards in hand. Per the acceptance rule the line is
closed: no more feature-side or smoke-scale deployment levers. What
this whole arc establishes: the hoarding equilibrium is held in place
by the VALUE function's blindness to multi-turn payoffs (a Tome
activated three times, a Serra attacking twice are 5+ turns of
consequence; playouts to the next turn start cannot see them), and the
honest fixes are structural -- much deeper playout horizons at real
compute cost, or a policy head / explicit multi-turn credit -- not
another feature block. The deliverable stands at handcrafted parity:
ckpt-v3-48000, 57.5%/120, confirm-400 54.8% (LCB 49.9%).

## Counter-window diagnosis (2026-08-11): verdict (c), held for discussion

Counterburn casts 0.0-0.1 counterspells/game (its bottom-three losing
matchups all carry dead counter halves). Hypothesis tested: the
observation omits stack contents, so countering looks like "down a card
for nothing". Findings from 32 instrumented games (ckpt-v3-48000, sweep
seeds): (1) the extractor IS stack-blind -- one scalar, stack size, in
every schema; (2) but the engine offers counter casts correctly
(enumerated when UU is payable from untapped; stack targets carry
stackId/type='spell', NOT 'instance' -- a probe/scout blind spot, now
fixed in scout.py); (3) at the 11 eligible windows the MYOPIC screen
already favors countering (0.663 vs 0.582, 7/11) despite the blindness,
and it is the PLAYOUT refinement -- the lens that actually sees the
resolution -- that prefers passing (0.463 vs 0.367, 4/11) and drives
the declines; the net countered 2/11 and mana-floated toward more. The
binding constraints are (i) OFFER SCARCITY, ~0.34 eligible windows per
game, because the pilot taps out (no learned untapped-mana discipline),
and (ii) the playout stand-in policy's handling of post-counter
positions -- the same multi-turn-credit limitation as the deployment
arc. Stack features would only sharpen a screen that is already
directionally right, so per the (c) branch nothing was implemented;
held for discussion.

## Policy-head prototype (v1 design, 2026-08-11)

Every wall this week -- deployment hoarding, the tap-out habit, counter
declines, the old passivity -- is one failure: the greedy value
stand-in cannot credit actions whose payoff spans multiple turns, and
features / curriculum / aux-loss / deeper-playouts all proved null
against it. v1 learns ACTION SELECTION directly, cheaply:

- **Data**: winner imitation. Action-level data is NOT recoverable
  from existing artifacts (rings hold only the chosen afterstate +
  outcome -- positives-only collapses into the value function;
  transcripts hold strings), so `gen_policy_archive.py` self-plays the
  best net and records, at every multi-action greedy decision, the
  post-prune candidate afterstate set + the choice + the seat's
  eventual result. Winner-seat decisions only; ~330 rows/game, f16.
- **Head**: `train_policy_head.py` -- same architecture and features
  as the value net (no new schema), BCE on "the eventual winner chose
  this afterstate" vs the winner's declined candidates; holdout RANK
  accuracy (chosen ranks first) vs the 1/k random baseline is the
  training-side sanity metric.
- **Blend**: decision-time only, `PENTA_POLICY_NET` +
  `PENTA_POLICY_WEIGHT` env (workers inherit; training runs leave the
  vars unset): the MYOPIC screen becomes (1-w)*value + w*policy BEFORE
  the top-k playout stage, so winner-typical but myopically-ugly
  actions survive into the playouts. Terminals stay exact; the refined
  argmax stays pure value. w swept over {0.25, 0.5}.
- **Acceptance, behavioral first**: Counterburn counterspells/game
  (~0.1 baseline), The Deck spells/game + big-threat casts, sweep wins
  vs the 7/128 / 4/128 references; 120-gate sanity, deliverable behind
  the 57.5% bar throughout.

**v1 results (2026-08-11): SCALE -- and the first certified
better-than-handcrafted configuration.** Archive: 1462 decisive games,
384k candidate rows over 109k decisions; head holdout rank-accuracy
70.9% vs 33.5% random. Behavioral table (128-game sweeps, sweep seeds):

| pilot seat | metric | baseline | w=0.25 | w=0.5 |
| --- | --- | --- | --- | --- |
| Counterburn | wins | 58/128 | **67/128** | 64/128 |
| Counterburn | counterspells/g | 0.09 | 0.16 | 0.14 |
| The Deck | wins | 4/128 | 6/128 | 5/128 |
| The Deck | spells/g | 5.4 | **6.2** | **6.4** |
| The Deck | big-threat casts/g | 0.03 | 0.11 | 0.11 |
| The Deck | engine casts/g | 0.10 | 0.30 | 0.28 |

The first lever of the whole arc to move casting behavior materially:
The Deck +0.8-1.0 spells/game, engines x3, big threats x4, and wins up
on both seats. Counterspells rose 0.09 -> 0.16, short of the 1+ hope
-- the offer-scarcity constraint (tapping out) still binds. Sanity
gates: w=0.25 58.3% (LCB 49.4%), w=0.5 55.8%; the standing confirm-400
on the w=0.25 blend: **57.0%, LCB 52.1% -- the first configuration
whose 400-game lower confidence bound clears 50%** (previous best:
54.8%, LCB 49.9%). Certified config = penta_net.npz (value net,
ckpt-v3-48000, artifact unchanged) + policy_head.npz at
PENTA_POLICY_WEIGHT=0.25. Natural scale-ups, in order of expected
value: iterate the head (bigger archive from BLENDED self-play,
DAgger-style), let training run under the blend, and revisit w.

**Scale-up results (2026-08-11).** Stage 1, DAgger iteration: blended
self-play archive (1617 decisive games, 461k rows), head rank-acc
72.0%; behavior holds, 120-gate 59.2% (LCB 50.2%), confirm-400 57.1%
(LCB 52.1%) -- accepted, head versioned as policy_head.ckpt-dagger1.
Stage 2, value training under the blend (4 x 4000 games from 48k,
seeds 7M+): gates 52.5 / 58.8 / 55.0 / 58.3 vs the 59.2 bar -- no
promotion, a plateau straddling the bar; stability data point:
training under the blend held the band, no collapse. (Chunk 15 also
surfaced the engine-panic class fixed by the clone armor + upstream
lacker/penta#38; field frequency 1 panicking game / 8000.) Stage 3,
w sweep on the certified pair: w=0.15 -> **60.0%/120** (best-ever),
w=0.25 -> 59.2%, w=0.35 -> 56.7%; confirm-400 at w=0.15: **57.4%, LCB
52.4% -- the best certification yet**. CERTIFIED PAIR: penta_net.npz
(ckpt-v3-48000 value) + policy_head.ckpt-dagger1.npz at
PENTA_POLICY_WEIGHT=0.15.

## Playing against the bot (play_server.py)

`play_server.py` serves a local human-vs-bot game over HTTP; the client
page is `web/penta-play.html` (linked from the dashboard nav as "Play
penta bot"):

```bash
cd penta-bot && python3 play_server.py     # API on http://localhost:4180
```

The web page itself is served by the ARENA web server (`web/server.mjs`,
port 4173, our C++ engine's dashboard -- a separate process this server
does not touch) or any static server over `web/`; it talks to port 4180
for game state. The bot seat plays the CERTIFIED configuration through
`trainer.choose()` -- value net `penta_net.npz` + policy head
`policy_head.ckpt-dagger1.npz` at `PENTA_POLICY_WEIGHT=0.25`, dominance
prune, rollout search at the gate config (topk 4 / 1 playout / budget
120 / playout-max-eval 8, max-eval 16) on true `game.clone()` forks --
so you face exactly the bot that gates. Every human choice is a
`legalActions` index rendered as a labeled button (mulligans, casts
with targets/X, attacks, blocks, response windows, mid-resolution
decisions); nothing bypasses the list. Concede is session-level only
(protocol 2 removed Concede from the action list). Read-only over the
training artifacts; safe to run beside an active training run.
`test_play_server.py` drives full games through the HTTP API (random
human) and re-derives the bot's moves offline to assert they match
`trainer.choose()`.

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
