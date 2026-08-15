# spz-core resume state (throughput campaign)

Milestones: [x] profile pie  [x] crate skeleton compiles vs 0.7.0 engine
[x] extractor lockstep vs Python (0.00 diff, 184 decisions)
[ ] full policy lockstep (net+blend+prune+playout choose identical index)
[ ] games/s benchmark Python vs Rust, same seeds K=2
[ ] training integration

NEXT: export the value net + dagger1 head to the flat binary net.rs reads
(export_weights.py: u64 hidden, u64 inputs, then w1/b1/w2/b2 f64 LE),
then port trainer.choose (dominance_prune + blend + playout_value) into
spz-core and lockstep its chosen index vs Python trainer.choose on
identical worlds. Build from inside spz-core/ (cargo config there sets
the macOS dynamic_lookup link flags). Reload: cp target/release/
libspz_core.dylib ../spz_core.so.
