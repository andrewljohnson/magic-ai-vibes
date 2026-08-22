# penta-bot

Reinforcement learning for **lacker's penta** — a compact Magic: The
Gathering engine. This repo trains one thing: the strongest **honest** bot
we can, to play on lacker's server.

Everything else that used to live here (a C++ engine, a browser arena, a
human-vs-bot play server) has been removed. We are not building an engine
and we are not building a site.

```
RESULTS.md    what we measured, and what it means
ROADMAP.md    what to try next
penta-bot/    the bot: training, evaluation, monitor
```

## Honest

The deployed actor sees only a **redacted observation** — its own hand,
the public zones, and the opponent's hand *size*. It never sees the
opponent's hidden hand. That constraint is the whole problem; everything
in the design exists to get strength out of it.

Decklists are **open**: the bot knows which of the built-in decks the
opponent is piloting. Only their hand is hidden. (Inferring the deck from
revealed cards was the old default and is still available behind
`--classify-decklist`; it is 76.6% accurate and 0% on turn 1.)

## Where it stands

| | honest gate vs the built-in bot |
|---|---|
| the handcrafted bot we set out to beat | 31.6% |
| best trained actor, 800-game gate | **51.1%** |

See `RESULTS.md` for the full record and the error bars — gates are 400
games (±2.5 points), so single gates are noise and only means count.

## Setup

Needs Rust and Python 3.13 (the engine binding pins `abi3-py313`).

```bash
# 1. build the engine binding for this platform
cd penta-bot/vendor/penta/bindings/penta-py
cargo build --release
cp target/release/libpenta.so ../../../../engine-0.7.0/penta.so
cd ../../../..

# 2. python + torch
uv venv --python 3.13 .venv-torch
uv pip install --python .venv-torch/bin/python torch numpy

# 3. the native self-play runner
cd spz-core && cargo build --release
cp target/release/libspz_core.so ../spz_core.so && cd ..

# 4. sanity: must print 1081
PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python -c \
  "from extractor import Extractor; print(Extractor(version=2, belief=True).size)"
```

If step 4 prints anything else, the card catalog differs from the one the
saved actors were trained against and they will not transfer.

## Train

```bash
cd penta-bot
PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python aac_torch_par.py \
  --native --native-threads 6 --belief --hidden 256 \
  --games 200000 --gate-every 3000 --gate-games 400 \
  --log myrun.log --save-prefix myrun
```

One process saturates around 3 cores, so **fill a big machine with
several concurrent runs, not one wide one** — see RESULTS.md.

## Watch

```bash
cd penta-bot && python3 monitor.py     # -> http://localhost:8899
```

Reads the trainers' own logs. No dependencies, no build step.

## Check

```bash
# native rows must stay bit-identical to the Python reference path
PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python aac_lockstep.py \
    --episodes 3 --hidden 256 --belief --actor <actor>.npz
```
