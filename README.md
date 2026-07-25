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
Monte Carlo, Deep Monte Carlo, Strategic, and Learned Value bots:

```sh
make run
```

For reproducible or larger runs:

```sh
make
./build/alpha-sim --games 100 --seed 42 --bots mixed \
  --rollouts 2 --deep-rollouts 8 --train-games 200
```

`--bots` accepts `mixed`, `random`, `monte-carlo`, `deep-monte-carlo`,
`strategic`, or `learned`. `--rollouts` and `--deep-rollouts` control sampled
continuations; `--train-games` controls Learned Value's random self-play
dataset. Mixed mode uses a 25-game rotation containing all ordered policy
pairings, including mirrors.

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

Strategic is deliberately simpler and cheaper than Monte Carlo. It uses
card-aware rules to play lands first, avoid self-targeting, Bolt useful targets,
counter opposing spells, mill the opponent, preserve Counterspell mana, and
pass priority to resolve its own stack objects. It also chooses favorable
attacks, blocks, and damage order. The other three policies retain the shared
random combat sub-policy.

Learned Value contains no card-name or card-ID rules. A dependency-free
22-input, 16-hidden-unit neural network is trained from random self-play game
outcomes. Its features are generic quantities only: life, library and hand
sizes, mana, creature counts and aggregate power/toughness, other permanent
counts, stack state, and whose turn it is. During play it scores legal
successor states with the network. For combat it samples legal attack and block
candidates and keeps the one with the highest learned value.

This is AlphaGo-inspired, not a full AlphaGo implementation: it is a learned
value function plus shallow candidate search, without a policy network,
PUCT/MCTS tree, or iterative neural self-play yet.

## Bot benchmark harness

Use the paired harness to decide whether a challenger is actually stronger:

```sh
make benchmark
make benchmark-deep
make benchmark-learned

./build/alpha-sim --benchmark --games 20 --seed 424242 \
  --challenger strategic --baseline monte-carlo --rollouts 2

./build/alpha-sim --benchmark --games 20 --seed 424242 \
  --challenger strategic --baseline deep-monte-carlo --deep-rollouts 8

./build/alpha-sim --benchmark --games 20 --seed 424242 \
  --challenger learned --baseline monte-carlo \
  --rollouts 2 --train-games 200
```

For every repetition, it covers all ten unordered deck pairings, including
mirrors; swaps which policy pilots each deck; forces both play/draw positions;
and reuses seeds across paired games to reduce shuffle noise. `--games 20`
therefore runs 800 games. It reports per-deck records, rollout cost, a Wilson
95% confidence interval, and passes only when the challenger's lower bound is
above 50%.

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

At 100 games per matchup with seed `424242`, two standard rollouts, and eight
deep rollouts, the five-policy 600-game mixed run completed in about 5.6
seconds on the development machine, including 200 self-play training games.
Aggregate seat-game win rates were 21.7% Random, 44.2% Monte Carlo, 51.7% Deep
Monte Carlo, 68.8% Strategic, and 63.8% Learned Value.

The output ranked Blue as the biggest beneficiary of deeper Monte Carlo search:
its Random win rate was 11.7%, standard Monte Carlo was 38.3% (+26.7 points),
and Deep Monte Carlo was 60.0% (+48.3 points). The lists remain balanced under
the original random policy; stronger policies expose a separate deck-balance
problem for the next tuning pass.

In the controlled 800-game harness, Strategic beat standard Monte Carlo
618–182 (77.2%, 95% CI 74.2–80.0%) while using no rollouts. Against Deep Monte
Carlo it won 563–237 (70.4%, 95% CI 67.1–73.4%); Deep averaged about 620
rollout continuations per game.

Learned Value beat standard Monte Carlo 644–156 (80.5%, 95% CI 77.6–83.1%)
and Deep Monte Carlo 575–225 (71.9%, 95% CI 68.7–74.9%). Against the
card-aware Strategic bot it went 390–410 (48.8%, 95% CI 45.3–52.2%), an
inconclusive result close to parity.
