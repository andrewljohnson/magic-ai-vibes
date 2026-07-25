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
Monte Carlo, and Deep Monte Carlo bots:

```sh
make run
```

For reproducible or larger runs:

```sh
make
./build/alpha-sim --games 100 --seed 42 --bots mixed \
  --rollouts 2 --deep-rollouts 8
```

`--bots` accepts `mixed`, `random`, `monte-carlo`, or `deep-monte-carlo`.
`--rollouts` and `--deep-rollouts` control the sampled continuations per legal
action. Mixed mode uses a nine-game rotation containing every same-policy
pairing and both seat orders for every cross-policy pairing.

Each run prints:

- Per-deck records, play/draw splits, ending life, card, spell, damage, and mill
  statistics
- A per-deck ranking showing the win-rate lift from both Monte Carlo depths
- Per-bot records, decision counts, and rollout counts
- Direct records for all three cross-policy bot matchups
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

Combat declarations remain the shared random sub-policy in this first Monte
Carlo MVP: legal attackers, blockers, and multi-block damage order are chosen
randomly. The Monte Carlo bot currently controls land plays, spell casting,
targets, responses, activated abilities, and passing priority.

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
deep rollouts, the 600-game mixed run completed in about 9 seconds on the
development machine. Aggregate seat-game win rates were 33.3% Random, 55.5%
Monte Carlo, and 61.3% Deep Monte Carlo. Deep beat standard Monte Carlo 76–56
head-to-head.

The output ranked White as the biggest beneficiary of deeper search: its
Random win rate was 41.4%, standard Monte Carlo was 97.0% (+55.6 points), and
Deep Monte Carlo was 99.0% (+57.6 points). The lists remain balanced under the
original random policy; stronger policies expose a separate deck-balance
problem for the next tuning pass.
