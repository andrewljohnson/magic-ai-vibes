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
`handcrafted`, or `learned`. `--rollouts` and `--deep-rollouts` control sampled
continuations; `--train-games` controls Learned Value's random self-play
dataset. `--train-seed` controls model generation independently from the
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
attacks, blocks, and damage order. The other three policies retain the shared
random combat sub-policy.

Learned Value contains no scripted card values or card-specific action rules.
It is a dependency-free ensemble of two 16-hidden-unit neural networks. Inputs
include scalar state (life, zone sizes, mana, creature power/toughness, stack,
turn) plus neutral per-card count planes for its own library and hand and every
public zone: both battlefields, tapped/summoning-sick/damaged permanents, both
graveyards, and stack objects by controller. Opponent hand identities and
library order remain hidden. A learned linear skip path helps sparse card-zone
signals while the hidden layer learns interactions.

Training starts with random games, then runs two generations of
Learned-vs-Learned fitted self-play with 10%/5% legal-action exploration.
Terminal returns are discounted by game length, so dominant decks still learn
to win efficiently. Counterspell choices and activated-ability states receive
targeted replay. The two networks train concurrently and average predictions.

During play, every legal action receives an immediate value plus two
stack-faithful, four-turn Learned-vs-Learned continuations with re-randomized
hidden libraries. Combat enumerates small legal attack/block spaces and samples
bounded candidates for large spaces, using learned expected value throughout.

This is AlphaGo-inspired, not a full AlphaGo implementation: it is an ensemble
value function plus fitted self-play and shallow candidate search, without a
learned policy head or PUCT/MCTS tree yet.

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

At 100 games per matchup with seed `424242`, 800 training games, two standard
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

In the latest completed eight-seed all-policy panel, Learned beat Random
1473–127, Monte Carlo 1289–311, and Deep Monte Carlo 1164–436, with every seed
and deck passing. It beat Handcrafted 857–743 overall (53.6%, 95% CI
51.1–56.0%) but did not clear the strictest stability gate: one seed lost,
pooled Red was 138–163, and White was essentially tied at 293–292.

That limitation is intentional and visible. The next strength milestone is a
learned action-policy head or actor-critic objective; the experiment notebook
documents why additional scalar value tuning, rollout depth, and raw training
volume were rejected.
