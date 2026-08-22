//! Native AAC self-play: the trajectory-emitting hot loop.
//!
//! The Python trainer (`aac_torch_par.py`) spends its wall clock forking a
//! game per candidate action, round-tripping every afterstate through
//! protocol JSON, and re-extracting features in numpy -- per decision, per
//! candidate. This module does that natively: fork with `BotGame::clone`,
//! observe TYPED, featurize with `extract::features`, score with the AAC
//! actor, sample, and emit exactly the record the existing PPO+GAE update
//! consumes. Python keeps the learner (torch is tiny and fast); only
//! GENERATION moves here.
//!
//! Row semantics MIRROR `_worker_episode` in aac_torch_par.py line for
//! line -- same forced-action skip, same per-decision belief context, same
//! privileged (both-seat) critic input, same tempo shaping, same softmax.
//! `aac_lockstep.py` replays a native episode's chosen actions through the
//! Python path and compares features/priv/rewards bit-for-bit.

use penta::protocol::{protocol_actions, BotGame, Opponent};
use penta::{Action, PlayerId, PlayerObservation, Step};

use crate::decks::DeckBook;
use crate::det_runner::seen_defs;
use crate::extract::features;
use crate::prng::SplitMix64;
use crate::tables::Tables;

/// Matches aac_selfplay.MAX_DECISIONS -- the Python episode cap.
pub const MAX_DECISIONS: usize = 600;

// Tempo shaping constants, from aac_selfplay.py.
const K_LIFE: f64 = 0.02;
const K_POWER: f64 = 0.01;
const K_IDLE: f64 = 0.01;

fn seat_idx(s: PlayerId) -> usize { if s == PlayerId::One { 0 } else { 1 } }
fn flip(s: PlayerId) -> PlayerId {
    if s == PlayerId::One { PlayerId::Two } else { PlayerId::One }
}

/// The AAC ACTOR: an afterstate scorer, `feat -> hidden -> 1`, tanh hidden
/// and NO output squash. Deliberately distinct from `net::Mlp`, whose
/// sigmoid belongs to the determinized value net.
///
/// Arithmetic mirrors `_np_logits` in aac_torch_par.py: f32 weights and
/// f32 features widened to f64, accumulated in f64. (numpy's BLAS matmul
/// blocks its summation, so the last ulp or two of a dot product can
/// differ from this straight loop -- see `aac_lockstep.py`, which holds
/// features to bit-equality and logits to a tolerance.)
pub struct Actor {
    pub inputs: usize,
    pub hidden: usize,
    pub w1: Vec<f64>, // hidden x inputs, row-major (torch f1.weight)
    pub b1: Vec<f64>,
    pub w2: Vec<f64>, // hidden (torch f2.weight, squeezed)
    pub b2: f64,
}

impl Actor {
    pub fn new(inputs: usize, hidden: usize, w1: Vec<f64>, b1: Vec<f64>,
               w2: Vec<f64>, b2: f64) -> Result<Self, String> {
        if w1.len() != hidden * inputs {
            return Err(format!("w1 has {} entries, expected {hidden}x{inputs}",
                               w1.len()));
        }
        if b1.len() != hidden || w2.len() != hidden {
            return Err(format!("b1/w2 length {}/{} != hidden {hidden}",
                               b1.len(), w2.len()));
        }
        Ok(Actor { inputs, hidden, w1, b1, w2, b2 })
    }

    /// Unsquashed logit for one afterstate feature row.
    pub fn score(&self, x: &[f32]) -> f64 {
        debug_assert_eq!(x.len(), self.inputs);
        let mut out = self.b2;
        for h in 0..self.hidden {
            let row = &self.w1[h * self.inputs..(h + 1) * self.inputs];
            let mut pre = self.b1[h];
            for (w, xi) in row.iter().zip(x) {
                pre += w * f64::from(*xi);
            }
            out += self.w2[h] * pre.tanh();
        }
        out
    }
}

/// One recorded decision -- the AAC training row.
pub struct Record {
    /// `m * feat` candidate afterstate features, row-major.
    pub cand: Vec<f32>,
    pub m: usize,
    pub chosen: usize,
    pub logp_old: f64,
    /// Unsquashed actor logits for the candidates (diagnostics/lockstep).
    pub logits: Vec<f64>,
    /// `2 * feat`: [features(acting seat), features(other seat)].
    pub privileged: Vec<f32>,
    pub reward: f64,
    /// 0 = p1, 1 = p2.
    pub seat: u8,
}

