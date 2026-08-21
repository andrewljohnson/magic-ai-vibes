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

Requires macOS ARM (penta.so is a compiled binary; if the new box is a
different arch, rebuild/obtain a matching penta.so). Python 3.13.

```bash
cd penta-bot
python3 -m venv --system-site-packages .venv-torch
.venv-torch/bin/pip install torch numpy
# sanity:
PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python -c \
  "from extractor import Extractor; print(Extractor(version=2, belief=True).size)"  # -> 1081
```

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

## NEXT STEPS (priority order)

1. **Let the belief run go much longer** — it peaked 45% at only 6k games and
   was still oscillating up; belief cols started at zero and need many games to
   mature. On the faster box, run 100k+ games and watch for the climb toward
   the old C++ bot's 57.7%. If belief stays ~+1% by ~30k games, it's marginal
   in AAC and deprioritize.
2. **Rust the self-play hot loop** — the real throughput multiplier and the fix
   for the hang tax (async collection that doesn't stall a whole round; native
   actor forward). spz-core crate exists. This unblocks all bigger experiments.
3. **Bigger net** (512) once throughput allows — width ladder gave ~+5%/2x but
   diminishing.
4. Reward/λ sweeps (cheap tuning), larger gate samples to cut variance.

See memory: `aac-parallel-throughput.md`, `penta-bot-arc.md`.
