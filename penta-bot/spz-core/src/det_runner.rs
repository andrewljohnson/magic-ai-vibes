//! Native determinized self-play that EMITS training rows. Rust plays
//! the games (K-world consensus via Game::from_observation, the
//! certified evaluate-all policy); Python keeps SGD, the replay ring,
//! league scheduling, and promotion. One PyO3 call returns a whole
//! batch of games as flat arrays -- no per-decision boundary crossing.
//!
//! Row semantics MATCH the Python det_selfplay path (verified by the
//! row-lockstep check): a row is the redacted afterstate feature vector
//! of the REAL game after the acting seat plays, recorded at every
//! non-forced decision; targets are TD-lambda=1.0 == the seat's game
//! outcome z (0/0.5/1). Capped games (>=600 decisions) emit no rows.
//! Worlds are drawn with the shared splitmix64 PRNG so a Python run on
//! the same seeds draws the identical worlds.

use penta::protocol::{protocol_actions, BotGame, Opponent};
use penta::{PlayerId, PlayerObservation};

use crate::decks::Decks;
use crate::extract::features;
use crate::policy::Policy;
use crate::prng::SplitMix64;
use crate::tables::Tables;

const MAX_DECISIONS: usize = 600;

fn seat_idx(s: PlayerId) -> usize { if s == PlayerId::One { 0 } else { 1 } }

fn seen_defs(obs: &PlayerObservation, seat: PlayerId, include_hand: bool)
             -> Vec<u16> {
    let si = seat_idx(seat);
    let mut v = Vec::new();
    if include_hand {
        for (_, d) in &obs.hand { v.push(d.0); }
    }
    for p in &obs.battlefield {
        if p.controller == seat { v.push(p.definition.0); }
    }
    for (_, d) in &obs.graveyards[si] { v.push(d.0); }
    for (_, d) in &obs.exiles[si] { v.push(d.0); }
    v
}

fn unseen_pool(decks: &Decks, name: &str, seen: &[u16]) -> Vec<u16> {
    let mut pool: Vec<u16> = Vec::new();
    if let Some(counts) = decks.get(name) {
        let mut sorted: Vec<(&u16, &u32)> = counts.iter().collect();
        sorted.sort();  // deterministic pool order (defn ascending)
        for (d, c) in sorted {
            for _ in 0..*c { pool.push(*d); }
        }
    }
    for &d in seen {
        if let Some(pos) = pool.iter().position(|&x| x == d) {
            pool.remove(pos);
        }
    }
    pool
}

/// Build the hidden-hypothesis JSON for Game::from_observation, or None
/// when the deck hypothesis cannot cover the observation. Mirrors
/// hosted_policy.sample_hidden with the shared PRNG.
fn sample_hidden(obs: &PlayerObservation, prng: &mut SplitMix64,
                 decks: &Decks, my_deck: &str, opp_deck: &str)
                 -> Option<String> {
    let me = obs.viewer;
    let opp = if me == PlayerId::One { PlayerId::Two } else { PlayerId::One };
    let (mi, oi) = (seat_idx(me), seat_idx(opp));
    let mut my_pool = unseen_pool(decks, my_deck,
                                  &seen_defs(obs, me, true));
    let mut opp_pool = unseen_pool(decks, opp_deck,
                                   &seen_defs(obs, opp, false));
    prng.shuffle(&mut my_pool);
    prng.shuffle(&mut opp_pool);
    let need_opp = obs.opponent_hand_size + obs.library_sizes[oi];
    if my_pool.len() < obs.library_sizes[mi] || opp_pool.len() < need_opp {
        return None;
    }
    let opp_hand = &opp_pool[..obs.opponent_hand_size];
    let opp_lib = &opp_pool[obs.opponent_hand_size
        ..obs.opponent_hand_size + obs.library_sizes[oi]];
    let my_lib = &my_pool[..obs.library_sizes[mi]];
    let seat_name = |s: PlayerId| if s == PlayerId::One { "p1" } else { "p2" };
    let arr = |xs: &[u16]| -> String {
        let s: Vec<String> = xs.iter().map(|d| d.to_string()).collect();
        format!("[{}]", s.join(","))
    };
    Some(format!(
        "{{\"hands\":{{\"{}\":{}}},\"libraries\":{{\"{}\":{},\"{}\":{}}},\
         \"outsideGame\":{{\"p1\":[],\"p2\":[]}}}}",
        seat_name(opp), arr(opp_hand),
        seat_name(me), arr(my_lib), seat_name(opp), arr(opp_lib)))
}

