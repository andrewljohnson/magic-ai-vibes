# Early Magic Bot Simulator

A dependency-free C++20 MVP for simulating a round robin between four early
*Magic: The Gathering* decks:

- Green: 18 Forest, 9 Grizzly Bears, 12 Ironroot Treefolk, 1 Tsunami
- Red: 18 Mountain, 10 Lightning Bolt, 12 Fire Elemental
- Blue: 18 Island, 14 Counterspell, 8 Water Elemental
- White: 22 Plains, 3 Millstone, 15 Moat

The original three decks use Alpha cards. The white deck is an explicit
early-Magic exception: Millstone debuted in *Antiquities* and Moat debuted in
*Legends*.

The default command runs 100 games per matchup (600 games total) with Random,
Monte Carlo, Deep Monte Carlo, Handcrafted Policy, and Learned Value bots:

```sh
make run
```

For reproducible or larger runs:

```sh
make
./build/alpha-sim --games 100 --seed 42 --bots mixed \
  --rollouts 2 --deep-rollouts 8 --train-games 800 \
  --train-seed 424242
```

`--bots` accepts `mixed`, `random`, `monte-carlo`, `deep-monte-carlo`,
`handcrafted`, `learned`/`learned-value`, or `learned-actor`. The default
Learned bot and the Learned seat in `mixed` are the value-search champion;
the unified actor is an explicit research challenger and is not silently put
in the mixed field. `--rollouts` and `--deep-rollouts` control sampled
continuations; `--train-games` controls the selected Learned model's training
set. `--train-seed` controls model generation independently from the
game/evaluation `--seed` and defaults to `424242`. Mixed mode uses a 25-game
rotation containing all ordered policy
pairings, including mirrors. Games in a policy matrix use common shuffle seeds
and balanced play/draw assignments, and independent games run in parallel.

Each run prints:

- Per-deck records, play/draw splits, ending life, card, spell, damage, and mill
  statistics
- A per-deck ranking showing the win-rate lift from every stronger policy
- Per-bot records, decision counts, and rollout counts
- Direct records for all ten cross-policy bot matchups
- Overall game length and finish reasons

Run the test suite:

```sh
make test
```

Evolve a new 40-card deck from the union of the current card pool:

```sh
make evolve

./build/alpha-sim --evolve-deck --generations 20 \
  --population 32 --games 8 --seed 42
```

Evolution starts from the four metagame decks, uses mutation plus elitism, and
scores every candidate against all four decks with both deck seats and both
starting-player assignments. `--games` is paired repetitions per opponent in
this mode. The default pilot is Handcrafted Policy so searches stay fast and
do not evaluate hybrid decks far outside the value model's training data.

## MVP rules implemented

- 20 starting life, shuffled 40-card decks, seven-card opening hands
- Random starting player; the starting player skips their first draw
- Untap, draw, first main, combat, second main, and cleanup
- One land play per turn and colored/generic mana payment
- Creature, sorcery, instant, artifact, and enchantment spells are cast onto a
  LIFO stack
- Priority alternates between players; after two consecutive passes, the top
  spell resolves, or the phase ends when the stack is empty
- Priority windows currently occur in first main, beginning of combat, end of
  combat, and second main. Post-attacker and post-blocker priority windows are
  not implemented yet.
- The casting player retains priority after casting, and the active player
  receives priority after a spell resolves
- Creatures, sorceries, artifacts, and enchantments require an active-player
  main phase and an empty stack; instants can be cast in response
- Summoning sickness, tapping to attack, legal blocking, simultaneous combat
  damage, and lethal-damage cleanup
- Lightning Bolt can legally target either player or any creature
- Counterspell can target and counter any spell, including another
  Counterspell
- Tsunami is a sorcery and destroys all Islands only when it resolves
- Millstone resolves as an artifact permanent; paying two mana and tapping it
  puts a targeted mill-two activated ability on the stack
