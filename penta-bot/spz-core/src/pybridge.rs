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
    let mut tables = Tables::load(catalog)?;
    let value = Mlp::load(value_path)?;
    // The belief (1081) schema is selected by the net's input width: turn
    // the hidden-pool block on iff the value net expects it.
    tables.set_belief(value.inputs > tables.v2_size);
    Ok(Policy {
        tables,
        value,
        head: Mlp::load(head_path)?,
        fast_head: None,
        weight,
        search: SearchConfig { top_k, playouts, budget,
                               playout_max_eval: pme },
        max_eval,
    })
}

/// Parse the in-tree opponent-model flag ("handcrafted" default, or
/// "greedy") into the typed `MctsConfig.opponent`.
fn parse_opponent(name: &str) -> PyResult<crate::mcts::OpponentModel> {
    match name {
        "handcrafted" => Ok(crate::mcts::OpponentModel::Handcrafted),
        "greedy" => Ok(crate::mcts::OpponentModel::Greedy),
        other => Err(pyo3::exceptions::PyValueError::new_err(format!(
            "unknown opponent model: {other} (want \"handcrafted\" or \"greedy\")"))),
    }
}

/// Seat-indexed decklist count arrays [p1, p2] for the belief block.
fn deck_slots_for(tables: &Tables, decks: &crate::decks::Decks,
                  d1: &str, d2: &str) -> [Vec<i32>; 2] {
    let empty = std::collections::HashMap::new();
    [tables.deck_slots(decks.get(d1).unwrap_or(&empty)),
     tables.deck_slots(decks.get(d2).unwrap_or(&empty))]
}

#[pyfunction]
#[allow(clippy::needless_pass_by_value, clippy::too_many_arguments)]
fn lockstep_trace(
    catalog_json: String,
    d1: String,
    d2: String,
    seed: u64,
    max_decisions: usize,
    belief: bool,
    decklists_path: String,
) -> PyResult<Vec<(String, Vec<f32>)>> {
    let mut tables = Tables::load(&catalog_json)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    tables.set_belief(belief);
    let deck_slots = if belief {
        let decks = decks::load(&decklists_path)
            .map_err(pyo3::exceptions::PyValueError::new_err)?;
        deck_slots_for(&tables, &decks, &d1, &d2)
    } else {
        [vec![0i32; tables.defs], vec![0i32; tables.defs]]
    };
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
        out.push((json, features(&obs, pregame, &tables, &deck_slots)));
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
    // Benchmark helper: exercises only the greedy choose path (825 nets in
    // the lockstep/bench suite), so the belief decklists are placeholders.
    let zero = [vec![0i32; policy.tables.defs], vec![0i32; policy.tables.defs]];
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
            let idx = policy.choose(&game, &zero);
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
    let zero = [vec![0i32; policy.tables.defs], vec![0i32; policy.tables.defs]];
    Ok(policy.choose(&game, &zero))
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
    let zero = [vec![0i32; policy.tables.defs], vec![0i32; policy.tables.defs]];
    Ok(policy.playout_candidate(&game, action_index, &zero))
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
    decklists_path: String, epsilon: f64, prior_frac: f64, inert: bool,
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
                        epsilon, prior_frac, inert, &mut x, &mut y);
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
    let zero = [vec![0i32; policy.tables.defs], vec![0i32; policy.tables.defs]];
    Ok(policy.choose(world.core_game(), &zero))
}

/// One native SO-ISMCTS choice at a state replayed to `history` from
/// (d1,d2,seed) External (mirrors `choose_at`, deterministic by action
/// history so tests are reproducible). Returns (best_index, visit_counts)
/// over the root protocol_actions; the visit vector is exposed for policy
/// iteration. Determinization draws use the shared splitmix64 seeded from
/// `seed` so a run reproduces. `inert` selects the inert hypothesis.
#[pyfunction]
#[pyo3(signature = (catalog_json, value_path, head_path, weight, iters,
    c_puct, budget, decklists_path, d1, d2, seed, inert, history,
    opponent = "handcrafted".to_string()))]