pub struct Episode {
    pub records: Vec<Record>,
    /// 0 = p1 wins, 1 = p2 wins, 2 = draw, -1 = unfinished (hit the cap).
    pub result: i8,
    pub decisions: usize,
    /// Widest legal-action list seen; the afterstate expansion is linear
    /// in this, so it is the throughput outlier to watch.
    pub max_actions: usize,
}

/// Best-overlap built-in deck for a list of seen definitions -- a port of
/// `hosted_policy.classify_deck`. Ties keep the FIRST deck in decklist
/// file order (strict `>`), which is why `DeckBook` preserves that order.
pub fn classify_deck<'a>(book: &'a DeckBook, seen: &[u16]) -> Option<&'a str> {
    if seen.is_empty() {
        return None;
    }
    let mut best: Option<&str> = None;
    let mut best_score = -1.0f64;
    for (name, deck) in &book.order {
        let mut pool = deck.clone();
        let mut hits = 0usize;
        for d in seen {
            if let Some(c) = pool.get_mut(d) {
                if *c > 0 {
                    *c -= 1;
                    hits += 1;
                }
            }
        }
        let score = hits as f64 / seen.len() as f64;
        if score > best_score {
            best_score = score;
            best = Some(name.as_str());
        }
    }
    best
}

/// Seat-indexed decklist counts for one decision's belief block -- a port
/// of `hosted_policy.belief_deck_context`. HONEST: our own deck is known
/// (we pilot it); the opponent's is CLASSIFIED from its revealed cards
/// only, never from its hidden hand.
fn belief_context(tables: &Tables, book: &DeckBook, obs: &PlayerObservation,
                  our_deck: &str) -> [Vec<i32>; 2] {
    if !tables.belief {
        return [Vec::new(), Vec::new()];
    }
    let mi = seat_idx(obs.viewer);
    let opp_seen = seen_defs(obs, flip(obs.viewer), false);
    let opp_name = classify_deck(book, &opp_seen).unwrap_or(our_deck);
    let mut out = [Vec::new(), Vec::new()];
    out[mi] = tables.deck_slots(book.counts(our_deck));
    out[1 - mi] = tables.deck_slots(book.counts(opp_name));
    out
}

/// Printed power of the creatures `seat` controls -- a port of
/// `aac_selfplay.own_board_power` (catalog power, 0 for non-creatures).
fn own_board_power(obs: &PlayerObservation, seat: PlayerId, t: &Tables) -> f64 {
    let mut total = 0i64;
    for perm in &obs.battlefield {
        if perm.controller == seat {
            total += t.power.get(&perm.definition.0).copied().unwrap_or(0);
        }
    }
    total as f64
}

/// Categorical draw mirroring CPython's `random.choices(weights=pi)`:
/// cumulative weights, `u = random() * total`, `bisect_right(cum, u, 0,
/// n-1)`. Written out so `aac_lockstep.py` can reproduce the same index
/// from the same splitmix64 stream.
fn choose_weighted(pi: &[f64], prng: &mut SplitMix64) -> usize {
    let n = pi.len();
    let mut cum = Vec::with_capacity(n);
    let mut acc = 0.0f64;
    for p in pi {
        acc += *p;
        cum.push(acc);
    }
    let u = prng.next_f64() * cum[n - 1];
    // bisect_right(cum, u, 0, n - 1): first index whose cum > u, with the
    // search capped at the last slot so a rounding overshoot cannot walk
    // off the end (CPython passes hi = n - 1 for exactly this reason).
    let mut lo = 0usize;
    let mut hi = n - 1;
    while lo < hi {
        let mid = (lo + hi) / 2;
        if u < cum[mid] { hi = mid; } else { lo = mid + 1; }
    }
    lo
}

