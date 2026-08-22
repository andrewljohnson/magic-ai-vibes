# AGENTS.md

## Project goal

Train the strongest bot for lacker's penta engine and run it on
lacker's server. Nothing else. There is no engine of ours to maintain, no
arena, no site.

The deployed actor sees only a redacted observation — its own hand, the
public zones, the opponent's hand *size*. It never sees the opponent's
hand contents or either library's order, and the server does not disclose
which archetype the opponent is playing.

Read `RESULTS.md` before proposing an experiment; several obvious ideas
have already been tried and measured, including two that failed.

## Layout

```
penta-bot/
  aac_torch_par.py     the trainer (--native is the fast path)
  aac_torch.py         MLP, GAE, PPO reference
  aac_native.py        adapter to the native runner
  aac_lockstep.py      proves native rows == Python rows
  monitor.py           local dashboard, http://localhost:8899
  extractor.py         the 1081-feature schema (Python side)
  hosted_bot.py        plays on lacker's server
  spz-core/            Rust: native self-play, features, ISMCTS
  vendor/penta/        lacker's engine, pinned
```

## Build and verification

```sh
# engine binding (per platform) -> engine-0.7.0/penta.so
cd penta-bot/vendor/penta/bindings/penta-py && cargo build --release

# native runner
cd penta-bot/spz-core && cargo build --release
cp target/release/libspz_core.so ../spz_core.so

# must print 1081, or saved actors will not transfer
PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python -c \
  "from extractor import Extractor; print(Extractor(version=2, belief=True).size)"

# the gate that matters: native rows bit-identical to the Python path
PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python aac_lockstep.py \
    --episodes 3 --hidden 256 --belief --actor <actor>.npz
```

## Rules that keep results credible

- **A single gate is not a result.** Gates are 400 games: ±2.5 points.
  Compare means over many gates. With several runs going, the best gate
  across them is a max-of-noise statistic.
- **Never let the actor read hidden information.** The critic is
  privileged by design and is train-time only — it may only be used at
  play time inside a determinized world, where the hypothesis supplies
  both hands by construction.
- **Any change to the native runner must keep `aac_lockstep.py` passing.**
  Features, privileged rows and rewards are held to bit-equality; logits
  to a tolerance (numpy uses BLAS's reordered summation).
- **Report negative results.** Two are already recorded in `RESULTS.md`
  and both cost real time to establish.

## Machine

One trainer process saturates around 3 cores regardless of thread count.
Fill a big box with **several concurrent runs**, not one wide one.