/// Shared exploration draw (splitmix64) -- an explicit spec mirrored in
/// det_shared.py so exploration rows lockstep. First_bot-shaped prior:
/// land > biggest-power castable > attacker > uniform. Draw counts are
/// deterministic per branch, keeping the PRNG stream aligned with Python.
fn explore_pick(obs: &PlayerObservation, actions: &[penta::Action],
                tables: &Tables, prior_frac: f64,
                prng: &mut SplitMix64) -> usize {
    use penta::Action;
    let v = prng.next_f64();
    if v < prior_frac {
        // aggression prior over the FULL protocol action list
        let lands: Vec<usize> = actions.iter().enumerate()
            .filter(|(_, a)| matches!(a, Action::PlayLand { .. }))
            .map(|(i, _)| i).collect();
        if !lands.is_empty() {
            return lands[prng.below(lands.len())];
        }
        let mut hand_def = std::collections::HashMap::new();
        for (id, d) in &obs.hand { hand_def.insert(id.0, d.0); }
        let casts: Vec<usize> = actions.iter().enumerate()
            .filter(|(_, a)| matches!(a, Action::CastSpell { .. }))
            .map(|(i, _)| i).collect();
        if !casts.is_empty() {
            // biggest printed power, first on tie (no draw)
            let mut best = casts[0]; let mut bestp = i64::MIN;
            for &i in &casts {
                let p = if let Action::CastSpell { card, .. } = &actions[i] {
                    hand_def.get(&card.0)
                        .and_then(|d| tables.power.get(d)).copied()
                        .unwrap_or(0)
                } else { 0 };
                if p > bestp { bestp = p; best = i; }
            }
            return best;
        }
        let atk: Vec<usize> = actions.iter().enumerate()
            .filter(|(_, a)| matches!(a, Action::DeclareAttacker { .. }))
            .map(|(i, _)| i).collect();
        if !atk.is_empty() {
            return atk[prng.below(atk.len())];
        }
    }
    prng.below(actions.len())  // uniform
}

/// Consensus index over K hypothesis worlds; None when no world
/// survived (caller then plays the first legal action, matching the
/// Python DET_FALLBACK path's greedy degenerate case at epsilon 0).
fn det_choose(real: &BotGame, seat: PlayerId, policy: &Policy,
              decks: &Decks, my_deck: &str, opp_deck: &str, k: usize,
              prng: &mut SplitMix64) -> Option<usize> {
    let raw = real.observe_json(seat);
    let obs = real.core_game().observe(seat);
    let mut votes: std::collections::HashMap<usize, usize> =
        std::collections::HashMap::new();
    let mut sum_val: std::collections::HashMap<usize, f64> =
        std::collections::HashMap::new();
    for _ in 0..k {
        let hidden = match sample_hidden(&obs, prng, decks, my_deck, opp_deck) {
            Some(h) => h, None => continue,
        };
        let rollout_seed = prng.next_u64();
        let world = match BotGame::from_observation_json(&raw, &hidden,
                                                          rollout_seed) {
            Ok(w) => w, Err(_) => continue,  // fail closed per world
        };
        let (idx, val) = policy.choose_scored(world.core_game());
        *votes.entry(idx).or_insert(0) += 1;
        *sum_val.entry(idx).or_insert(0.0) += val;
    }
    if votes.is_empty() { return None; }
    let top = *votes.values().max().unwrap();
    let mut tied: Vec<usize> = votes.iter()
        .filter(|(_, &v)| v == top).map(|(&i, _)| i).collect();
    tied.sort();  // deterministic; then break by mean value (Python parity)
    Some(*tied.iter().max_by(|&&a, &&b| {
        let ma = sum_val[&a] / votes[&a] as f64;
        let mb = sum_val[&b] / votes[&b] as f64;
        ma.total_cmp(&mb).then(b.cmp(&a))  // higher mean; tie -> lower idx
    }).unwrap())
}