/// Score every candidate afterstate for one decision.
///
/// Returns the `m * feat` feature block and the actor logits. This is THE
/// hot path: cost is linear in the legal-action count, and a handful of
/// decisions (mass blocker/damage assignment) carry action lists orders of
/// magnitude wider than the ~5 typical -- which is what made the Python
/// worker look like it had "hung" and tripped the watchdog.
fn expand_candidates(game: &BotGame, seat: PlayerId, m: usize,
                     tables: &Tables, deck_ctx: &[Vec<i32>; 2],
                     actor: &Actor) -> Option<(Vec<f32>, Vec<f64>)> {
    let feat = tables.size;
    let mut cand: Vec<f32> = Vec::with_capacity(m * feat);
    let mut logits: Vec<f64> = Vec::with_capacity(m);
    for i in 0..m {
        let mut copy = game.clone();
        if copy.act(i).is_err() {
            return None;
        }
        let after = copy.core_game().observe(seat);
        let after_pg = copy.core_game().in_pregame();
        let row = features(&after, after_pg, tables, deck_ctx);
        logits.push(actor.score(&row));
        cand.extend_from_slice(&row);
    }
    Some((cand, logits))
}

/// Play one AAC self-play episode and emit its training records.
///
/// `handcrafted` puts the engine's built-in bot in the non-learner seat
/// and records only `learner`'s decisions; otherwise both seats are driven
/// by the actor and both are recorded (the curriculum's self-play half).
///
/// `max_actions` caps the afterstate expansion: a decision offering more
/// than that many legal actions is played greedily from the first
/// `max_actions` candidates and NOT recorded. Set it to 0 to expand
/// everything (bit-identical to the Python path -- what `aac_lockstep.py`
/// checks). See `aac_native.py` for why a finite cap is the default.
#[allow(clippy::too_many_arguments)]
pub fn play_episode(actor: &Actor, tables: &Tables, book: &DeckBook,
                    d1: &str, d2: &str, seed: u64, temperature: f64,
                    handcrafted: bool, learner: PlayerId,
                    max_actions: usize) -> Episode {
    let mut game = if handcrafted {
        BotGame::new(d1, d2, Opponent::Handcrafted, flip(learner), seed)
    } else {
        BotGame::new(d1, d2, Opponent::External, PlayerId::Two, seed)
    }.expect("game");
    // Same seed derivation as the Python worker's `random.Random(seed ^
    // 0x9E37)`; the generator itself is splitmix64 so a Python reference
    // can mirror the stream (CPython's Mersenne Twister cannot be).
    let mut prng = SplitMix64::new(seed ^ 0x9E37);
    let temp = temperature.max(1e-6);

    let mut records: Vec<Record> = Vec::new();
    let mut prev_opp_life: [Option<i64>; 2] = [None, None];
    let mut prev_own_power: [Option<f64>; 2] = [None, None];
    let mut n = 0usize;
    let mut max_seen = 0usize;

    while game.result().is_none() && n < MAX_DECISIONS {
        let Some(seat) = game.decision_seat() else { break };
        let si = seat_idx(seat);
        let record_this = !handcrafted || seat == learner;
        let obs = game.core_game().observe(seat);
        let acts = protocol_actions(&obs);
        max_seen = max_seen.max(acts.len());
        // Mirrors the Python worker: an unrecorded seat, or a forced
        // single action, is played as index 0 without a training row.
        if !record_this || acts.len() == 1 {
            if game.act(0).is_err() { break; }
            n += 1;
            continue;
        }

        let my_deck = if seat == PlayerId::One { d1 } else { d2 };
        let deck_ctx = belief_context(tables, book, &obs, my_deck);

        // A pathologically wide decision: expand only a prefix, play the
        // best of it, and emit NO row. Recording a truncated candidate
        // set would train the actor on a softmax over actions it never
        // really chose between, so the row is dropped instead.
        let wide = max_actions > 0 && acts.len() > max_actions;
        if wide {
            let Some((_, logits)) = expand_candidates(
                &game, seat, max_actions, tables, &deck_ctx, actor) else {
                break;
            };
            let mut best = 0usize;
            for (i, l) in logits.iter().enumerate() {
                if *l > logits[best] { best = i; }
            }
            if game.act(best).is_err() { break; }
            n += 1;
            // Keep the tempo baselines current so the NEXT recorded row's
            // shaped reward is not credited with this decision's swing.
            let after = game.core_game().observe(seat);
            prev_opp_life[si] = Some(i64::from(after.life_totals[1 - si]));
            prev_own_power[si] = Some(own_board_power(&after, seat, tables));
            continue;
        }

        // Privileged critic input: BOTH seats' redacted observations.
        let pregame = game.core_game().in_pregame();
        let obs_other = game.core_game().observe(flip(seat));
        let mut privileged = features(&obs, pregame, tables, &deck_ctx);
        privileged.extend(features(&obs_other, pregame, tables, &deck_ctx));

        let m = acts.len();
        let Some((cand, logits)) = expand_candidates(
            &game, seat, m, tables, &deck_ctx, actor) else { break };

        // softmax(logits / temp) over the candidates, then sample.
        let mut z: Vec<f64> = logits.iter().map(|l| l / temp).collect();
        let zmax = z.iter().copied().fold(f64::NEG_INFINITY, f64::max);
        let mut sum = 0.0f64;
        for v in &mut z {
            *v = (*v - zmax).exp();
            sum += *v;
        }
        let pi: Vec<f64> = z.iter().map(|v| v / sum).collect();
        let c = choose_weighted(&pi, &mut prng);
        let logp_old = (pi[c] + 1e-12).ln();

        // Tempo shaping, read from the PRE-action observation.
        let idle = matches!(acts[c], Action::PassPriority)
            && obs.step == Step::PrecombatMain
            && obs.active_player == seat;

        if game.act(c).is_err() { break; }
        n += 1;

        let after = game.core_game().observe(seat);
        let opp_life = i64::from(after.life_totals[1 - si]);
        let power = own_board_power(&after, seat, tables);
        let mut reward = if idle { -K_IDLE } else { 0.0 };
        if let (Some(pl), Some(pp)) = (prev_opp_life[si], prev_own_power[si]) {
            reward += K_LIFE * (pl - opp_life).max(0) as f64;
            reward += K_POWER * (power - pp).max(0.0);
        }
        prev_opp_life[si] = Some(opp_life);
        prev_own_power[si] = Some(power);

        records.push(Record { cand, m, chosen: c, logp_old, logits,
                              privileged, reward, seat: si as u8 });
    }

    let result = match game.result() {
        None => -1,
        Some(penta::GameResult::Draw) => 2,
        Some(penta::GameResult::Winner { winner, .. }) =>
            if winner == PlayerId::One { 0 } else { 1 },
    };
    Episode { records, result, decisions: n, max_actions: max_seen }
}

