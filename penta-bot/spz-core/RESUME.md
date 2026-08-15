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

## NEXT once row-lockstep passes
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
cd spz-core && cargo build --release && cp target/release/libspz_core.dylib ../spz_core.so
