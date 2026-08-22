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

- `aac_torch_par.py` — THE trainer. **This is the file to run**, now with
  `--native`: episode generation moves to spz-core (see "NATIVE SELF-PLAY
  LOOP" below) while PPO/GAE/critic/gating stay here. Without `--native`
  it is the original N-worker fork Pool, watchdog and all.
- `aac_native.py` — adapter for the native runner (weights out, records in).
- `aac_lockstep.py` — the check that certifies native rows == Python rows.
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

The engine binding pins `abi3-py313`, so it needs Python **3.13**. Ubuntu
24.04 ships 3.12; `uv` installs 3.13 without touching the system python
(and without patching the vendored engine's build config, which would
make the "pinned engine" claim a lie).

```bash
# 1. Rust engine -> Linux penta.so
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
source "$HOME/.cargo/env"
cd penta-bot/vendor/penta/bindings/penta-py
cargo build --release            # -> target/release/libpenta.so
cp target/release/libpenta.so ../../../../engine-0.7.0/penta.so   # overwrite mac binary
cd ../../../../                  # back to penta-bot
# NOTE: the rebuilt .so is a LINUX binary. Leave it out of commits --
# engine-0.7.0/penta.so is tracked as the macOS-ARM build.

# 2. Python 3.13 + torch
curl -LsSf https://astral.sh/uv/install.sh | sh
export PATH="$HOME/.local/bin:$PATH"
uv python install 3.13
uv venv --python 3.13 .venv-torch
uv pip install --python .venv-torch/bin/python torch numpy

# 3. spz-core (the native self-play runner)
cd spz-core && cargo build --release
cp target/release/libspz_core.so ../spz_core.so && cd ..

# 4. Sanity: must print 1081 (825 + 2*128 defs) -- confirms the Linux engine
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

- **"Native engine hangs" — SOLVED, and they were never hangs.** They were
  decisions with enormous legal-action lists (up to 538 vs a median 17),
  where the afterstate expansion costs a fork + a feature extraction per
  action. See "What the native engine hang actually was" below. The
  `--native` runner caps the expansion; the fork-pool path still needs
  `--per-result-timeout`.
- **BLAS oversubscription** was fixed by pinning 1 thread/worker (baked into
  the script's env at the top). Do not remove.
- Memory: use `--workers = physical_cores - 2`. fork start method loads torch
  ONCE (shared COW); workers use numpy+engine only.

## NATIVE SELF-PLAY LOOP -- DONE (2026-08-21)

Step #1 below is BUILT and validated. Generation runs in Rust; the learner
is untouched (Python still owns PPO, GAE, the critic, gating, saving).

Files: `spz-core/src/aac.rs` (runner + AAC actor + honest deck
classification), `pybridge.rs::aac_stream_episodes` / `aac_gate`,
`aac_native.py` (adapter), `aac_lockstep.py` (the certification),
`aac_torch_par.py --native`.

**Row lockstep PASSES.** `aac_lockstep.py` replays a native episode's
chosen actions through the original Python path and compares. Candidate
afterstate features, privileged critic rows, shaped rewards, and the
record/result structure are BIT-EQUAL over 569 decisions / 5202
candidates. Actor logits agree to 1.4e-13 -- not bit-equal by design,
because numpy hands the dot product to BLAS (blocked, reordered
summation) while the native scorer accumulates straight through in f64.
4/569 argmax disagreements, all near-exact ties.

```bash
PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python aac_lockstep.py \
    --episodes 4 --hidden 256 --belief --actor aac_par_belief_best.npz
```

**The gate agrees too.** Same 100 games (same seats, decks, seeds), 45%
champion: native **39.0%** in 4.3s vs Python **40.0%** in 49.1s -- one
game apart, 11.3x faster wall. The one-game delta comes from argmax
tie-breaks (the Python gate's forward is torch f32, the native one f64)
and from the wide-decision cap; both only move decisions where candidates
are separated by less than an ulp.

**Measured throughput** (32-core Linux box, 1248-game run, belief 1081,
hidden 256, 30 threads): **7.8 g/s generation, 6.1 g/s including a
200-game gate every 600 games** -- against the Python fork pool's ~0.9
g/s ceiling. ~5% of episodes still hit the 600-decision cap and are
dropped by compute_gae, exactly as before.

### Run the native trainer

```bash
PENTA_ENGINE_DIR=engine-0.7.0 nohup .venv-torch/bin/python aac_torch_par.py \
  --native --native-threads <CORES-2> \
  --belief --games 100000 --round-episodes 64 --hidden 256 \
  --actor-lr 1e-3 --critic-lr 2e-3 --ppo-epochs 4 --entropy-beta 0.01 \
  --gae-lambda 0.95 \
  --init-actor aac_par_belief_actor.npz \
  --gate-every 3000 --gate-games 400 --seed 1 \
  --log belief_native.log --save-prefix aac_par_belief_native \
  > belief_native.out 2>&1 &
```
`--gate-games` can now be 400 rather than 120: the gate is native and
threaded, so a large-sample gate costs less than the round it validates.
`--python-gate` forces the old Python gate for spot-checking.

### What the "native engine hang" actually was

Not a hang. The afterstate expansion costs one game fork + one feature
extraction PER LEGAL ACTION. The median decision offers 17 candidates,
but 21 of 240 gate games contain a decision offering more than 64, up to
**538** -- roughly 30x a normal decision, several times per game. The old
fork-pool worker was not stuck in native code, it was expanding an
enormous action list, and `--per-result-timeout` killed it at 25s. Same
240 games, 8 threads: **47.8s with `--native-max-actions 64`, versus not
finishing in 13 minutes uncapped.**

Above the cap the runner scores a prefix, plays the best of it, and emits
NO training row -- a softmax over a truncated candidate set is not the
distribution the actor sampled from, so it must not become a PPO target.
`--native-max-actions 0` restores full expansion (what lockstep runs).

## NEXT STEPS (priority order)

**#1: scale the belief run.** This was #2 and is now unblocked. 100k+
games (it peaked 45% at only 6k and was still oscillating up; the belief
columns start at zero and need many games to mature -- open question
whether it climbs toward the old C++ bot's 57.7%). Then a bigger net
(512; the width ladder gave ~+5% per 2x, diminishing but real).

**#2: the remaining throughput lever is per-process scaling.** Native
generation measured **7.8 g/s** end-to-end in the trainer against the
Python pool's ~0.9 g/s ceiling. But thread scaling inside ONE process
saturates around 8 threads, while FOUR processes at 8 threads each hit
~26 g/s aggregate on the same box -- so ~3x is still on the table and the
limit is per-process, not hardware. mimalloc was tried and moved it only
8.0 -> 8.5 g/s, so allocator arenas are not it; the next suspects are the
remaining per-candidate allocation (a fresh feature Vec and a full
`BotGame::clone` per candidate) and cache pressure from the 2.2 MB f64
weight matrix. Cheap workaround available today: run 2-4 trainer
processes and average their actors, or shard the games.

**#3:** dedupe equivalent afterstates before scoring (many of those wide
action lists are permutations that settle to identical afterstates --
would cut the cap's cost AND remove its approximation), reward/lambda
sweeps, larger gate samples to cut variance.

## REPO BACKLOG (Andrew, 2026-08-21) -- do in this order

The repo is being narrowed to ONE purpose: training bots that play
lacker's penta engine, plus monitor screens for the runs.

1. Merge the bot work to master. Note the current branch
   (`rust-native-selfplay`) descends from `ismcts-handcrafted-opponent`,
   NOT from master -- master is a different lineage, so this is a real
   merge decision, not a fast-forward.
2. **Write up the experiment record FIRST.** Results have to outlive the
   code that produced them.
3. Then delete the C++ code and the web-play code.
4. Then tidy what remains.

Step 2 before step 3 is the whole point: once the old lineages are gone,
so is the context for their numbers. Confirm what counts as "web play
code" before deleting -- the hosted path may still be needed to deploy a
trained actor.

See memory: `aac-parallel-throughput.md`, `penta-bot-arc.md`,
`repo-scope-penta-only.md`.
