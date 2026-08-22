# penta-bot

Reinforcement learning for **[penta](https://github.com/lacker/penta)** — a
deterministic, headless Magic: The Gathering simulator built for writing AI
bots against. Rust engine, Python bindings, Old School 93/94 with fifteen
built-in archetypes.

This repo trains one thing: the strongest bot we can, and keeps it
registered and playing on the public server at
`https://penta.lacker.workers.dev`.

We are not building an engine and not building a site. The engine is
upstream's; the server is upstream's. We build the player.

```
RESULTS.md    what we measured, and what it means
ROADMAP.md    what to try next
penta-bot/    the bot: training, evaluation, monitor, hosted daemon
```

## What the bot can see

This is the whole problem, so it is worth stating precisely.

**The opponent's deck is not disclosed.** Upstream's bot guide is explicit
that the protocol carries no deck metadata: registration declares *your
own* deck, the heartbeat's `deck` field is *your own*, and the observation
has no archetype field. A bot infers what it is facing from cards it has
actually seen played.

**The hand and both libraries are closed.** The bot gets a redacted
observation — its own hand, the public zones (battlefield, graveyards,
exile, stack), and the opponent's hand *count*. Never their hand
contents, never either library's order.

What it does get is the **pool of fifteen built-in archetypes**. So the
inference chain is: classify which archetype the opponent is on from
their revealed cards, then subtract everything visible from that
decklist to estimate what is still hidden. That is what the **belief
features** encode — a per-card unseen-pool count for each player. 825
base features + 256 belief = the 1081-dimensional input.

Classification is imperfect, and worst early: measured 76.6% overall, but
0% on turn one (nothing has been revealed yet), 53% on turn two, ~90% by
turn eight.

`--open-decklist` hands training the opponent's true decklist instead.
That is a **research setting only** — it makes results comparable to
older determinized-search numbers, which were measured that way. It must
not be used to train a bot intended for the server, which will not supply
that information.

The engine enforces the boundary: bots choose an index from a
hidden-information-safe legal-action list, so a bot cannot read state it
was not offered.

## Architecture

**Actor** — `1081 → 256 → 1`, scores *afterstates*: fork the game, play
each legal move, featurize the result, softmax over the scores. Sees only
the redacted observation. This is what deploys.

**Critic** — `2162 → 256 → 1`, takes both seats' redacted observations
concatenated, so it effectively sees both hands. It exists to lower the
variance of the advantage estimate during training and is **never
deployed**; it would be reading information the actor is not allowed.

Trained with PPO + GAE, roughly half self-play and half against the
engine's built-in bot.

**Generation runs natively.** Rust (`spz-core/`) plays the games,
featurizes, and scores; Python keeps PPO, GAE, gating and checkpointing.
They meet once per training round over flat buffers — 0.9 → 7.8 games/sec.
`aac_lockstep.py` holds the native rows bit-identical to the Python
reference path.

## Where it stands

| | win rate vs the engine's built-in bot |
|---|---|
| the built-in bot we set out to beat | 31.6% |
| best trained actor, 800-game evaluation | **51.1%** |

Evaluations are 400 games (±2.5 points), so single numbers are noise and
only means count. Full record and error bars in `RESULTS.md`.

That champion was trained before we established that the server discloses
no opponent deck, so the current deployable line is being retrained from
scratch with classification (`cls_scratch`). See `RESULTS.md` → In flight.

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
saved actors were trained against and they will not transfer. Upstream
advises pinning the simulation fingerprint alongside trained weights;
ours is in `engine-0.7.0/PIN.txt`.

## Train

```bash
cd penta-bot
PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python aac_torch_par.py \
  --native --native-threads 6 --belief --hidden 256 \
  --games 200000 --gate-every 3000 --gate-games 400 \
  --log myrun.log --save-prefix myrun
```

One process saturates around 3 cores, so **fill a big machine with
several concurrent runs, not one wide one** — see `RESULTS.md` §4c.

## Watch

```bash
cd penta-bot && python3 monitor.py     # -> http://localhost:8899
```

Reads the trainers' own logs. No dependencies, no build step.

## Play on the server

`hosted_bot.py` registers with the bot registry, heartbeats to stay
present, and plays whatever games it is invited to. It is meant to run
continuously so the bot is always available.

```bash
cd penta-bot
PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python hosted_bot.py \
  --server https://penta.lacker.workers.dev --deck Sligh
```

See `penta-bot/deploy/` for running it as a supervised service.

## Check

```bash
# native rows must stay bit-identical to the Python reference path
PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python aac_lockstep.py \
    --episodes 3 --hidden 256 --belief --actor <actor>.npz
```