#[allow(clippy::needless_pass_by_value, clippy::too_many_arguments)]
fn ismcts_choose_at(
    catalog_json: String, value_path: String, head_path: String,
    weight: f64, iters: usize, c_puct: f64, budget: usize,
    decklists_path: String, d1: String, d2: String, seed: u64,
    inert: bool, history: Vec<usize>, opponent: String,
) -> PyResult<(usize, Vec<u32>)> {
    let policy = build_policy(&catalog_json, &value_path, &head_path,
                             weight, 0, 1, budget, 999, 999)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let decks = decks::load(&decklists_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let mut game = penta::protocol::BotGame::new(
        &d1, &d2, penta::protocol::Opponent::External,
        penta::PlayerId::Two, seed,
    ).map_err(pyo3::exceptions::PyValueError::new_err)?;
    for idx in history {
        game.act(idx)
            .map_err(|e| pyo3::exceptions::PyValueError::new_err(e.to_string()))?;
    }
    let our_seat = game.decision_seat()
        .ok_or_else(|| pyo3::exceptions::PyValueError::new_err("game is over"))?;
    // Seat-relative deck names for the determinization sampler.
    let (my_deck, opp_deck) = if our_seat == penta::PlayerId::One {
        (d1.as_str(), d2.as_str())
    } else {
        (d2.as_str(), d1.as_str())
    };
    let deck_slots = deck_slots_for(&policy.tables, &decks, &d1, &d2);
    let cfg = crate::mcts::MctsConfig {
        iters, c_puct, inert,
        use_dominance: true, leaf_playout: false, leaf_blend: false,
        redeterminize_m: 1,
        opponent: parse_opponent(&opponent)?,
        max_decisions: 800,
        max_depth: 400,
    };
    let search = crate::mcts::Ismcts {
        policy: &policy, decks: &decks, deck_slots: &deck_slots,
        my_deck, opp_deck, our_seat, cfg,
    };
    let mut prng = crate::prng::SplitMix64::new(
        seed.wrapping_mul(0x9E3779B97F4A7C15) ^ 0xD1B5);
    Ok(search.search(&game, &mut prng))
}

/// One native SO-ISMCTS choice for LIVE hosted play, from a raw protocol
/// observation (no local game object) -- the observation-based sibling of
/// `ismcts_choose_at`, mirroring `DeterminizedPolicy.choose`. `our_deck`
/// is our known deck; `opp_deck` is the caller's best-overlap guess (both
/// seat-relative). Returns the chosen protocol action index; on any
/// reconstruction/parse failure it returns the first legal action (index
/// 0), matching the det path's fail-closed contract -- the Python wrapper
/// substitutes its shaped fallback there.
#[pyfunction]
#[pyo3(signature = (catalog_json, value_path, head_path, weight, iters,
    c_puct, budget, inert, decklists_path, raw_obs, our_deck, opp_deck,
    opponent = "handcrafted".to_string(), leaf_playout = false))]
#[allow(clippy::needless_pass_by_value, clippy::too_many_arguments)]
fn ismcts_choose(
    catalog_json: String, value_path: String, head_path: String,
    weight: f64, iters: usize, c_puct: f64, budget: usize, inert: bool,
    decklists_path: String, raw_obs: String,
    our_deck: String, opp_deck: String, opponent: String,
    leaf_playout: bool,
) -> PyResult<usize> {
    let policy = build_policy(&catalog_json, &value_path, &head_path,
                             weight, 0, 1, budget, 999, 999)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let decks = decks::load(&decklists_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let value: serde_json::Value = serde_json::from_str(&raw_obs)
        .map_err(|e| pyo3::exceptions::PyValueError::new_err(e.to_string()))?;
    let seat = value.get("seat").and_then(|s| s.as_str())
        .ok_or_else(|| pyo3::exceptions::PyValueError::new_err(
            "observation missing seat"))?;
    let our_seat = if seat == "p1" { penta::PlayerId::One }
                   else { penta::PlayerId::Two };
    // Seat-indexed decklists for the belief block (p1 plays d1, p2 plays d2).
    let (d1, d2) = if our_seat == penta::PlayerId::One {
        (our_deck.as_str(), opp_deck.as_str())
    } else {
        (opp_deck.as_str(), our_deck.as_str())
    };
    let deck_slots = deck_slots_for(&policy.tables, &decks, d1, d2);
    let cfg = crate::mcts::MctsConfig {
        iters, c_puct, inert,
        use_dominance: true, leaf_playout, leaf_blend: false,
        redeterminize_m: 1,
        opponent: parse_opponent(&opponent)?,
        max_decisions: 800,
        max_depth: 400,
    };
    let search = crate::mcts::Ismcts {
        policy: &policy, decks: &decks, deck_slots: &deck_slots,
        my_deck: &our_deck, opp_deck: &opp_deck, our_seat, cfg,
    };
    // Per-move determinism: seed from the observation bytes so the same
    // decision reproduces its world draws.
    use std::hash::{Hash, Hasher};
    let mut hasher = std::collections::hash_map::DefaultHasher::new();
    raw_obs.hash(&mut hasher);
    let mut prng = crate::prng::SplitMix64::new(hasher.finish() ^ 0xD1B5);
    Ok(search.search_obs(&raw_obs, &mut prng).map_or(0, |(best, _)| best))
}

/// Native SO-ISMCTS gate: play `n_games` full games vs the engine's
/// handcrafted bot across the deck matchup (alternating seats, mirroring
/// gate_hosted's pairing), entirely in Rust -- no per-move Python/JSON
/// crossing. Multithreaded like `stream_rows`. Returns (wins, draws,
/// games_finished). `specs` is the (d1, d2, our_p1, seed) per game
/// (the caller builds the round-robin pairing, like ismcts_gate_stream).
#[pyfunction]
#[pyo3(signature = (catalog_json, value_path, head_path, weight, iters,
    c_puct, budget, inert, redeterminize_m, decklists_path, specs, workers,
    opponent = "handcrafted".to_string(), max_decisions = 800,
    classify = true))]
