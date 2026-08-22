# spz-core resume state (throughput campaign -> native training runner)

[x] extractor lockstep 0.00 ; net 5.55e-16 ; prune ported
[x] FULL policy lockstep 100% (1210/1210) -- evaluate-all + argmax fixes
[x] throughput: native 4.6x/core, ~3-5x @8 cores vs Python
[x] native row-emitting runner (det_runner.rs) + stream_rows PyO3 bulk entry
[x] shared splitmix64 PRNG (prng.rs) mirrored in Python (det_shared.py);
    prng + shuffle verified bit-identical
[x] fingerprint unified: rebuilt engine-0.7.0/penta.so FROM vendor/penta
    (with the accessor patch) so the crate and the Python binding share
    sha256-fca6... -- from_observation cross-accepts, lockstep is possible
[x] BUG FOUND+FIXED: native used typed obs.legal_actions.len() for the
    forced/single-action check but indexes via protocol_actions (a
    targeted spell = 1 typed action -> N protocol entries); fixed to
    protocol_actions().len() in policy.choose + det_runner. Was the
    2x-rows trajectory divergence.
[~] ROW LOCKSTEP: re-measuring after the fix (scratchpad/rowlock.log).
    GATE: native stream_rows == Python det_shared rows bit-for-bit
    (feature + target) on identical seeds before any native training row
    counts.

## AAC NATIVE SELF-PLAY (2026-08-21) -- the current work

[x] `aac.rs`: native AAC trajectory runner. Forks with BotGame::clone,
    observes typed, featurizes with extract::features (belief block was
    already ported), scores with an AAC `Actor` (afterstate scorer, tanh
    hidden, NO sigmoid -- net::Mlp's squash belongs to the determinized
    value net), softmax + splitmix64 sampling, emits the exact record
    aac_torch's compute_gae / ppo_update_fast consume.
[x] Ported for the honest actor: classify_deck + belief_deck_context (our
    deck known, opponent's classified from REVEALED cards only), and
    `DeckBook` -- classify_deck breaks ties by decklist FILE order, which
    a HashMap cannot reproduce, hence serde_json preserve_order.
[x] pybridge: `aac_stream_episodes` (one call per PPO round, games across
    OS threads, GIL released, flat buffers back) and `aac_gate`.
[x] ROW LOCKSTEP PASSES: candidate features, privileged rows, shaped
    rewards and record/result structure BIT-EQUAL over 569 decisions /
    5202 candidates vs the Python path. Logits agree to 1.4e-13 (numpy
    uses blocked BLAS summation; native accumulates straight through).
    Run: `.venv-torch/bin/python aac_lockstep.py --belief --hidden 256`
[x] Throughput: 7.8 g/s end-to-end in the trainer vs the Python pool's
    ~0.9 g/s ceiling.
[x] The "native engine hang" was WIDE DECISIONS, not a stall: 21 of 240
    gate games hold a decision offering >64 legal actions (max 538) and
    the expansion is linear in that. 240 games = 47.8s capped at 64 vs
    not finishing in 13 min uncapped. `max_actions` caps it; over the cap
    a decision is played greedily from a prefix and emits NO row.
[x] Actor forward batched over a decision's candidates -- one pass over
    the 2.2 MB f64 w1 per DECISION instead of per candidate. Bit-exact
    (same per-candidate accumulation order). 0.80 -> 0.24 s/game.

## OPEN: per-process scaling
One process saturates near 8 threads (8.5 g/s); FOUR processes at 8
threads each reach ~26 g/s aggregate on the same box. So ~3x remains and
the limit is per-process, not hardware. mimalloc moved it 8.0 -> 8.5, so
allocator arenas are not the cause. Next suspects: the per-candidate
feature Vec and full BotGame::clone, and cache pressure from the f64
weight matrix (f32 weights would halve it, at the cost of the exact-f64
lockstep). Workaround today: shard across 2-4 trainer processes.

## NEXT (determinized runner -- older track, unchanged)
1. end-to-end trainer integration: a --native-rows mode in trainer.py
   that calls spz_core.stream_rows per round (specs from matchup/league/
   learner_seat scheduling), reshapes into the replay ring, and runs the
   existing SGD/promotion. Python owns SGD/ring/league/promotion; Rust
   only plays + emits. Batched (one call/round), no per-ply crossing.
2. end-to-end trainer games/s benchmark (native-rows vs 8-worker python
   det, K=2) -> confirm the multiplier survives integration.
3. relaunch determinized curve on the native runner: det2 prefix,
   evaluate-all, K=2, bar 31.6%/400 LCB 26.9, hosted-path confirm-400,
   SPZ-redeploy hook, ~hourly chunks, detached, telemetry, early-stop.
   This is also the clean evaluate-all baseline.

## Scope notes / honest gaps
- native runner is GREEDY (epsilon 0) determinized self-play + handcrafted
  league. Epsilon exploration + frozen-snapshot league are NOT yet in the
  native runner (Python det path has them). First native curve runs
  greedy + handcrafted-league; add exploration/frozen next if the curve
  needs more data diversity.
- vendor/penta carries the engine (ac6cd4d) + the into_core_game/core_game
  accessor patch (SPZ VENDOR PATCH) -- candidate upstream PR.

## Reload
cd spz-core && cargo build --release
cp target/release/libspz_core.so ../spz_core.so   # .dylib on macOS
