# spz-core resume state (throughput campaign)

Milestones:
[x] profile pie (observe 38% / act 22% / json 18% / orch 16% / net 4% / feat 2%)
[x] crate skeleton compiles vs vendored 0.7.0 engine
[x] extractor lockstep vs Python: 0.00e+00 over 184 decisions
[x] net forward lockstep: 5.55e-16 over 120 states
[~] policy lockstep: myopic screen 95.6% (search off); FULL playout 38.3% -- NOT training-ready
[ ] games/s benchmark: native 0.06 g/s vs Python 0.026 single-core, matched topk4/pme16 = 2.3x/core (+ GIL-free scaling)
[ ] training integration -- BLOCKED on playout lockstep

## Why full-policy lockstep is the hard gate (38.3%)
The myopic screen is essentially correct; the divergence is the PLAYOUT
rollout, which is deterministic-but-chaotic: any difference cascades over
~120 plies. Exact remaining causes, in priority order:
1. dominance prune is STUBBED in policy.rs (dominance_keep returns all) --
   port land-drop + no-upside-vs-pass from trainer.py.
2. max_eval / playout_max_eval SAMPLING: Python rng.sample(actions, budget)
   when actions>budget; Rust takes first `limit`. Must replicate the exact
   random.Random sample sequence (or gate lockstep to states under budget).
3. numpy argmax tie = FIRST max (fixed for the two argmax sites; the
   playout greedy 1-ply inner loop also uses strict > = first-max, ok).
4. pregame threading: obs_pregame() is stubbed False; thread real pregame.
5. order.sort_by (top-k membership) is stable-descending in Rust; numpy
   argsort()[::-1] differs on ties -- match with a stable first-k.

## Verdict for the user
Native extractor+net are BIT-EXACT and reusable now. The search assembly
needs exact-replication work (items 1-5) before ANY training row from the
crate counts -- the non-negotiable. 2.3x/core + clean multicore is the
proven headroom; not yet realizable until playout parity lands.

## Reload
cd spz-core && cargo build --release && cp target/release/libspz_core.dylib ../spz_core.so
Weights: python3 export_weights.py <net>.npz <net>.spzw
