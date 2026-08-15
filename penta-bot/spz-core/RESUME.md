# spz-core resume state (throughput campaign)

[x] profile pie; [x] crate compiles; [x] extractor lockstep 0.00e+00 (184)
[x] net lockstep 5.55e-16 (120)
[x] dominance prune ported (land-drop + no-upside-vs-pass, fail-closed guards)
[~] policy lockstep:
    - SCREEN + PRUNE (search off): 99.3% (440/443). The 3 misses are all
      >16-action sets (35/18/17) where Python rng.sample subsamples to
      max_eval=16. This is the ONLY screen/prune divergence = the RNG trap,
      now isolated + quantified (~0.7% of decisions).
    - FULL PLAYOUT (search on): 50.8%. Boundary bug fixed (active-player,
      not priority-holder). A SECOND structural playout bug REMAINS:
      disagreements are systematic small-set (n=3, turn 2, py=0 vs rust=2),
      NOT sampling (no-sample config was also ~48%). NOT the prune (99.3%).
[ ] games/s multicore benchmark -- BLOCKED on playout parity
[ ] training integration -- BLOCKED on playout parity

## Two decisions to surface (coordinator)
1. RNG (screen sampling on >16-action sets): reproducing CPython
   random.sample bit-for-bit is the intractable trap. Clean shared-algorithm
   fix: DROP the max_eval/playout_max_eval cap in the GREEDY path and
   evaluate ALL candidates on both sides -- native afterstates are ~free,
   it is strictly more thorough, fully deterministic, removes the RNG.
   COST: changes the deployed policy (screens all vs 16) -> re-gate. Needs
   sign-off.
2. Playout second bug: localize with a per-candidate playout-value diff.
   NEXT ACTION: add a Rust `playout_at(action_index)->f64` hook, pick one
   n=3 disagreeing state (seed 6000000, the turn-2 states), compare Rust
   playout value vs Python playout_value for each of the 3 candidates.
   The one that differs points at the bug (candidate afterstate handoff,
   greedy tie-break, or terminal-perspective sign).

## Reload
cd spz-core && cargo build --release && cp target/release/libspz_core.dylib ../spz_core.so
