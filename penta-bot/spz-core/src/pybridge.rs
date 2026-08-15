//! PyO3 boundary. Lockstep verification first: `lockstep_trace` plays a
//! deterministic game and returns (observation JSON, native features)
//! pairs so the Python reference extractor can be compared bit-for-bit
//! on identical states.

use pyo3::prelude::*;

use crate::extract::features;
use crate::net::Mlp;
use crate::policy::{Policy, SearchConfig};
use crate::tables::Tables;

fn build_policy(catalog: &str, value_path: &str, head_path: &str,
                weight: f64, top_k: usize, playouts: usize,
                budget: usize, pme: usize, max_eval: usize)
                -> Result<Policy, String> {
    Ok(Policy {
        tables: Tables::load(catalog)?,
        value: Mlp::load(value_path)?,
        head: Mlp::load(head_path)?,
        weight,
        search: SearchConfig { top_k, playouts, budget,
                               playout_max_eval: pme },
        max_eval,
    })
}

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

/// Native greedy self-play throughput: play `games` full mirror games
/// with the native policy on both seats, return games/sec.
#[pyfunction]
#[allow(clippy::too_many_arguments)]
fn bench_native_selfplay(
    catalog_json: String, value_path: String, head_path: String,
    weight: f64, top_k: usize, budget: usize, pme: usize,
    d1: String, d2: String, seed_base: u64, games: usize,
) -> PyResult<(f64, usize)> {
    let policy = build_policy(&catalog_json, &value_path, &head_path,
                             weight, top_k, 1, budget, pme, 16)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let t0 = std::time::Instant::now();
    let mut decisions = 0usize;
    for gi in 0..games {
        let mut game = penta::protocol::BotGame::new(
            &d1, &d2, penta::protocol::Opponent::External,
            penta::PlayerId::Two, seed_base + gi as u64,
        ).map_err(pyo3::exceptions::PyValueError::new_err)?
         .into_core_game();
        let mut n = 0;
        while game.result().is_none() && n < 600 {
            let idx = policy.choose(&game);
            let seat = game.decision_player().unwrap();
            let obs = game.observe(seat);
            let actions = penta::protocol::protocol_actions(&obs);
            if game.apply(seat, actions[idx].clone()).is_err() { break; }
            n += 1;
            decisions += 1;
        }
    }
    let secs = t0.elapsed().as_secs_f64();
    Ok((games as f64 / secs, decisions))
}

/// One native greedy choice on a game replayed to `history` from
/// (d1,d2,seed) external -- for lockstep vs Python trainer.choose.
#[pyfunction]
#[allow(clippy::too_many_arguments)]
fn choose_at(
    catalog_json: String, value_path: String, head_path: String,
    weight: f64, top_k: usize, budget: usize, pme: usize,
    d1: String, d2: String, seed: u64, history: Vec<usize>,
) -> PyResult<usize> {
    let policy = build_policy(&catalog_json, &value_path, &head_path,
                             weight, top_k, 1, budget, pme, 16)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let mut game = penta::protocol::BotGame::new(
        &d1, &d2, penta::protocol::Opponent::External,
        penta::PlayerId::Two, seed,
    ).map_err(pyo3::exceptions::PyValueError::new_err)?
     .into_core_game();
    for idx in history {
        let seat = game.decision_player().unwrap();
        let obs = game.observe(seat);
        let actions = penta::protocol::protocol_actions(&obs);
        game.apply(seat, actions[idx].clone())
            .map_err(|e| pyo3::exceptions::PyValueError::new_err(e.to_string()))?;
    }
    Ok(policy.choose(&game))
}

#[pymodule]
fn spz_core(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(lockstep_trace, m)?)?;
    m.add_function(wrap_pyfunction!(net_value, m)?)?;
    m.add_function(wrap_pyfunction!(bench_native_selfplay, m)?)?;
    m.add_function(wrap_pyfunction!(choose_at, m)?)?;
    Ok(())
}
