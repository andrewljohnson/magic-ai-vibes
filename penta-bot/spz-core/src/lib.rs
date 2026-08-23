//! spz-core: the SPZ penta bot's inference stack, native. See Cargo.toml.

// See the mimalloc note in Cargo.toml: the native AAC runner's per-thread
// allocation churn hits glibc arena contention long before it saturates
// the cores. A cdylib's #[global_allocator] governs only this library's
// own allocations, so it does not disturb the host interpreter.
#[global_allocator]
static ALLOC: mimalloc::MiMalloc = mimalloc::MiMalloc;
pub mod aac;
pub mod az;
pub mod decks;
pub mod det_runner;
pub mod extract;
pub mod mcts;
pub mod mcts_runner;
pub mod net;
pub mod policy;
pub mod prng;
pub mod tables;
mod pybridge;
