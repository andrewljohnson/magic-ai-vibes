# Penta strong-bot — restart handoff (2026-08-21)

Self-play RL bot for lacker's penta MTG engine. Goal: strongest HONEST bot
(actor sees only redacted observations) vs the built-in handcrafted bot; beat
the 31.6% honest baseline and get onto a demonstrable path past 50%.

## Current results (honest gate: actor argmax, Sligh vs handcrafted spread)

| Stage | Win rate | Artifact |
|---|---|---|
| Handcrafted baseline to beat | 31.6% | — |
| 128-net AAC (prior ceiling) | ~35% | — |
| 256-net AAC (400-game confirmed) | **39.8%** | `aac_par_h256d_best.npz` |
| 256-net continuation | **44.0%** | `aac_par_44pct.npz` (= h256e_best) |
| **256-net + belief features (CHAMPION)** | **45.0%** | `aac_par_belief_best.npz` |

The curve NEVER plateaued — each "more net / more games / +belief" step kept
climbing. This is the "on a path to 50%" evidence.

## What the approach is (the winning recipe)

Asymmetric Actor-Critic (AAC), PyTorch PPO + GAE:
- **Actor** = afterstate scorer, observation-only (HONEST). Redacted features.
- **Critic** = PRIVILEGED (sees BOTH seats' redacted obs concatenated = both
  hands); used only at train time. Only the honest actor deploys.
- PPO clipped surrogate, GAE(λ=0.95, γ=1.0), advantage norm, entropy 0.01,
  grad clip 1.0, Adam lr 1e-3 (actor) / 2e-3 (critic), 4 PPO epochs.
- Tempo reward shaping (K_LIFE opp-life-lost, K_POWER own-board, K_IDLE pass
  penalty); terminal z (win 1 / loss 0 / draw .5) dominates.
- Curriculum: ~50% self-play, ~50% vs handcrafted.
- **Belief (hidden-pool) features** (the latest lever, from the old C++ 57.7%
  bot): `Extractor(version=2, belief=True)` = 1081-dim = 825 + 2*128, adding
  per-card UNSEEN-POOL counts for [me, opp] = decklist minus visible cards.
  HONEST: our deck known (we pilot it), opp deck CLASSIFIED from its revealed
  cards (never its hidden hand). Wired via `belief_deck_context()`.

## Files

- `aac_torch_par.py` — THE trainer (parallel, N-worker fork Pool). Has all the
  throughput fixes + `--belief` + grow-init warm-start + best-actor saving +
  native-hang watchdog. **This is the file to run.**
- `aac_torch.py` — single-process reference (MLP, decide, compute_gae,
  ppo_update, gate). Imported by the parallel trainer.
- `aac_selfplay.py` — env helpers (_priv_features, tempo constants, MAX_DECISIONS).
- `extractor.py` — features; belief block already implemented + tested.
- `hosted_policy.py` — `load_decklists`, `belief_deck_context`, classify.
- `builtin-decklists.json`, `engine-0.7.0/penta.so` — runtime deps (tracked).
- Best actors: `aac_par_belief_best.npz` (45%, belief 1081),
  `aac_par_belief_actor.npz` (latest belief, ~44%, for RESUME),
  `aac_par_44pct.npz` (44%, plain 825).

## SETUP ON THE NEW MACHINE

All this work lives on git branch **`ismcts-handcrafted-opponent`** (NOT main —
main is a different lineage). On the new box:

```bash
git clone https://github.com/andrewljohnson/magic-ai-vibes.git
cd magic-ai-vibes
git checkout ismcts-handcrafted-opponent
```

The committed `engine-0.7.0/penta.so` is a macOS-ARM binary and will NOT load
on Linux. The engine SOURCE is vendored (Rust), so rebuild it — this reproduces
the pinned engine 0.7.0 / protocol 22 (`lacker/penta ac6cd4d`). Needs Python
3.13 and a current Rust (edition 2024 -> rustc >= 1.85).

```bash
# 1. Rust engine -> Linux penta.so
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
source "$HOME/.cargo/env"
cd penta-bot/vendor/penta/bindings/penta-py
cargo build --release            # -> target/release/libpenta.so
cp target/release/libpenta.so ../../../../engine-0.7.0/penta.so   # overwrite mac binary
cd ../../../../                  # back to penta-bot

# 2. Python + torch (CUDA build auto-selected on a GPU box)
python3 -m venv --system-site-packages .venv-torch
.venv-torch/bin/pip install torch numpy

# 3. Sanity: must print 1081 (825 + 2*128 defs) -- confirms the Linux engine
#    reproduces the same 128-def catalog, so the .npz actors transfer as-is.
PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python -c \
  "from extractor import Extractor; print(Extractor(version=2, belief=True).size)"
```
If step 3 prints anything other than 1081, the catalog differs from what the
actors were trained on -> retrain from scratch (fine; the recipe is the value,
not the weights -- see "Early-days mindset" memory).

