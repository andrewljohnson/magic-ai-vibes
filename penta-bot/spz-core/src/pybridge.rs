//! PyO3 boundary. Lockstep verification first: `lockstep_trace` plays a
//! deterministic game and returns (observation JSON, native features)
//! pairs so the Python reference extractor can be compared bit-for-bit
//! on identical states.

use pyo3::prelude::*;

use crate::extract::features;
use crate::net::Mlp;
use crate::tables::Tables;

#[pyfunction]
#[allow(clippy::needless_pass_by_value)]
fn lockstep_trace(
    catalog_json: String,
    d1: String,
    d2: String,
    seed: u64,
    max_decisions: usize,
) -> PyResult<Vec<(String, Vec<f32>)>> {
    let tables = Tables::load(&catalog_json)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let mut game = penta::protocol::BotGame::new(
        &d1, &d2, penta::protocol::Opponent::Handcrafted,
        penta::PlayerId::Two, seed,
    )
    .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let mut out = Vec::new();
    let mut n = 0;
    while game.result().is_none() && n < max_decisions {
        let Some(seat) = game.decision_seat() else { break };
        let json = game.observe_json(seat);
        let obs = game.core_game().observe(seat);
        let pregame = game.core_game().in_pregame();
        out.push((json, features(&obs, pregame, &tables)));
        // advance deterministically: first legal action
        game.act(0)
            .map_err(pyo3::exceptions::PyValueError::new_err)?;
        n += 1;
    }
    Ok(out)
}

#[pyfunction]
fn net_value(weights_path: String, x: Vec<f32>) -> PyResult<f64> {
    let net = Mlp::load(&weights_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    Ok(net.value(&x))
}

#[pymodule]
fn spz_core(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(lockstep_trace, m)?)?;
    m.add_function(wrap_pyfunction!(net_value, m)?)?;
    Ok(())
}