- Counterspell cannot target Millstone's activated ability because it is not a
  spell
- Moat only prevents creatures without flying from attacking after the
  enchantment spell resolves
- Loss by zero life or drawing from an empty library

## Bot policies

The random policy uniformly chooses from the legal priority actions. Both flat
Monte Carlo policies score every legal priority action with repeated random
game continuations, using 1 point for a win, 0.5 for a draw, and 0 for a loss.
The standard bot defaults to two continuations per action; Deep Monte Carlo
defaults to eight. They choose randomly among equally scored actions.

Every continuation re-randomizes both remaining libraries, since their order is
hidden information. The candidate action and existing stack resolve, then the
rest of the game is played by random bots from the following turn. The real
game still uses normal alternating priority and LIFO stack resolution.

Handcrafted Policy is deliberately simpler and cheaper than Monte Carlo. It uses
card-aware rules to play lands first, avoid self-targeting, Bolt useful targets,
counter opposing spells, mill the opponent, preserve Counterspell mana, and
pass priority to resolve its own stack objects. It also chooses favorable
attacks, blocks, and damage order.

Both Learned variants contain no scripted card values or card-specific action
rules.
It is a dependency-free ensemble of two 16-hidden-unit neural networks. Inputs
include scalar state (life, zone sizes, mana, creature power/toughness, stack,
turn) plus neutral per-card count planes for its own library and hand and every
public zone: both battlefields, tapped/summoning-sick/damaged permanents, both
graveyards, and stack objects by controller. Opponent hand identities and
library order remain hidden. A learned linear skip path helps sparse card-zone
signals while the hidden layer learns interactions.

Learned Value training starts with random games, then runs two generations of
Learned-Value-versus-Learned-Value fitted self-play with 10%/5% legal-action
exploration.
Terminal returns are discounted by game length, so dominant decks still learn
to win efficiently. Counterspell choices and activated-ability states receive
targeted replay. The two networks train concurrently and average predictions.

During play, every legal action receives an immediate value plus two
four-turn Learned-Value mirror continuations over common information-set
samples. They use normal pass/stack handling and resume from the current phase
through every priority window the MVP engine currently models. Combat
enumerates small public-board attack/block spaces and samples bounded
candidates for large spaces, using learned expected value throughout.

`learned-actor` keeps the separate unified policy-head experiment: its priority,
attack, block, and damage-order heads train from information-safe search and
self-play data. It remains available for direct comparison but is not the
default. This is AlphaGo-inspired, not a full AlphaGo implementation: there is
no PUCT/MCTS tree.

Actor G0 is that frozen unified-actor model. Actor G1 clones G0, keeps the
parent immutable, and performs one experimental search-as-teacher generation:
24 balanced G0-mirror self-play games provide information-set-safe Priority and
Attack targets plus TD(lambda) critic targets. `learned-actor` and
`learned-actor-g0` select G0; `learned-actor-g1` selects the one-generation
candidate in benchmark arguments. G1 is a research challenger, not an accepted
champion, and neither actor generation replaces the default Learned Value bot.

`learned-value-g8` is a separate immutable eight-generation Value challenger.
It retains its base checkpoint and G1 through G8, so probe runs can attribute
the first generation where a decision changes. Because the canonical
800-game recipe is expensive, benchmark and probe routes transparently cache
the complete frozen bundle at
`build/model-cache/value-g8-v1-t800-s424242.bin` (with the requested training
game count and seed in the filename). The first route prints `generated`;
later matching routes print `loaded` and reproduce the exact report,
fingerprints, and IEEE-754 model weights. A corrupt or mismatched artifact
fails closed with an actionable error. Use `--refresh-value-g8-cache` on a
benchmark or probe route that selects G8 to retrain and atomically replace it.
Legacy `learned`/`learned-value`/`learned-value-g0` behavior is unchanged.
For benchmarks only, `learned-value-g1` through `learned-value-g7` select the
matching immutable checkpoint from that same validated bundle; comparing two
such checkpoints loads or generates the bundle once and never retrains an
individual checkpoint. Probe `--value-generation` remains limited to `0|8`
because its G8 mode already reports the full ordered checkpoint table.

