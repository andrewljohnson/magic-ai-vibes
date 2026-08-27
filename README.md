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
| `checkpoints/deploy_p29` | 29 | 50.8% | not measured |

The strong net plays protocol 22. **The public server requires protocol 29
and refuses to register anything else**, so the deployable net is the weaker
one.

**The open problem is that the loop does not climb.** Search beats our
policy — 55.8% ± 3.5 in a mirror match at 32 sims, measured in exactly the
configuration self-play trains on — but 180 rounds and ~8600 games produced
zero promotions and left the best net byte-identical to its warm start. The
teacher is good; the student does not learn from it. See ROADMAP.

A bot is deployed and playing on the public server at 512 sims per move.
Hosted play needs `SPZ_ACCEPT_FINGERPRINT` set to the server's advertised
`simulationFingerprint`; without it every observation is rejected and the
bot silently plays a 1-ply pick. See RESULTS.

**The protocol-22 numbers are historical.** `spz-core` now embeds the
protocol-29 engine, so they cannot be re-measured as the tree stands.
Restoring the p22 vendor contaminated the tree once; ROADMAP explains why
it is not worth redoing.

## Architecture

**Features (1081 floats, hand-built).** Per-definition counts for each zone
(hand, our battlefield, theirs, both graveyards) over the 128 legal card
definitions, then 35 aggregate scalars (life, board power/toughness,
untapped counts, attackers), cost buckets, and a belief block of unseen-pool
counts for both seats. The opponent's deck is *classified* from revealed
cards (76.6% accurate overall, 0% on turn one) — the server discloses no
decklist.

This is bag-of-cards counting. It has no per-instance state and cannot
express a pairwise creature matchup, which is the known cause of the combat
misplays in RESULTS.

**Policy head — factorised, 184-dim action encoding.**
`score(s,a) = w2·tanh(Ws·s + Wa·a + b1) + b2`. `Ws·s` is computed once per
search node and only the small per-action term repeats. This is what made
search tractable: scoring an afterstate instead costs a game clone plus a
full feature extraction per action, measured at 13ms each.

**Value head.** `s -> P(win)`, one hidden layer of 128, trained on game
outcomes with label smoothing, plus two auxiliary heads used only to shape
the trunk (predict the opponent's hand, predict fraction of game remaining).
Neither auxiliary head is exported.

**Search.** Single-observer ISMCTS. Each iteration samples a determinized
world from the belief, descends by PUCT with the policy head as prior and
the value head at the leaf, and the visit counts become the policy target.
The opponent is a fixed in-tree model, never branched.

**Scale.** Protocol 22 reached 61% after roughly 100k self-play games.
Protocol 29 has had roughly 34k and sits at 50.8% at 32 sims.

## What it is

* **Engine** — `vendor/penta`, built into `spz-core`. Carries eleven local
  patches, each marked `SPZ VENDOR PATCH`. Eight are additive accessors
  proposed upstream; three are the LOCAL-ONLY fingerprint acceptance that
  hosted play needs.
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

# host it on the public server (search needs the server's fingerprint)
FP=$(curl -s https://penta.lacker.workers.dev/_bots \
     | python3 -c "import json,sys;print(json.load(sys.stdin)['compatibility']['simulationFingerprint'])")
PENTA_ENGINE_DIR=engine-p29 PENTA_CATALOG=pinned-catalog-p29.json \
SPZ_ACCEPT_FINGERPRINT=$FP \
python3 hosted_bot.py --server https://penta.lacker.workers.dev \
    --engine-dir engine-p29 --az deploy_p29 --az-iters 512

# watch what it actually does
PENTA_ENGINE_DIR=engine-p29 python3 playout_log.py --net checkpoints/deploy_p29
```

To measure a checkpoint, call `spz_core.az_gate(catalog, value, policy,
iters, c_puct, decklists, specs, threads, max_decisions, classify)`. It
returns `(wins, draws, finished, capped, per_game_scores)`. **Sweeping
`iters` on a fixed net is the single most informative measurement here** --
it is how much search is worth over its own prior, and the loop climbs only
at that rate.

`RESULTS.md` is the findings record. `ROADMAP.md` is what to do next.
`AGENTS.md` is how to work on this without repeating our mistakes.
