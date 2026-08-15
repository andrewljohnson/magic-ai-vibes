//! PyO3 boundary. Lockstep verification first: `lockstep_trace` plays a
//! deterministic game and returns (observation JSON, native features)
//! pairs so the Python reference extractor can be compared bit-for-bit
//! on identical states.

use pyo3::prelude::*;

use crate::extract::features;
use crate::net::Mlp;
use crate::decks;
use crate::det_runner;
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

/// Rust refined playout value for one candidate at a replayed state.
#[pyfunction]
#[allow(clippy::too_many_arguments)]
fn playout_at(
    catalog_json: String, value_path: String, head_path: String,
    weight: f64, budget: usize, d1: String, d2: String, seed: u64,
    history: Vec<usize>, action_index: usize,
) -> PyResult<f64> {
    let policy = build_policy(&catalog_json, &value_path, &head_path,
                             weight, 4, 1, budget, 999, 999)
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
    Ok(policy.playout_candidate(&game, action_index))
}

/// Play a batch of determinized games natively and return their
/// training rows as flat arrays: (x_flat, y, row_counts_per_game).
/// `specs` is one (d1, d2, seed, handcrafted, learner_p1) per game.
#[pyfunction]
#[allow(clippy::too_many_arguments)]
fn stream_rows(
    py: Python<'_>,
    catalog_json: String, value_path: String, head_path: String,
    weight: f64, top_k: usize, budget: usize, k_worlds: usize,
    decklists_path: String,
    specs: Vec<(String, String, u64, bool, bool)>,
) -> PyResult<(Vec<f32>, Vec<f32>, Vec<usize>)> {
    let policy = build_policy(&catalog_json, &value_path, &head_path,
                             weight, top_k, 1, budget, 999, 999)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let decks = decks::load(&decklists_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    // Multithreaded: the policy and decks are read-only, so play the
    // games across `threads` OS threads with the GIL released, then
    // concatenate in spec order. One PyO3 call per round.
    let threads = std::thread::available_parallelism()
        .map(|n| n.get()).unwrap_or(8).min(specs.len().max(1));
    let policy = std::sync::Arc::new(policy);
    let decks = std::sync::Arc::new(decks);
    let specs = std::sync::Arc::new(specs);
    let results: Vec<(Vec<f32>, Vec<f32>, usize, usize)> =
        py.detach(|| {
        let mut handles = Vec::new();
        for t in 0..threads {
            let policy = policy.clone();
            let decks = decks.clone();
            let specs = specs.clone();
            handles.push(std::thread::spawn(move || {
                let mut out = Vec::new();
                let mut i = t;
                while i < specs.len() {
                    let (d1, d2, seed, hc, lp1) = &specs[i];
                    let learner = if *lp1 { penta::PlayerId::One }
                                  else { penta::PlayerId::Two };
                    let mut x = Vec::new();
                    let mut y = Vec::new();
                    let c = det_runner::play_game(&policy, &policy.tables,
                        &decks, d1, d2, *seed, k_worlds, *hc, learner,
                        &mut x, &mut y);
                    out.push((x, y, i, c));
                    i += threads;
                }
                out
            }));
        }
        let mut all = Vec::new();
        for h in handles { all.extend(h.join().unwrap()); }
        all
    });
    // reorder by spec index for determinism
    let mut ordered = results;
    ordered.sort_by_key(|r| r.2);
    let mut x = Vec::new();
    let mut y = Vec::new();
    let mut counts = vec![0usize; specs.len()];
    for (gx, gy, i, c) in ordered {
        x.extend_from_slice(&gx);
        y.extend_from_slice(&gy);
        counts[i] = c;
    }
    Ok((x, y, counts))
}

#[pyfunction]
#[allow(clippy::too_many_arguments)]
fn trace_game(catalog_json: String, value_path: String, head_path: String,
              weight: f64, budget: usize, k_worlds: usize,
              decklists_path: String, d1: String, d2: String, seed: u64,
              handcrafted: bool, learner_p1: bool, max_moves: usize)
              -> PyResult<Vec<i64>> {
    let policy = build_policy(&catalog_json, &value_path, &head_path,
                             weight, 4, 1, budget, 999, 999)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let decks = decks::load(&decklists_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let learner = if learner_p1 { penta::PlayerId::One } else { penta::PlayerId::Two };
    Ok(det_runner::trace_game(&policy, &decks, &d1, &d2, seed, k_worlds,
                              handcrafted, learner, max_moves))
}

#[pyfunction]
#[allow(clippy::too_many_arguments)]
fn choose_world(catalog_json: String, value_path: String, head_path: String,
                weight: f64, top_k: usize, budget: usize,
                raw_obs: String, hidden: String, rollout: u64)
                -> PyResult<usize> {
    let policy = build_policy(&catalog_json, &value_path, &head_path,
                             weight, top_k, 1, budget, 999, 999)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let world = penta::protocol::BotGame::from_observation_json(
        &raw_obs, &hidden, rollout)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    Ok(policy.choose(world.core_game()))
}

#[pyfunction]
fn prng_probe(seed: u64, count: usize) -> Vec<u64> {
    let mut p = crate::prng::SplitMix64::new(seed);
    (0..count).map(|_| p.next_u64()).collect()
}

#[pyfunction]
fn shuffle_probe(seed: u64, n: usize) -> Vec<usize> {
    let mut p = crate::prng::SplitMix64::new(seed);
    let mut v: Vec<usize> = (0..n).collect();
    p.shuffle(&mut v);
    v
}

#[pymodule]
fn spz_core(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(lockstep_trace, m)?)?;
    m.add_function(wrap_pyfunction!(net_value, m)?)?;
    m.add_function(wrap_pyfunction!(bench_native_selfplay, m)?)?;
    m.add_function(wrap_pyfunction!(choose_at, m)?)?;
    m.add_function(wrap_pyfunction!(playout_at, m)?)?;
    m.add_function(wrap_pyfunction!(stream_rows, m)?)?;
    m.add_function(wrap_pyfunction!(prng_probe, m)?)?;
    m.add_function(wrap_pyfunction!(shuffle_probe, m)?)?;
    m.add_function(wrap_pyfunction!(choose_world, m)?)?;
    m.add_function(wrap_pyfunction!(trace_game, m)?)?;
    Ok(())
}