#[allow(clippy::needless_pass_by_value, clippy::too_many_arguments)]
fn ismcts_gate(
    py: Python<'_>,
    catalog_json: String, value_path: String, head_path: String,
    weight: f64, iters: usize, c_puct: f64, budget: usize, inert: bool,
    redeterminize_m: usize, decklists_path: String,
    specs: Vec<(String, String, bool, u64)>, workers: usize,
    opponent: String, max_decisions: usize, classify: bool,
) -> PyResult<(usize, usize, usize, usize)> {
    let policy = build_policy(&catalog_json, &value_path, &head_path,
                             weight, 0, 1, budget, 999, 999)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let decks = decks::load(&decklists_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let book = decks::DeckBook::load(&decklists_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let cfg = crate::mcts::MctsConfig {
        iters, c_puct, inert, use_dominance: true,
        leaf_playout: false, leaf_blend: false,
        redeterminize_m: redeterminize_m.max(1),
        opponent: parse_opponent(&opponent)?,
        max_decisions,
        max_depth: 400,
    };
    run_gate(py, policy, decks, book, cfg, specs, workers, classify)
}

/// Gate an ALPHAZERO checkpoint: the value net scores leaves, the
/// factorised `.azp` head supplies PUCT priors. `ismcts_gate` cannot do
/// this -- it builds both roles from `Mlp` files, and the AZ policy head
/// is not an `Mlp`, so without this there is no way to measure whether the
/// AZ loop is getting stronger.
#[pyfunction]
#[allow(clippy::too_many_arguments)]
fn az_gate(
    py: Python<'_>,
    catalog_json: String, value_path: String, policy_path: String,
    iters: usize, c_puct: f64, decklists_path: String,
    specs: Vec<(String, String, bool, u64)>, workers: usize,
    max_decisions: usize, classify: bool,
) -> PyResult<(usize, usize, usize, usize)> {
    let policy = build_policy(&catalog_json, &value_path, &value_path,
                              0.0, 0, 1, 400, 999, 999)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let fh = crate::action_feat::PolicyHead::load(&policy_path)
        .map_err(|e| pyo3::exceptions::PyValueError::new_err(
            format!("policy head {policy_path}: {e}")))?;
    let policy = crate::policy::Policy { fast_head: Some(fh), ..policy };
    let decks = decks::load(&decklists_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let book = decks::DeckBook::load(&decklists_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let cfg = crate::mcts::MctsConfig {
        // Dominance pruning ON for the gate. It is a CERTIFIED prune, so it
        // only removes provably-dominated actions and cannot weaken play --
        // and without it every wide decision (up to 538 candidates) is
        // searched in full. Measured: a 200-game gate took 422s, roughly
        // half the wall clock of the ten training rounds it was scoring.
        //
        // Self-play generation deliberately leaves it OFF: there the visit
        // vector is a training target, and a target over a pruned action
        // set is not the distribution the policy is asked to reproduce.
        iters, c_puct, inert: false, use_dominance: true,
        leaf_playout: false, leaf_blend: false, redeterminize_m: 1,
        opponent: crate::mcts::OpponentModel::Handcrafted,
        max_decisions,
        max_depth: 400,
    };
    run_gate(py, policy, decks, book, cfg, specs, workers, classify)
}

#[allow(clippy::too_many_arguments)]
fn run_gate(
    py: Python<'_>, policy: crate::policy::Policy,
    decks: crate::decks::Decks, book: crate::decks::DeckBook,
    cfg: crate::mcts::MctsConfig,
    specs: Vec<(String, String, bool, u64)>, workers: usize, classify: bool,
) -> PyResult<(usize, usize, usize, usize)> {
    let threads = if workers == 0 {
        std::thread::available_parallelism().map(|n| n.get()).unwrap_or(8)
    } else { workers }.min(specs.len().max(1));
    let policy = std::sync::Arc::new(policy);
    let decks = std::sync::Arc::new(decks);
    let book = std::sync::Arc::new(book);
    let specs = std::sync::Arc::new(specs);
    let cfg = std::sync::Arc::new(cfg);
    // (wins, draws, finished, capped). `finished` counts every game that
    // produced a score (natural OR capped-as-loss), so wins+draws+losses ==
    // finished and the win% is bounded; `capped` reports how many of those
    // losses were stragglers cut at the decision cap.
    let tallies: Vec<(usize, usize, usize, usize)> = py.detach(|| {
        let mut handles = Vec::new();
        for t in 0..threads {
            let policy = policy.clone();
            let decks = decks.clone();
            let book = book.clone();
            let specs = specs.clone();
            let cfg = cfg.clone();
            handles.push(std::thread::spawn(move || {
                let (mut w, mut d, mut f, mut cap) = (0usize, 0usize, 0usize, 0usize);
                let mut i = t;
                let mut x = Vec::new();
                let mut y = Vec::new();
                while i < specs.len() {
                    let (d1, d2, our_p1, seed) = &specs[i];
                    let out = crate::mcts_runner::play_ismcts_game(
                        &policy, &policy.tables, &decks, &book, d1, d2,
                        *our_p1, *seed, &cfg, classify, false, &mut x,
                        &mut y);
                    if let Some(s) = out.score {
                        f += 1;
                        if s == 1.0 { w += 1; } else if s == 0.5 { d += 1; }
                        if out.capped { cap += 1; }
                    }
                    i += threads;
                }
                (w, d, f, cap)
            }));
        }
        handles.into_iter().map(|h| h.join().unwrap()).collect()
    });
    let (mut wins, mut draws, mut finished, mut capped) = (0, 0, 0, 0);
    for (w, d, f, c) in tallies {
        wins += w; draws += d; finished += f; capped += c;
    }
    Ok((wins, draws, finished, capped))
}

/// Native SO-ISMCTS training rows: play `specs` full games vs the
/// handcrafted opponent and return their (redacted afterstate, z) value
/// rows as flat arrays (x_flat, y, row_counts_per_game), parallel to
/// `stream_rows`. `specs` is (d1, d2, our_p1, seed) per game.
#[pyfunction]
#[pyo3(signature = (catalog_json, value_path, head_path, weight, iters,
    c_puct, budget, inert, redeterminize_m, decklists_path, specs, workers,
    opponent = "handcrafted".to_string(), max_decisions = 800))]
#[allow(clippy::needless_pass_by_value, clippy::too_many_arguments)]
fn ismcts_stream_rows(
    py: Python<'_>,
    catalog_json: String, value_path: String, head_path: String,
    weight: f64, iters: usize, c_puct: f64, budget: usize, inert: bool,
    redeterminize_m: usize, decklists_path: String,
    specs: Vec<(String, String, bool, u64)>, workers: usize,
    opponent: String, max_decisions: usize,
) -> PyResult<(Vec<f32>, Vec<f32>, Vec<usize>)> {
    let policy = build_policy(&catalog_json, &value_path, &head_path,
                             weight, 0, 1, budget, 999, 999)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let decks = decks::load(&decklists_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let book = decks::DeckBook::load(&decklists_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let cfg = crate::mcts::MctsConfig {
        iters, c_puct, inert, use_dominance: true,
        leaf_playout: false, leaf_blend: false,
        redeterminize_m: redeterminize_m.max(1),
        opponent: parse_opponent(&opponent)?,
        max_decisions,
        max_depth: 400,
    };
    let threads = if workers == 0 {
        std::thread::available_parallelism().map(|n| n.get()).unwrap_or(8)
    } else { workers }.min(specs.len().max(1));
    let policy = std::sync::Arc::new(policy);
    let decks = std::sync::Arc::new(decks);
    let book = std::sync::Arc::new(book);
    let specs = std::sync::Arc::new(specs);
    let cfg = std::sync::Arc::new(cfg);
    let results: Vec<(Vec<f32>, Vec<f32>, usize, usize)> = py.detach(|| {
        let mut handles = Vec::new();
        for t in 0..threads {
            let policy = policy.clone();
            let decks = decks.clone();
            let book = book.clone();
            let specs = specs.clone();
            let cfg = cfg.clone();
            handles.push(std::thread::spawn(move || {
                let mut out = Vec::new();
                let mut i = t;
                while i < specs.len() {
                    let (d1, d2, our_p1, seed) = &specs[i];
                    let mut x = Vec::new();
                    let mut y = Vec::new();
                    let o = crate::mcts_runner::play_ismcts_game(
                        &policy, &policy.tables, &decks, &book, d1, d2,
                        *our_p1, *seed, &cfg, true, true, &mut x, &mut y);
                    out.push((x, y, i, o.rows));
                    i += threads;
                }
                out
            }));
        }
        let mut all = Vec::new();
        for h in handles { all.extend(h.join().unwrap()); }
        all
    });
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

// ---- native AAC self-play -------------------------------------------
//
// One call per PPO round: Python hands over the current actor weights and
// a list of episode specs, Rust plays them across OS threads with the GIL
// released, and the whole batch of trajectories comes back as flat
// buffers. No per-decision boundary crossing, no worker pool, no fork.

/// Little-endian bytes for a float slice, so the big trajectory arrays
/// cross into Python as one `bytes` object each (`np.frombuffer`) instead
/// of a multi-million-element Python list.
fn f32_bytes(v: &[f32]) -> Vec<u8> {
    let mut out = Vec::with_capacity(v.len() * 4);
    for x in v {
        out.extend_from_slice(&x.to_le_bytes());
    }
    out
}

fn f64_bytes(v: &[f64]) -> Vec<u8> {
    let mut out = Vec::with_capacity(v.len() * 8);
    for x in v {
        out.extend_from_slice(&x.to_le_bytes());
    }
    out
}

/// Work-stealing cursor: threads pull the next index instead of owning a
/// fixed stride.
///
/// This box is an i9-13900KS -- 8 performance cores plus 16 efficiency
/// cores, not 32 interchangeable ones. With static round-robin
/// (`i += n_threads`) the games handed to an E-core thread take far
/// longer, and since the call cannot return until every thread is done,
/// the slowest straggler sets the wall clock. That is what made one
/// process look like it saturated at 8 threads (8.5 g/s) while four
/// separate processes reached ~26 g/s aggregate on the same machine:
/// smaller per-process batches simply had shorter straggler tails.
///
/// Pulling indices off one atomic lets the fast cores take more games and
/// the slow ones take fewer, so the tail shrinks to a single game.
fn next_index(cursor: &std::sync::atomic::AtomicUsize, n: usize)
              -> Option<usize> {
    let i = cursor.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    (i < n).then_some(i)
}

fn thread_count(requested: usize, work: usize) -> usize {
    let n = if requested == 0 {
        std::thread::available_parallelism().map_or(8, std::num::NonZero::get)
    } else {
        requested
    };
    n.min(work.max(1)).max(1)
}

/// Build the actor + tables + decklists shared by every native AAC entry.
#[allow(clippy::too_many_arguments)]
fn build_aac(catalog_json: &str, decklists_path: &str, belief: bool,
             hidden: usize, w1: Vec<f64>, b1: Vec<f64>, w2: Vec<f64>,
             b2: f64)
             -> Result<(crate::aac::Actor, Tables, decks::DeckBook), String> {
    let mut tables = Tables::load(catalog_json)?;
    tables.set_belief(belief);
    let actor = crate::aac::Actor::new(tables.size, hidden, w1, b1, w2, b2)?;
    let book = decks::DeckBook::load(decklists_path)?;
    Ok((actor, tables, book))
}

/// Play a batch of AAC self-play episodes natively and return their
/// trajectories.
///
/// `specs` is one `(d1, d2, seed, handcrafted, learner_is_p1)` per
/// episode. The return is a flat, dtype-tagged bundle (see
/// `aac_native.py`, which reassembles it into the record dicts
/// `compute_gae` / `ppo_update_fast` already consume):
///
/// ```text
/// 0  cand_bytes    f32, sum(m)*feat        candidate afterstate features
/// 1  rec_u32       u32, 2*n_records        m per record, then the sampled
///                                          candidate index per record
///                                          (two n_records halves; packed
///                                          because PyO3 tuples stop at 12)
/// 3  logp_old      f64, n_records          log pi(chosen) at collection
/// 4  logit_bytes   f64, sum(m)             unsquashed actor logits
/// 5  priv_bytes    f32, n_records*2*feat   privileged critic input
/// 6  reward        f64, n_records          shaped tempo reward
/// 7  seat          u8,  n_records          0 = p1, 1 = p2
/// 8  ep_records    u32, n_episodes         records per episode
/// 9  ep_result     i8,  n_episodes         0 p1, 1 p2, 2 draw, -1 capped
/// 9b final_bytes   f32, (truncated eps)*2*feat  [features(p1),features(p2)]
///                                          at the cut, ONE BLOCK PER
///                                          TRUNCATED episode in order --
///                                          lets the learner bootstrap
///                                          V(s_T) instead of discarding
/// 10 ep_diag       u32, 2*n_episodes       decisions played, then the
///                                          widest legal-action list seen
///                                          (two n_episodes-long halves;
///                                          packed because PyO3 tuples
///                                          stop at 12 elements)
/// 11 feat          usize                   feature width (825 or 1081)
/// ```
#[pyfunction]
#[allow(clippy::needless_pass_by_value, clippy::too_many_arguments)]
fn aac_stream_episodes(
    py: Python<'_>,
    catalog_json: String,
    decklists_path: String,
    belief: bool,
    hidden: usize,
    w1: Vec<f64>,
    b1: Vec<f64>,
    w2: Vec<f64>,
    b2: f64,
    temperature: f64,
    threads: usize,
    max_actions: usize,
    open_decklist: bool,
    specs: Vec<(String, String, u64, bool, bool)>,
) -> PyResult<(pyo3::Bound<'_, pyo3::types::PyBytes>, Vec<u32>,
               Vec<f64>, pyo3::Bound<'_, pyo3::types::PyBytes>,
               pyo3::Bound<'_, pyo3::types::PyBytes>, Vec<f64>, Vec<u8>,
               Vec<u32>, Vec<i8>, Vec<u32>, usize,
               pyo3::Bound<'_, pyo3::types::PyBytes>)>
{
    use pyo3::types::PyBytes;

    let (actor, tables, book) = build_aac(&catalog_json, &decklists_path,
                                          belief, hidden, w1, b1, w2, b2)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let feat = tables.size;
    let n_threads = thread_count(threads, specs.len());

    let actor = std::sync::Arc::new(actor);
    let tables = std::sync::Arc::new(tables);
    let book = std::sync::Arc::new(book);
    let specs = std::sync::Arc::new(specs);

    // (spec index, episode) pairs, reordered to spec order afterwards so
    // the batch is reproducible regardless of how the threads interleave.
    let cursor = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let mut episodes: Vec<(usize, crate::aac::Episode)> = py.detach(|| {
        let mut handles = Vec::new();
        for _ in 0..n_threads {
            let (actor, tables, book, specs, cursor) =
                (actor.clone(), tables.clone(), book.clone(), specs.clone(),
                 cursor.clone());
            handles.push(std::thread::spawn(move || {
                let mut out = Vec::new();
                while let Some(i) = next_index(&cursor, specs.len()) {
                    let (d1, d2, seed, hc, lp1) = &specs[i];
                    let learner = if *lp1 { penta::PlayerId::One }
                                  else { penta::PlayerId::Two };
                    out.push((i, crate::aac::play_episode(
                        &actor, &tables, &book, d1, d2, *seed, temperature,
                        *hc, learner, max_actions, open_decklist)));
                }
                out
            }));
        }
        let mut all = Vec::new();
        for h in handles {
            all.extend(h.join().expect("aac worker thread panicked"));
        }
        all
    });
    episodes.sort_by_key(|(i, _)| *i);

    let mut cand: Vec<f32> = Vec::new();
    let mut cand_counts: Vec<u32> = Vec::new();
    let mut chosen: Vec<u32> = Vec::new();
    let mut logp: Vec<f64> = Vec::new();
    let mut logits: Vec<f64> = Vec::new();
    let mut privileged: Vec<f32> = Vec::new();
    let mut reward: Vec<f64> = Vec::new();
    let mut seat: Vec<u8> = Vec::new();
    let mut ep_records: Vec<u32> = Vec::new();
    let mut ep_result: Vec<i8> = Vec::new();
    let mut ep_decisions: Vec<u32> = Vec::new();
    let mut ep_maxactions: Vec<u32> = Vec::new();
    let mut final_feat: Vec<f32> = Vec::new();
    for (_, ep) in episodes {
        final_feat.extend_from_slice(&ep.final_feat);
        ep_records.push(ep.records.len() as u32);
        ep_result.push(ep.result);
        ep_decisions.push(ep.decisions as u32);
        ep_maxactions.push(ep.max_actions as u32);
        for r in ep.records {
            cand.extend_from_slice(&r.cand);
            cand_counts.push(r.m as u32);
            chosen.push(r.chosen as u32);
            logp.push(r.logp_old);
            logits.extend_from_slice(&r.logits);
            privileged.extend_from_slice(&r.privileged);
            reward.push(r.reward);
            seat.push(r.seat);
        }
    }
    Ok((PyBytes::new(py, &f32_bytes(&cand)),
        cand_counts.into_iter().chain(chosen).collect(), logp,
        PyBytes::new(py, &f64_bytes(&logits)),
        PyBytes::new(py, &f32_bytes(&privileged)), reward, seat,
        ep_records, ep_result,
        ep_decisions.into_iter().chain(ep_maxactions).collect(), feat,
        PyBytes::new(py, &f32_bytes(&final_feat))))
}

/// The evaluation, natively: actor ARGMAX (observation only) versus the
/// engine's handcrafted bot, alternating seats and rotating opponent
/// decks exactly like `gate_belief` in aac_torch_par.py.
///
/// Returns (score rate, per-game decisions, per-game widest action list)
/// -- the two diagnostic vectors are what identify the wide-decision
/// outliers that dominate gate wall clock.
#[pyfunction]
#[allow(clippy::needless_pass_by_value, clippy::too_many_arguments)]
fn aac_gate(
    py: Python<'_>,
    catalog_json: String,
    decklists_path: String,
    belief: bool,
    hidden: usize,
    w1: Vec<f64>,
    b1: Vec<f64>,
    w2: Vec<f64>,
    b2: f64,
    learner_deck: String,
    games: usize,
    seed_base: u64,
    threads: usize,
    max_actions: usize,
    open_decklist: bool,
) -> PyResult<(f64, Vec<u32>, Vec<u32>, Vec<String>, Vec<f64>, Vec<u32>)> {
    let (actor, tables, book) = build_aac(&catalog_json, &decklists_path,
                                          belief, hidden, w1, b1, w2, b2)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let opps: Vec<String> = book.names().filter(|n| *n != learner_deck)
        .map(String::from).collect();
    if opps.is_empty() {
        return Err(pyo3::exceptions::PyValueError::new_err(
            format!("no opponent decks besides {learner_deck}")));
    }
    let n_threads = thread_count(threads, games);
    let actor = std::sync::Arc::new(actor);
    let tables = std::sync::Arc::new(tables);
    let book = std::sync::Arc::new(book);
    let opps = std::sync::Arc::new(opps);
    let learner_deck = std::sync::Arc::new(learner_deck);
    let cursor = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let mut per_game: Vec<(usize, f64, u32, u32)> = py.detach(|| {
        let mut handles = Vec::new();
        for _ in 0..n_threads {
            let (actor, tables, book, opps, learner_deck, cursor) =
                (actor.clone(), tables.clone(), book.clone(), opps.clone(),
                 learner_deck.clone(), cursor.clone());
            handles.push(std::thread::spawn(move || {
                let mut out = Vec::new();
                while let Some(g) = next_index(&cursor, games) {
                    let my_p1 = g % 2 == 0;
                    let opp_deck = &opps[g % opps.len()];
                    let (d1, d2) = if my_p1 {
                        (learner_deck.as_str(), opp_deck.as_str())
                    } else {
                        (opp_deck.as_str(), learner_deck.as_str())
                    };
                    let learner = if my_p1 { penta::PlayerId::One }
                                  else { penta::PlayerId::Two };
                    let (s, dec, mx) = crate::aac::gate_game(
                        &actor, &tables, &book, d1, d2, seed_base + g as u64,
                        learner, max_actions, open_decklist);
                    out.push((g, s, dec as u32, mx as u32));
                }
                out
            }));
        }
        let mut all = Vec::new();
        for h in handles {
            all.extend(h.join().expect("gate thread panicked"));
        }
        all
    });
    per_game.sort_by_key(|r| r.0);
    let score: f64 = per_game.iter().map(|r| r.1).sum();
    // Per-opponent breakdown. One aggregate number hides that a bot can be
    // dominant into half the field and unplayable into the other half,
    // which is the thing you actually want to fix.
    let mut names: Vec<String> = Vec::new();
    let mut sums: Vec<f64> = Vec::new();
    let mut counts: Vec<u32> = Vec::new();
    for (i, o) in opps.iter().enumerate() {
        names.push(o.clone());
        let mut s = 0.0;
        let mut n = 0u32;
        for r in &per_game {
            if r.0 % opps.len() == i { s += r.1; n += 1; }
        }
        sums.push(s);
        counts.push(n);
    }
    Ok((score / games as f64,
        per_game.iter().map(|r| r.2).collect(),
        per_game.iter().map(|r| r.3).collect(),
        names, sums, counts))
}

/// The width of one action encoding, so Python can size its policy head.
#[pyfunction]
#[allow(clippy::needless_pass_by_value)]
fn az_action_dim(catalog_json: String, belief: bool) -> PyResult<usize> {
    let mut t = Tables::load(&catalog_json)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    t.set_belief(belief);
    Ok(crate::action_feat::width(&t))
}

/// AlphaZero self-play: both seats driven by search, emitting the visit
/// distribution as a policy target and the outcome as a value target.
///
/// `policy_path` is the factorised head az_train.py writes; the search
/// takes its PUCT priors and its opponent model from it, which is what
/// makes a searched decision cost ~8 ms instead of ~1790 ms.
///
/// ```text
/// 0 cand_bytes   f32, sum(m)*action_dim  ACTION encodings (not afterstates)
/// 1 rec_u32      u32, 3*n_records        m, visit-sum, chosen
/// 2 visit_bytes  u32, sum(m)             visit counts per action
/// 3 priv_bytes   f32, n_records*2*feat   privileged rows
/// 4 seat         u8,  n_records          0 = p1, 1 = p2
/// 5 ep_records   u32, n_episodes
/// 6 ep_result    i8,  n_episodes         0 p1, 1 p2, 2 draw, -1 capped
/// 7 feat         usize                   STATE feature width
/// ```
#[pyfunction]
#[allow(clippy::needless_pass_by_value, clippy::too_many_arguments)]
fn az_stream_episodes(
    py: Python<'_>,
    catalog_json: String, value_path: String, policy_path: String,
    iters: usize, c_puct: f64, decklists_path: String,
    max_actions: usize, threads: usize,
    specs: Vec<(String, String, u64)>,
) -> PyResult<(pyo3::Bound<'_, pyo3::types::PyBytes>, Vec<u32>,
               pyo3::Bound<'_, pyo3::types::PyBytes>,
               pyo3::Bound<'_, pyo3::types::PyBytes>, Vec<u8>, Vec<u32>,
               Vec<i8>, usize)> {
    use pyo3::types::PyBytes;
    let mut policy = build_policy(&catalog_json, &value_path, &value_path,
                                  0.0, 0, 1, 400, 999, 999)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    policy.fast_head = Some(crate::action_feat::PolicyHead::load(&policy_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?);
    let decks = decks::load(&decklists_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let book = decks::DeckBook::load(&decklists_path)
        .map_err(pyo3::exceptions::PyValueError::new_err)?;
    let feat = policy.tables.size;
    let cfg = crate::mcts::MctsConfig {
        iters, c_puct, inert: false,
        // The dominance prune was 55% of search time and its job -- drop
        // actions with no upside versus passing -- is what the policy head
        // should learn. Off.
        use_dominance: false,
        leaf_playout: false, leaf_blend: false, redeterminize_m: 1,
        opponent: crate::mcts::OpponentModel::Greedy,
        max_decisions: crate::az::MAX_DECISIONS,
        max_depth: 400,
    };
    let n_threads = thread_count(threads, specs.len());
    let policy = std::sync::Arc::new(policy);
    let decks = std::sync::Arc::new(decks);
    let book = std::sync::Arc::new(book);
    let cfg = std::sync::Arc::new(cfg);
    let specs = std::sync::Arc::new(specs);
    let cursor = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));

    let mut episodes: Vec<(usize, crate::az::Episode)> = py.detach(|| {
        let mut handles = Vec::new();
        for _ in 0..n_threads {
            let (policy, decks, book, cfg, specs, cursor) =
                (policy.clone(), decks.clone(), book.clone(), cfg.clone(),
                 specs.clone(), cursor.clone());
            handles.push(std::thread::spawn(move || {
                let mut out = Vec::new();
                while let Some(i) = next_index(&cursor, specs.len()) {
                    let (d1, d2, seed) = &specs[i];
                    out.push((i, crate::az::play_episode(
                        &policy, &policy.tables, &decks, &book, d1, d2,
                        *seed, &cfg, max_actions)));
                }
                out
            }));
        }
        let mut all = Vec::new();
        for h in handles { all.extend(h.join().expect("az thread panicked")); }
        all
    });
    episodes.sort_by_key(|(i, _)| *i);

    let mut cand: Vec<f32> = Vec::new();
    let mut rec: Vec<u32> = Vec::new();
    let mut vsum: Vec<u32> = Vec::new();
    let mut chosen: Vec<u32> = Vec::new();
    let mut visits: Vec<u32> = Vec::new();
    let mut privileged: Vec<f32> = Vec::new();
    let mut seat: Vec<u8> = Vec::new();
    let mut ep_records: Vec<u32> = Vec::new();
    let mut ep_result: Vec<i8> = Vec::new();
    for (_, ep) in episodes {
        ep_records.push(ep.records.len() as u32);
        ep_result.push(ep.result);
        for r in ep.records {
            cand.extend_from_slice(&r.cand);
            rec.push(r.m as u32);
            vsum.push(r.visits.iter().sum());
            chosen.push(r.chosen as u32);
            visits.extend_from_slice(&r.visits);
            privileged.extend_from_slice(&r.privileged);
            seat.push(r.seat);
        }
    }
    let mut vb = Vec::with_capacity(visits.len() * 4);
    for v in &visits { vb.extend_from_slice(&v.to_le_bytes()); }
    rec.extend(vsum);
    rec.extend(chosen);
    Ok((PyBytes::new(py, &f32_bytes(&cand)), rec, PyBytes::new(py, &vb),
        PyBytes::new(py, &f32_bytes(&privileged)), seat, ep_records,
        ep_result, feat))
}

#[pymodule]
fn spz_core(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(az_stream_episodes, m)?)?;
    m.add_function(wrap_pyfunction!(az_action_dim, m)?)?;
    m.add_function(wrap_pyfunction!(aac_stream_episodes, m)?)?;
    m.add_function(wrap_pyfunction!(aac_gate, m)?)?;
    m.add_function(wrap_pyfunction!(lockstep_trace, m)?)?;
    m.add_function(wrap_pyfunction!(net_value, m)?)?;
    m.add_function(wrap_pyfunction!(bench_native_selfplay, m)?)?;
    m.add_function(wrap_pyfunction!(choose_at, m)?)?;
    m.add_function(wrap_pyfunction!(ismcts_choose_at, m)?)?;
    m.add_function(wrap_pyfunction!(ismcts_choose, m)?)?;
    m.add_function(wrap_pyfunction!(ismcts_gate, m)?)?;
    m.add_function(wrap_pyfunction!(az_gate, m)?)?;
    m.add_function(wrap_pyfunction!(ismcts_stream_rows, m)?)?;
    m.add_function(wrap_pyfunction!(playout_at, m)?)?;
    m.add_function(wrap_pyfunction!(stream_rows, m)?)?;
    m.add_function(wrap_pyfunction!(prng_probe, m)?)?;
    m.add_function(wrap_pyfunction!(shuffle_probe, m)?)?;
    m.add_function(wrap_pyfunction!(choose_world, m)?)?;
    m.add_function(wrap_pyfunction!(trace_game, m)?)?;
    Ok(())
}
