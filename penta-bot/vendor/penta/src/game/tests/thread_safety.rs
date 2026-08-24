//! `Game` must stay `Send + Sync`, because the Python binding hands it to
//! pyo3 as a `#[pyclass]` and pyo3 asserts both.
//!
//! This assertion lives here rather than only in `bindings/penta-py` on
//! purpose. That crate is excluded from the workspace and builds against a
//! Python toolchain, so nothing in a normal `cargo test` compiles it, and a
//! field that quietly costs `Sync` -- a `Cow`, an `Rc`, a `Cell` -- reads as
//! a perfectly ordinary engine change right up until the bindings CI job
//! fails several minutes later. Failing here instead costs nothing: these
//! are compile-time bounds, and every agent already runs the engine tests.
//!
//! If this stops compiling, find the field that lost the bound rather than
//! deleting the assertion. `super::super::prospective_x` shows the shape of
//! the fix for interior mutability.

use crate::Game;
use crate::protocol::BotGame;

const fn assert_send_and_sync<T: Send + Sync>() {}

#[test]
fn the_engine_types_the_python_binding_exposes_stay_send_and_sync() {
    // `#[pyclass] struct Game { inner: BotGame }` in bindings/penta-py.
    const { assert_send_and_sync::<BotGame>() }
    const { assert_send_and_sync::<Game>() }
}
