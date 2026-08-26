# penta-bot

A self-play bot for [lacker/penta](https://github.com/lacker/penta), a
deterministic headless Old School 93/94 Magic simulator. The goal is to
play on the public server at `penta.lacker.workers.dev`.

No handcrafted play knowledge is used anywhere in training. The bot learns
from self-play only.

## Where it stands

Win rate is against the engine's built-in bot, alternating seats, rotating
all 14 opponent decks. **50% is parity — the number IS our head-to-head win
rate, so anything below 50% is losing.**

| net | protocol | 32 sims | 128 sims |
|---|---|---|---|
| `checkpoints/deploy_v1` | 22 | **54.3%** | **61.0%** |
| `checkpoints/deploy_p29` | 29 | 43.3% (16 sims) | not measured |

The strong net plays protocol 22. **The public server requires protocol 29
and refuses to register anything else**, so the deployable net is the weaker
one. Closing that gap is the open problem.

## What it is

* **Engine** — `vendor/penta`, built into `spz-core`. Carries four local
  patches, each marked `SPZ VENDOR PATCH`.
* **`spz-core/`** — Rust: features, ISMCTS search, self-play generation,
  gating. Exposed to Python through PyO3.
* **`az_train.py`** — the AlphaZero loop. Generation in Rust, learning in
  PyTorch.
* **`hosted_policy.py` / `hosted_bot.py`** — the server daemon.
* **`playout_log.py`** — replays games and flags rule-level misplays.
* **`monitor.py`** — training dashboard on :8899.

## Running it

```sh
# protocol-29 catalog (feature layout is pinned to 128 definitions)
PENTA_ENGINE_DIR=engine-p29 python3 make_pinned_catalog.py

# train
PENTA_ENGINE_DIR=engine-p29 PENTA_CATALOG=pinned-catalog-p29.json \
AZ_GAME_SECS=45 AZ_GATE_SECS=300 \
python3 az_train.py --iters 16 --games 48 --threads 14 \
    --gate-every 20 --gate-games 120 --save-prefix run

# watch what it actually does
PENTA_ENGINE_DIR=engine-p29 python3 playout_log.py --net checkpoints/deploy_p29
```

`RESULTS.md` is the findings record. `ROADMAP.md` is what to do next.
`AGENTS.md` is how to work on this without repeating our mistakes.
