# Old School Magic — engine, bots, and arena

A fast, deterministic C++20 engine for a compact Old School
*Magic: The Gathering* card pool, with a browser arena and a ladder of
bots. The five-deck metagame:

- Green: 18 Forest, 9 Grizzly Bears, 8 Ironroot Treefolk, 4 Giant Growth,
  1 Tsunami
- Red: 15 Mountain, 9 Lightning Bolt, 7 Ironclaw Orcs, 4 Gray Ogre,
  3 Hill Giant, 2 Fire Elemental
- Blue: 15 Island, 1 Mox Sapphire, 1 Sol Ring, 1 Ancestral Recall,
  1 Time Walk, 1 Braingeyser, 4 Flying Men, 4 Force Spike,
  8 Counterspell, 4 Air Elemental
- White: 22 Plains, 3 Millstone, 15 Moat
- RU Aggro: 13 Mountain, 4 Island, 3 Flying Men, 5 Ironclaw Orcs,
  2 Gray Ogre, 8 Hill Giant, 3 Lightning Bolt, 2 Disintegrate

## Bots

- **Random** — uniform over legal actions.
- **Monte Carlo / Deep Monte Carlo** — random-continuation sampling.
- **Handcoded Policy** — the compact rules-aware baseline.
- **Self-Play Zero (SPZ)** — the champion. A value network learned purely
  from mirror self-play outcomes, played with determinized rollout
  lookahead and rules-only waste pruning. No hand-authored card values.
  SPZ beats the Handcoded Policy ~58-60% in deck/seat/play-draw-balanced
  paired benchmarks and pilots every metagame deck at or above the
  Handcoded Policy's win rate.

## Build and test

```sh
make            # selfplay-zero + web bridge
make test       # engine, SPZ, bridge, and web test suites
```

## Play in the browser

```sh
make web        # http://127.0.0.1:4173 (PORT=NNNN to change)
```

The browser never invents rules: every button comes from the engine's
legal-action list, and hidden information stays on the engine side.

## SPZ training and benchmarks

```sh
# Train a fresh champion (league self-play, then rollout fine-tuning):
./build/selfplay-zero train --out build/spz.txt --iterations 120 \
    --games 256 --threads 8 --seed 424243
./build/selfplay-zero train --out build/spz-ft.txt --init build/spz.txt \
    --iterations 40 --games 96 --threads 8 --rollout --lr 0.003 \
    --eps-start 0.06 --eps-final 0.03 --seed 991177

# Paired benchmark against a baseline:
./build/selfplay-zero benchmark --model data/spz-champion-v6.txt \
    --reps 20 --seed 101 --threads 8 --worlds 4 --rollout \
    --baseline handcrafted
```

The deployed champion lives at `data/spz-champion-v6.txt`.

## Live training monitor

Add `--telemetry build/telemetry/telemetry.jsonl --probe-every 10
--probe-baseline data/spz-champion-v6.txt` to any `train` run, then:

```sh
cp web/training-monitor.html build/telemetry/monitor.html
python3 -m http.server 4175 --directory build/telemetry --bind 127.0.0.1
```

Open <http://127.0.0.1:4175/monitor.html> for live strength-probe and
loss charts (probes are 100-game paired sets vs Handcrafted and
head-to-head vs the frozen champion).