NOTE ON GPU: the nets are tiny (1081->256->1); a GPU barely helps them. The
throughput wall is CPU self-play generation through the engine, which scales
with CPU CORES (`--workers`), not GPU. The GPU only becomes the lever if we go
to a much bigger net or move generation into the Rust native loop (spz-core).

## RESTART TRAINING (pick up where we left off)

Resume the belief run from the latest belief actor (climbing, peaked 45%):

```bash
cd penta-bot
PENTA_ENGINE_DIR=engine-0.7.0 nohup .venv-torch/bin/python aac_torch_par.py \
  --belief --games 60000 --workers <CORES-2> --round-episodes 24 --hidden 256 \
  --actor-lr 1e-3 --critic-lr 2e-3 --ppo-epochs 4 --entropy-beta 0.01 \
  --gae-lambda 0.95 --per-result-timeout 25 \
  --init-actor aac_par_belief_actor.npz \
  --gate-every 3000 --gate-games 200 --seed 1 \
  --log belief2.log --save-prefix aac_par_belief2 > belief2.out 2>&1 &
```
Note: `--init-actor` with a 1081-dim belief npz loads directly; a 825-dim plain
npz is grown (belief cols zero-padded) automatically. Watch: `grep "GATE @\|new best" belief2.log`.

Confirm any best actor on a large sample (edit prefix/size/belief in the snippet):
```bash
PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python -c "
import numpy as np, torch, aac_torch as AT
from extractor import Extractor; from hosted_policy import load_decklists
from aac_torch_par import gate_belief
ex=Extractor(version=2, belief=True); import extractor
from extractor import import_penta; penta=import_penta()
a=AT.MLP(ex.size,256); d=np.load('aac_par_belief_best.npz')
a.load_state_dict({k:torch.as_tensor(d[k]) for k in d.files})
print('400-game gate:', 100*gate_belief(a,ex,penta,load_decklists(),'Sligh',400,500000))
"
```

## KNOWN ISSUES / THROUGHPUT

- **Native engine hangs**: a worker occasionally spins/blocks in native code
  (unbounded by MAX_DECISIONS). The watchdog (`--per-result-timeout`) kills +
  rebuilds the pool and keeps completed episodes. STRONGER actors hang MORE
  (~1/125 games at 44%+), dragging throughput to ~0.9 g/s. This is the current
  ceiling for strong-actor runs.
- **BLAS oversubscription** was fixed by pinning 1 thread/worker (baked into
  the script's env at the top). Do not remove.
- Memory: use `--workers = physical_cores - 2`. fork start method loads torch
  ONCE (shared COW); workers use numpy+engine only.

## NEXT STEPS (priority order — DECIDED 2026-08-21)

**#1 (USER-CHOSEN): Rust the self-play hot loop in `spz-core`.** The Python
loop is capped at ~0.9 g/s by native engine HANGS (a worker spins/blocks in
engine code; the watchdog absorbs it but it worsens as the actor strengthens).
A native loop removes that tax and saturates all cores on the beefy Linux box.
spz-core already has the pieces:
  - `net.rs::Mlp` (load + forward) — reuse as the AAC ACTOR (afterstate scorer,
    same features->scalar shape). Add softmax-over-afterstates + PRNG sampling.
  - `extract.rs::features` — native feature extraction. ADD the belief block
    (unseen-pool = decklist - seen; port `_belief_block` from extractor.py /
    the old C++ `selfplay_zero.cpp`). Need both-seats extraction for the
    privileged critic.
  - `det_runner.rs::play_game` — native self-play runner; prior "--native-rows"
    work already feeds trajectories to the Python trainer via `pybridge.rs`.
  Concrete FIRST SLICE: emit AAC trajectories natively — per decision
  {candidate afterstate features, chosen idx, logp_old, privileged (both-seat)
  features, shaped reward, seat} + terminal result — and feed them to the
  EXISTING Python PPO+GAE update (`ppo_update_fast` in aac_torch_par.py) and
  gate. Keep the learner in torch (tiny, fast); only MOVE GENERATION to Rust.
  Validate: a native-generated batch must reproduce the Python gate curve
  (lockstep check — there's precedent: "Row-lockstep PASSES" in git log).
  Build: `cd penta-bot/spz-core && cargo build --release`; exposed via pybridge.

**#2: Once native generation runs, scale hard** — belief run 100k+ games (it
peaked 45% at only 6k, still oscillating up; belief cols start at zero and need
many games to mature — open question whether it climbs toward 57.7%), then
bigger net (512; width ladder gave ~+5%/2x, diminishing but real).

**#3:** reward/λ sweeps, larger gate samples to cut variance.

See memory: `aac-parallel-throughput.md`, `penta-bot-arc.md`.
