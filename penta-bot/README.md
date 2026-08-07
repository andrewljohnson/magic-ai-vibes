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
- `checks.py` -- 26 standalone audit checks against the live engine: seat/
  perspective labels (recorded rows must be the learner's own view with its
  own outcome, in mirror, frozen-league, and handcrafted-league games), TD
  target recursion, candidate-sampling uniformity, afterstate/fork
  exactness, replay-ring arithmetic, league seat scheduling. Run
  `python3 checks.py` after any trainer change.

## Collapse post-mortem (2026-08 audit)

Two league-guarded curve runs collapsed (handcrafted gate 16.5% -> 5%,
then 17.5% -> 5.8%). A full audit (checks.py) cleared every labeling
hypothesis -- no seat inversion, no TD off-by-one, no sampling prefix
bias, no wrong-perspective afterstates, no ring bug. The defect was
quantitative: **per-decision TD lambda 0.9 on penta trajectories of
40-130 recorded decisions per seat leaves 55-78% of targets with under
5% outcome weight**, so the net mostly regressed toward its own previous
values; BCE fell while strength collapsed. The C++ SPZ recipe this port
mirrors defaults to lambda 1.0 (and its champion line trains on hard
undiscounted outcomes); its shorter effective trajectories made 0.9
viable there. Fixes: `--td-lambda` defaults to 1.0 (pure outcome
targets); frozen-league games record both seats (true outcome-labeled
data, learner-net bootstraps); league learner seats de-aliased from the
deck-pair rotation (both had even periods, so each ordered pair always
seated the learner on the same side).

## Training config (smoke run)

3000 self-play games, decks Sligh / White Weenie / The Deck / Counterburn
(all ordered non-mirror pairs, rotated), hidden 64, lr 0.01, batch 256,
momentum 0.9, TD lambda 0.9, epsilon 0.25 -> 0.03 linear, replay capacity
150k, max 16 afterstates evaluated per decision (8/4 past depth 200/400),
8 worker processes. Weights: `penta_net.npz` (engine/protocol version
pinned in the file's metadata).

## Gate results (200 games each, alternating seats, fixed seeds)

Greedy afterstate policy (epsilon 0), weights `penta_net.npz` (3000 games),
seeds 5000000+, deck pairs rotating over the four training decks:

| opponent | result | win rate | Wilson 95% LCB |
| --- | --- | --- | --- |
| `random` | 171 W / 2 D / 27 L | **86.0%** | 80.0% |
| `handcrafted` | 33 W / 0 D / 167 L | **16.5%** | 12.0% |

Beating random decisively: met. Beating handcrafted was not expected from a
3000-game smoke run and did not happen -- 16.5% is the honest number (for
scale, their scripted `first_bot.py` heuristic wins ~73% vs handcrafted in
its own Sligh-vs-The-Deck harness). Training loss over the run: BCE
0.692 -> 0.603 at a sustained ~3.5 games/s across 8 workers (~14 min of
self-play). The win-vs-random trend was still rising steeply at the end
(56% at a 384-game checkpoint, ~96% on a 24-game spot check at 1600 games),
so this is nowhere near converged.

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