/// One honest gate game: the actor plays ARGMAX from its own redacted
/// observations against the engine's handcrafted bot. Returns
/// (score, decisions, widest action list), where score is the learner's
/// (win 1, draw 0.5, unfinished 0.5 -- matching `gate_belief`, which
/// scores a capped game as a draw).
///
/// The Python gate runs its forward in torch f32 while this runs f64, so
/// a candidate pair separated by less than an f32 ulp could in principle
/// break the other way; `aac_lockstep.py` measures how often that happens
/// (it is rare, and confined to near-exact ties).
pub fn gate_game(actor: &Actor, tables: &Tables, book: &DeckBook,
                 d1: &str, d2: &str, seed: u64, learner: PlayerId,
                 max_actions: usize) -> (f64, usize, usize) {
    let mut game = BotGame::new(d1, d2, Opponent::Handcrafted, flip(learner),
                                seed).expect("game");
    let mut n = 0usize;
    let mut max_seen = 0usize;
    while game.result().is_none() && n < MAX_DECISIONS {
        let Some(seat) = game.decision_seat() else { break };
        let obs = game.core_game().observe(seat);
        let acts = protocol_actions(&obs);
        if acts.is_empty() { break; }
        max_seen = max_seen.max(acts.len());
        let idx = if acts.len() == 1 {
            0
        } else {
            let my_deck = if seat == PlayerId::One { d1 } else { d2 };
            let deck_ctx = belief_context(tables, book, &obs, my_deck);
            let m = if max_actions > 0 { acts.len().min(max_actions) }
                    else { acts.len() };
            let Some((_, logits)) = expand_candidates(
                &game, seat, m, tables, &deck_ctx, actor) else { break };
            let mut best = 0usize;
            for (i, l) in logits.iter().enumerate() {
                if *l > logits[best] { best = i; }
            }
            best
        };
        if game.act(idx).is_err() { break; }
        n += 1;
    }
    let score = match game.result() {
        None | Some(penta::GameResult::Draw) => 0.5,
        Some(penta::GameResult::Winner { winner, .. }) =>
            if winner == learner { 1.0 } else { 0.0 },
    };
    (score, n, max_seen)
}