`learned-value-mix50-g8` is a distinct Late-Mix50 collection challenger.
Its base and G1-G4 use the canonical recipe; in G5-G8, consecutive game pairs
alternate raw Value and information-safe K=1/H=4 Value search, exactly 50/50
by games. The progress report prints raw/search game and example counts
separately. Its artifact is isolated at
`build/model-cache/value-g8-mix50-v1-t800-s424242.bin` (parameterized by the
requested game count and seed), and `--refresh-value-mix50-cache` refreshes
only that recipe. Canonical and Mix50 bundles are validated independently and
cannot be substituted for one another.

## Bot benchmark harness

Use the paired harness to decide whether a challenger is actually stronger:

```sh
make benchmark
make benchmark-deep
make benchmark-learned

./build/alpha-sim --benchmark --games 20 --seed 424242 \
  --challenger handcrafted --baseline monte-carlo --rollouts 2

./build/alpha-sim --benchmark --games 20 --seed 424242 \
  --challenger handcrafted --baseline deep-monte-carlo --deep-rollouts 8

./build/alpha-sim --benchmark --games 20 --seed 424242 \
  --challenger learned --baseline monte-carlo \
  --rollouts 2 --train-games 800 --train-seed 424242

./build/alpha-sim --benchmark --games 5 --seed 101 \
  --challenger learned-actor --baseline learned-value \
  --train-games 800 --train-seed 424242

./build/alpha-sim --benchmark --games 15 --seed 101 \
  --train-games 800 --train-seed 424242 \
  --challenger learned-actor-g1 --baseline learned-actor-g0

./build/alpha-sim --benchmark --games 15 --seed 101 \
  --train-games 800 --train-seed 424242 \
  --challenger learned-value-g3 --baseline learned-value-g0

./build/alpha-sim --benchmark --games 15 --seed 202 \
  --train-games 800 --train-seed 424242 \
  --challenger learned-value-mix50-g8 --baseline learned-value-g0
```

For every repetition, it covers all ten unordered deck pairings, including
mirrors; swaps which policy pilots each deck; forces both play/draw positions;
and reuses seeds across paired games to reduce shuffle noise. `--games 20`
therefore runs 800 games. It reports per-deck records, rollout cost, a Wilson
95% confidence interval, and passes only when the challenger's lower bound is
above 50%.

Validate Learned against every other policy over independent seeds:

```sh
make stability
```

The all-policy harness trains one model from `--train-seed`, reuses that exact
model against Random, Monte Carlo, Deep Monte Carlo, and Handcrafted Policy
across every evaluation seed, and requires aggregate, per-seed, confidence,
and per-deck gates.

To measure training-seed and evaluation-seed variance separately:

```sh
./build/alpha-sim --variance-study --games 5 --train-games 800
```

This fixed 3x3 study trains each row model once and reuses it across all three
evaluation-seed columns. `EXPERIMENTS.md` is the lab notebook for successful
and failed tuning and evaluation runs.

## Offline decision probes

The probe CLI labels and scores a fixed 16-position development corpus without
reading the opponent's hidden cards:

```sh
./build/alpha-sim --score-probes \
  --probe-worlds 128 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v2.labels.tsv

./build/alpha-sim --score-probes \
  --actor-generation 1 --probe-worlds 8 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v2-k8-h0-audit.labels.tsv

./build/alpha-sim --score-probes \
  --actor-generation 0 --value-generation 8 \
  --probe-worlds 8 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v2-k8-h0-audit.labels.tsv

./build/alpha-sim --score-probes \
  --actor-generation 0 --value-recipe mix50 --value-generation 8 \
  --probe-worlds 8 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v2-k8-h0-audit.labels.tsv
```