/// Trace the chosen index at each decision (for lockstep debugging).
/// -1 = forced (single action, played as index 0 without recording).
#[allow(clippy::too_many_arguments)]
pub fn trace_game(policy: &Policy, decks: &Decks, d1: &str, d2: &str,
                  seed: u64, k: usize, handcrafted: bool,
                  learner: PlayerId, max_moves: usize) -> Vec<i64> {
    let opp_seat = if learner == PlayerId::One { PlayerId::Two }
                   else { PlayerId::One };
    let mut game = if handcrafted {
        BotGame::new(d1, d2, Opponent::Handcrafted, opp_seat, seed)
    } else {
        BotGame::new(d1, d2, Opponent::External, PlayerId::Two, seed)
    }.expect("game");
    let mut prng = SplitMix64::new(seed.wrapping_mul(0x9E3779B97F4A7C15) ^ 0xD1B5);
    let mut out = Vec::new();
    let mut n = 0usize;
    while game.result().is_none() && n < max_moves {
        let seat = game.decision_seat().unwrap();
        let obs = game.core_game().observe(seat);
        if protocol_actions(&obs).len() == 1 {
            let _ = game.act(0); n += 1; out.push(-1); continue;
        }
        let (my, opp) = if seat == PlayerId::One { (d1, d2) } else { (d2, d1) };
        let idx = det_choose(&game, seat, policy, decks, my, opp, k, &mut prng)
            .unwrap_or(0);
        out.push(idx as i64);
        if game.act(idx).is_err() { break; }
        n += 1;
    }
    out
}

/// One determinized game. Pushes rows into (x_out, y_out); returns the
/// per-game row count (0 for capped games). Mirror mode records both
/// seats; handcrafted mode records only the learner seat.
#[allow(clippy::too_many_arguments)]
pub fn play_game(policy: &Policy, tables: &Tables, decks: &Decks,
                 d1: &str, d2: &str, seed: u64, k: usize,
                 handcrafted: bool, learner: PlayerId, epsilon: f64,
                 prior_frac: f64,
                 x_out: &mut Vec<f32>, y_out: &mut Vec<f32>) -> usize {
    let opp_seat = if learner == PlayerId::One { PlayerId::Two }
                   else { PlayerId::One };
    let mut game = if handcrafted {
        BotGame::new(d1, d2, Opponent::Handcrafted, opp_seat, seed)
    } else {
        BotGame::new(d1, d2, Opponent::External, PlayerId::Two, seed)
    }.expect("game");
    let mut prng = SplitMix64::new(seed.wrapping_mul(0x9E3779B97F4A7C15) ^ 0xD1B5);
    // per-seat recorded feature rows
    let mut rows: [Vec<Vec<f32>>; 2] = [Vec::new(), Vec::new()];
    let mut n = 0usize;
    while game.result().is_none() && n < MAX_DECISIONS {
        let seat = game.decision_seat().unwrap();
        let obs = game.core_game().observe(seat);
        if protocol_actions(&obs).len() == 1 {
            let _ = game.act(0);
            n += 1;
            continue;
        }
        let (my_deck, opp_deck) = if seat == PlayerId::One { (d1, d2) }
                                  else { (d2, d1) };
        let e = prng.next_f64();
        let idx = if epsilon > 0.0 && e < epsilon {
            let acts = protocol_actions(&obs);
            explore_pick(&obs, &acts, tables, prior_frac, &mut prng)
        } else {
            det_choose(&game, seat, policy, decks, my_deck, opp_deck, k,
                       &mut prng).unwrap_or(0)
        };
        if game.act(idx).is_err() { break; }
        n += 1;
        // record the redacted afterstate for the acting seat
        if !handcrafted || seat == learner {
            let after = game.core_game().observe(seat);
            let pg = game.core_game().in_pregame();
            rows[seat_idx(seat)].push(features(&after, pg, tables));
        }
    }
    let result = game.result();
    let Some(res) = result else { return 0; };  // capped -> no rows
    let z = |s: PlayerId| -> f32 {
        match res {
            penta::GameResult::Winner { winner, .. } =>
                if winner == s { 1.0 } else { 0.0 },
            penta::GameResult::Draw => 0.5,
        }
    };
    let mut count = 0;
    let seats = if handcrafted { vec![learner] }
                else { vec![PlayerId::One, PlayerId::Two] };
    for s in seats {
        let zv = z(s);
        for row in &rows[seat_idx(s)] {
            x_out.extend_from_slice(row);
            y_out.push(zv);
            count += 1;
        }
    }
    count
}