Every candidate is evaluated on the same sampled information-set worlds and
continuation seeds. The cache is atomic and bound to the exact model,
corpus, reference algorithm, and scoring semantics. The run also verifies
that labels, policy scores, critics, and metrics are bit-identical after
repartitioning hidden zones. `--refresh-probe-cache` deliberately regenerates
a matching cache.

Frozen Actor G0 always owns and generates the cached reference labels.
`--actor-generation 1` changes only the candidate being scored; G1 cannot
relabel the cache. Even when explicitly refreshing a cache, labels are
regenerated from G0 rather than from the candidate.
Selecting Value G8 likewise leaves the Actor-owned labels untouched and adds
an ordered compact attribution table for the G8 base plus G1 through G8.
`--value-recipe mix50 --value-generation 8` does the same for the Mix50 base
and G1 through G8. Switching recipes changes only the scoring candidates:
frozen Actor G0 remains the label/cache owner, and legacy Value G0 remains the
continuation-sensitivity reference.

`probe-dev-v2` keeps its plan choices root-irreversible using ordinary game
states; deployed Pass semantics are not altered. Horizon zero completes the
current turn, prepares the next turn, and bootstraps from the frozen critic.
Longer horizons are available, but the experiment notebook documents cases
where terminal-outcome saturation erases a valid tactical signal.

Probe agreement, regret, candidate-Q error, and calibration are development
diagnostics only. Four positions per deck cannot establish that a bot is
stronger; the paired benchmark and multi-seed confidence gates remain the
authority.

There are no mulligans, sideboards, concessions, or draw effects yet. A
500-individual-turn safety limit is included, though these decks normally end
far earlier.

## Balance

The original Bears-versus-Bolts lists produced a 95.6% green win rate over a
10,000-game seeded run. The four current lists were tuned over larger seeded
round robins. At 30,000 games per matchup with seed `424242`, the results were:

- Green 48.6% / Red 51.4%
- Green 47.8% / Blue 52.2%
- Green 54.0% / White 46.0%
- Red 53.6% / Blue 46.4%
- Red 52.4% / White 47.6%
- Blue 51.6% / White 48.4%

The test suite includes a deterministic balance guard that keeps both decks in
every pairing inside a 45–55% win-rate band.

## Mixed-bot baseline

The following is a historical unified-actor baseline, not a result from the
current restored value-search default. At 100 games per matchup with seed
`424242`, 800 training games, two standard
rollouts, and eight deep rollouts, the current five-policy 600-game run took
about 26 seconds on the development machine. Aggregate seat-game win rates
were 23.8% Random, 40.8% Monte Carlo, 49.2% Deep Monte Carlo, 63.3%
Handcrafted Policy, and 72.9% Learned Value.

Learned produced the largest lift on Green (55.0% vs 28.3% Handcrafted), Red
(68.3% vs 56.7%), and Blue (75.0% vs 73.3%). On the small 60-game White slice,
Handcrafted led 95.0% to 93.3%. The controlled paired harness is the authority
for strength claims because these mixed per-deck slices remain noisy.

In the controlled 800-game harness, Handcrafted Policy beat standard Monte Carlo
618–182 (77.2%, 95% CI 74.2–80.0%) while using no rollouts. Against Deep Monte
Carlo it won 563–237 (70.4%, 95% CI 67.1–73.4%); Deep averaged about 620
rollout continuations per game.

That historical result predates the information-safe rollout correction and is
not a current strength claim. With one frozen model and three independent
evaluation seeds, the honest value-search baseline scored 43.3% (260–340)
against Handcrafted; the unified actor was statistically tied with it. The old
54% “champion” was inflated by hidden-information leakage, especially on
Counterspell decisions.

The active milestone is an iterated, information-safe search-as-teacher actor
and critic. `EXPERIMENTS.md` records every preregistered result, including
failed approaches and the exact gates required before any model is promoted.
