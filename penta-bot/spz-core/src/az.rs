//! AlphaZero-style self-play: BOTH seats choose by search, and every
//! decision emits the search's visit distribution as a policy target.
//!
//! This is the piece that makes self-play work at all. Plain self-play has
//! no policy improvement operator -- the actor trains toward its own
//! sampled advantage, so nothing ratchets it upward, and measured here it
//! drifts DOWN (52.5% -> 45.0% over 7.7k games). Search is an operator:
//! it produces a better move distribution than the raw actor (measured
//! 53.1% vs 50.7%), and training the actor toward that distribution is
//! what turns self-play into improvement.
//!
//! Per our decision we emit:
//!   * candidate afterstate features (m x feat) -- what the actor scores
//!   * the search's VISIT COUNTS over those candidates -- the policy target
//!   * the seat, so returns can be assigned per side
//! and per episode the outcome z, which is the value target.
//!
//! Both seats are driven by search against `Opponent::External`, so no
//! handcrafted bot is in the loop (see the pure-build rule in AGENTS.md).

use penta::protocol::{protocol_actions, BotGame, Opponent};
use penta::PlayerId;

use crate::decks::{DeckBook, Decks};
use crate::extract::features;
use crate::mcts::{Ismcts, MctsConfig};
use crate::policy::Policy;
use crate::prng::SplitMix64;
use crate::tables::Tables;

pub const MAX_DECISIONS: usize = 600;

/// Draw an index proportional to visit counts (temperature 1).
fn sample_visits(visits: &[u32], prng: &mut SplitMix64) -> usize {
    let total: u64 = visits.iter().map(|&v| v as u64).sum();
    if total == 0 { return 0; }
    let mut r = (prng.next_f64() * total as f64) as u64;
    for (i, &v) in visits.iter().enumerate() {
        let v = v as u64;
        if r < v { return i; }
        r -= v;
    }
    visits.len() - 1
}

fn seat_idx(s: PlayerId) -> usize { if s == PlayerId::One { 0 } else { 1 } }
fn flip(s: PlayerId) -> PlayerId {
    if s == PlayerId::One { PlayerId::Two } else { PlayerId::One }
}

/// One searched decision.
pub struct Record {
    /// `m * action_dim` ACTION encodings, row-major -- what the
    /// factorised policy head scores. Not afterstate features: producing
    /// those costs a game clone each, which is the cost this whole design
    /// removed.
    pub cand: Vec<f32>,
    pub m: usize,
    /// Search visit count per candidate -- normalised, this is the policy
    /// target the actor is trained toward.
    pub visits: Vec<u32>,
    /// Which candidate the search actually played.
    pub chosen: usize,
    /// `2 * feat`, [features(acting seat), features(other)] -- the
    /// privileged critic input, same shape the AAC path emits.
    pub privileged: Vec<f32>,
    pub seat: u8,
}

pub struct Episode {
    pub records: Vec<Record>,
    /// 0 = p1, 1 = p2, 2 = draw, -1 = unfinished (hit the cap).
    pub result: i8,
    pub decisions: usize,
}

/// Play one self-play game with search on both seats.
#[allow(clippy::too_many_arguments)]
pub fn play_episode(policy: &Policy, tables: &Tables, decks: &Decks,
                    book: &DeckBook, d1: &str, d2: &str, seed: u64,
                    cfg: &MctsConfig, max_actions: usize) -> Episode {
    // A WALL-CLOCK BUDGET, not just a decision cap.
    //
    // MAX_DECISIONS bounds how many decisions a game may take, but says
    // nothing about how long each one costs, and a game full of wide
    // decisions can run for minutes. Generation waits for every thread, so
    // ONE such game stalls a whole round: measured round times swung from
    // 33s to 1000s for the same 48 games, with the machine load average at
    // 2.4 while a 12-thread run sat waiting on a single straggler.
    //
    // A timed-out episode is reported as truncated (result -1) and handled
    // by whatever the truncation policy is, exactly like hitting the
    // decision cap.
    let budget = std::env::var("AZ_GAME_SECS").ok()
        .and_then(|v| v.parse::<u64>().ok()).unwrap_or(0);
    let started = std::time::Instant::now();
    let mut timed_out = false;
    let mut game = match BotGame::new(d1, d2, Opponent::External,
                                      PlayerId::Two, seed) {
        Ok(g) => g,
        Err(_) => return Episode { records: Vec::new(), result: -1,
                                   decisions: 0 },
    };
    // Decisions played by sampling before switching to the argmax.
    let temp_decisions: usize = std::env::var("AZ_TEMP_DECISIONS").ok()
        .and_then(|v| v.parse().ok()).unwrap_or(30);
    let mut prng = SplitMix64::new(seed ^ 0xA17E);
    let mut records = Vec::new();
    let mut scratch = std::collections::HashMap::new();
    let mut n = 0usize;

    while game.result().is_none() && n < MAX_DECISIONS {
        if budget > 0 && started.elapsed().as_secs() >= budget {
            timed_out = true;
            break;
        }
        let Some(seat) = game.decision_seat() else { break };
        let obs = game.core_game().observe(seat);
        let acts = protocol_actions(&obs);
        if acts.len() == 1 {
            if game.act(0).is_err() { break; }
            n += 1;
            continue;
        }
        // Wide decisions are played by the prior alone: searching 500
        // candidates costs more than the decision is worth, and the
        // truncated visit vector would be a misleading policy target.
        let wide = max_actions > 0 && acts.len() > max_actions;

        let my_deck = if seat == PlayerId::One { d1 } else { d2 };
        let opp_seen = crate::det_runner::seen_defs(&obs, flip(seat), false);
        let opp_deck = crate::aac::classify_deck(book, &opp_seen, &mut scratch)
            .unwrap_or(my_deck);
        let mi = seat_idx(seat);
        let mut deck_slots: [Vec<i32>; 2] = [Vec::new(), Vec::new()];
        deck_slots[mi] = tables.deck_slots(book.counts(my_deck));
        deck_slots[1 - mi] = tables.deck_slots(book.counts(opp_deck));

        let search = Ismcts {
            policy, decks, deck_slots: &deck_slots,
            my_deck, opp_deck, our_seat: seat, cfg: cfg.clone(),
        };
        let (best, visits) = if wide {
            (0usize, Vec::new())
        } else {
            search.search(&game, &mut prng)
        };

        // SAMPLE the move from the visit distribution; do not play the
        // argmax. AlphaZero plays proportional to visits during self-play
        // (temperature 1) and saves the argmax for evaluation, and the
        // reason shows up starkly at a cold start: with a near-uniform
        // policy the visit counts tie, argmax resolves every tie to the
        // lowest index, and the game walks the same non-advancing action
        // forever. Measured: 0 of 24 self-play games reached a result,
        // while a uniformly random policy finished 10 of 12. Every episode
        // scoring `z = 0.5` gives the value head nothing to learn from,
        // which silently disables half the loop.
        // TEMPERATURE SCHEDULE. AlphaZero samples proportional to visits
        // for the OPENING only, then plays the argmax. We sampled for the
        // whole game, which keeps injecting noise into decided positions --
        // and every state in a game carries that game's outcome as its
        // value label, so a sampled blunder in the endgame mislabels the
        // whole trajectory. Sampling early is exploration; sampling late is
        // just noise in the targets.
        let played = if wide || visits.is_empty() {
            best
        } else if n < temp_decisions {
            sample_visits(&visits, &mut prng)
        } else {
            best
        };

        if !wide && !visits.is_empty() {
            let pregame = game.core_game().in_pregame();
            let obs_other = game.core_game().observe(flip(seat));
            let mut privileged = features(&obs, pregame, tables, &deck_slots);
            privileged.extend(features(&obs_other, pregame, tables,
                                       &deck_slots));
            let m = acts.len().min(visits.len());
            let cand = crate::action_feat::encode_all(&acts[..m], &obs,
                                                      tables);
            records.push(Record {
                cand, m, visits: visits[..m].to_vec(),
                chosen: played.min(m - 1), privileged, seat: mi as u8,
            });
        }
        if game.act(played).is_err() { break; }
        n += 1;
        #[cfg(feature = "prof")]
        if n % 25 == 0 {
            println!("    .. decision {n}, {} searched, last m={}",
                     records.len(), acts.len());
        }
    }

    let result = match game.result() {
        // -2: OUR CLOCK ran out, not the game. A game stopped by the
        // wall-clock budget says nothing about the position -- it is
        // infrastructure giving up -- whereas a game that reached the
        // DECISION cap is the policy failing to close, which is a real
        // property worth penalising. Scoring both the same fed a fifth of
        // the value target as "both players lost" when finish rates sat at
        // 77-85%.
        None if timed_out => -2,
        None => -1,
        Some(penta::GameResult::Draw) => 2,
        Some(penta::GameResult::Winner { winner, .. }) =>
            if winner == PlayerId::One { 0 } else { 1 },
    };
    Episode { records, result, decisions: n }
}


/// MIRROR MATCH: our net on both seats, a different search budget per seat.
///
/// This is the only measurement that asks whether search is a policy
/// improvement operator IN THE DOMAIN THE LOOP TRAINS ON. Every earlier
/// "search is worth +X" number was taken against the built-in bot, where the
/// greedy in-tree model is simply the WRONG model of the opponent -- a best
/// response to the wrong opponent losing to no search is expected and says
/// nothing about self-play, where the real opponent is our own policy.
///
/// Both seats play argmax-of-visits (no sampling, no root noise) so the
/// result is a clean paired comparison of budgets, not of exploration.
///
/// Returns 1.0 if seat one won, 0.0 if seat two won, 0.5 for a draw, and
/// -1.0 for a game that never finished.
#[allow(clippy::too_many_arguments)]
/// Play one game with a SEPARATE net per seat.
///
/// This used to take a single `&Policy` and vary only `iters`, which meant
/// it could not answer "is net A better than net B" at all -- and it was
/// being used for exactly that. A mirror gate built on it played
/// A-vs-A on half the games and B-vs-B on the other half, then combined
/// them as though that were a head-to-head, so it read ~50% no matter how
/// good either net was. See RESULTS.md.
#[allow(clippy::too_many_arguments)]
pub fn play_h2h(policy_p1: &Policy, policy_p2: &Policy,
                tables: &Tables, decks: &Decks,
                book: &DeckBook, d1: &str, d2: &str, seed: u64,
                cfg: &MctsConfig, max_actions: usize,
                iters_p1: usize, iters_p2: usize,
                noise_p1: f64, noise_p2: f64) -> f64 {
    // AZ_H2H_SECS, falling back to AZ_GAME_SECS.
    //
    // Self-play wants a TIGHT per-game budget: a stuck game is wasted
    // generation. A head-to-head wants a LOOSE one, because an unfinished
    // game is DISCARDED, and discarding a third of the games is the guard
    // deciding the measurement. The in-training mirror finished 69 of 100
    // at 120s where the same comparison finished 172 of 200 at 600s, and
    // that mirror is what promotes or reverts a net.
    let budget = std::env::var("AZ_H2H_SECS").ok()
        .and_then(|v| v.parse::<u64>().ok())
        .or_else(|| std::env::var("AZ_GAME_SECS").ok()
                 .and_then(|v| v.parse::<u64>().ok()))
        .unwrap_or(0);
    let started = std::time::Instant::now();
    let mut game = match BotGame::new(d1, d2, Opponent::External,
                                      PlayerId::Two, seed) {
        Ok(g) => g,
        Err(_) => return -1.0,
    };
    let mut prng = SplitMix64::new(seed ^ 0xB2C3);
    let mut scratch = std::collections::HashMap::new();
    let mut n = 0usize;

    while game.result().is_none() && n < MAX_DECISIONS {
        if budget > 0 && started.elapsed().as_secs() >= budget {
            return -1.0;
        }
        let Some(seat) = game.decision_seat() else { break };
        let obs = game.core_game().observe(seat);
        let acts = protocol_actions(&obs);
        if acts.len() == 1 {
            if game.act(0).is_err() { break; }
            n += 1;
            continue;
        }
        let wide = max_actions > 0 && acts.len() > max_actions;
        let my_deck = if seat == PlayerId::One { d1 } else { d2 };
        let opp_seen = crate::det_runner::seen_defs(&obs, flip(seat), false);
        let opp_deck = crate::aac::classify_deck(book, &opp_seen, &mut scratch)
            .unwrap_or(my_deck);
        let mi = seat_idx(seat);
        let mut deck_slots: [Vec<i32>; 2] = [Vec::new(), Vec::new()];
        deck_slots[mi] = tables.deck_slots(book.counts(my_deck));
        deck_slots[1 - mi] = tables.deck_slots(book.counts(opp_deck));

        let p1 = seat == PlayerId::One;
        let mut seat_cfg = cfg.clone();
        seat_cfg.iters = if p1 { iters_p1 } else { iters_p2 };
        // Per-seat root noise. Self-play's targets are generated WITH noise,
        // so measuring the teacher without it measures a search we never
        // actually run.
        seat_cfg.root_noise_frac = if p1 { noise_p1 } else { noise_p2 };
        let policy = if p1 { policy_p1 } else { policy_p2 };

        let search = Ismcts {
            policy, decks, deck_slots: &deck_slots,
            my_deck, opp_deck, our_seat: seat, cfg: seat_cfg,
        };
        let best = if wide { 0usize } else { search.search(&game, &mut prng).0 };
        if game.act(best).is_err() { break; }
        n += 1;
    }

    match game.result() {
        None => -1.0,
        Some(penta::GameResult::Draw) => 0.5,
        Some(penta::GameResult::Winner { winner, .. }) =>
            if winner == PlayerId::One { 1.0 } else { 0.0 },
    }
}

#[cfg(all(test, feature = "prof"))]
mod prof_tests {
    use super::*;

    /// Where does a REAL self-play episode spend its time? The existing
    /// profile in mcts.rs measures one synthetic mid-game position and
    /// predicts ~20ms per decision; the actual az path costs 1.9s. A 95x
    /// gap means that profile does not describe this workload, so measure
    /// this one directly.
    ///
    /// `cargo test --release --features prof az_profile -- --nocapture`
    #[test]
    fn az_profile() {
        let catalog_path = std::env::var("AZ_CATALOG")
            .unwrap_or_else(|_| "../pinned-catalog.json".to_string());
        let CATALOG: &str = &catalog_path;
        const DECKLISTS: &str = "../builtin-decklists.json";
        let value = std::env::var("AZ_VALUE").expect("AZ_VALUE");
        let head = std::env::var("AZ_HEAD").expect("AZ_HEAD");
        let iters: usize = std::env::var("AZ_ITERS")
            .ok().and_then(|v| v.parse().ok()).unwrap_or(8);
        let opp = std::env::var("AZ_OPP").unwrap_or_else(|_| "greedy".into());

        let mut tables = crate::tables::Tables::load(
            &std::fs::read_to_string(CATALOG).unwrap()).unwrap();
        let v = crate::net::Mlp::load(&value).unwrap();
        tables.set_belief(v.inputs > tables.v2_size);
        let policy = Policy {
            tables, value: v, head: crate::net::Mlp::load(&head).unwrap(),
            weight: 0.0,
            search: crate::policy::SearchConfig {
                top_k: 0, playouts: 1, budget: 400, playout_max_eval: 999 },
            max_eval: 999,
            fast_head: None
        };
        // AZ_FAST=1 attaches a randomly-initialised fast policy head. The
        // numbers it produces are meaningless; the POINT is the timing --
        // does replacing per-action simulation with per-action encoding
        // collapse search cost as the 441x microbenchmark predicts?
        // AZ_HEADFILE loads the REAL trained head, so the profile measures
        // exactly what the training bridge runs rather than a stand-in.
        let policy = if let Ok(hf) = std::env::var("AZ_HEADFILE") {
            let fh = crate::action_feat::PolicyHead::load(&hf)
                .expect("policy head");
            println!("  loaded head: hidden={} state={} action={}",
                     fh.hidden, fh.state_dim, fh.action_dim);
            Policy { fast_head: Some(fh), ..policy }
        } else if std::env::var("AZ_FAST").as_deref() == Ok("1") {
            let sd = policy.tables.size;
            let ad = crate::action_feat::width(&policy.tables);
            let h = 64usize;
            let mut seed = 12345u64;
            let mut rnd = || {
                seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1);
                ((seed >> 33) as f64 / (1u64 << 31) as f64 - 0.5) * 0.1
            };
            let fh = crate::action_feat::PolicyHead {
                hidden: h, state_dim: sd, action_dim: ad,
                ws: (0..h * sd).map(|_| rnd()).collect(),
                wa: (0..h * ad).map(|_| rnd()).collect(),
                b1: vec![0.0; h],
                w2: (0..h).map(|_| rnd()).collect(),
                b2: 0.0,
            };
            Policy { fast_head: Some(fh), ..policy }
        } else { policy };
        let decks = crate::decks::load(DECKLISTS).unwrap();
        let book = crate::decks::DeckBook::load(DECKLISTS).unwrap();
        let cfg = MctsConfig {
            iters, c_puct: 1.5, inert: false,
            leaf_playout: false, leaf_blend: false,
            redeterminize_m: std::env::var("AZ_M").ok()
                .and_then(|v| v.parse().ok()).unwrap_or(1),
            use_dominance: std::env::var("AZ_DOM").as_deref() != Ok("0"),
            opponent: if opp == "handcrafted" {
                crate::mcts::OpponentModel::Handcrafted
            } else { crate::mcts::OpponentModel::Greedy },
            max_decisions: MAX_DECISIONS,
            max_depth: std::env::var("AZ_DEPTH").ok()
                .and_then(|v| v.parse().ok()).unwrap_or(400),
            root_noise_frac: std::env::var("AZ_NOISE").ok()
                .and_then(|v| v.parse().ok()).unwrap_or(0.0),
            root_noise_alpha: 1.0,
            max_actions: 0,
        };
        crate::mcts::prof::reset();
        let t0 = std::time::Instant::now();
        let ep = play_episode(&policy, &policy.tables, &decks, &book,
                              "Sligh", "Goblins", 5000, &cfg, 64);
        let total = t0.elapsed().as_nanos() as u64;
        let acc = crate::mcts::prof::snapshot();
        let sum: u64 = acc.iter().sum();
        println!("\n=== AZ episode profile (iters={iters}, opp={opp}) ===");
        println!("total {:.1}s for {} decisions ({} searched)",
                 total as f64 / 1e9, ep.decisions, ep.records.len());
        println!("per searched decision: {:.0} ms",
                 total as f64 / 1e6 / ep.records.len().max(1) as f64);
        let (plies, iters_ran) = crate::mcts::prof::walk();
        let forced = crate::mcts::prof::forced_count();
        println!("  plies {} over {} iterations -> {:.1} plies/iteration",
                 plies, iters_ran, plies as f64 / iters_ran.max(1) as f64);
        println!("  of those plies, {} ({:.0}%) had exactly ONE legal action",
                 forced, 100.0 * forced as f64 / plies.max(1) as f64);
        let (calls, rows) = crate::mcts::prof::counts();
        println!("  prior batches: {}, action-rows: {}, rows/batch: {:.1}",
                 calls, rows, rows as f64 / calls.max(1) as f64);
        println!("  -> {:.2} ms per action-row in action_prior",
                 acc[2] as f64 / 1e6 / rows.max(1) as f64);
        for i in 0..crate::mcts::prof::N {
            println!("  {:>14}: {:>8.2} s ({:>5.1}%)",
                     crate::mcts::prof::NAMES[i], acc[i] as f64 / 1e9,
                     100.0 * acc[i] as f64 / total as f64);
        }
        println!("  {:>14}: {:>8.2} s ({:>5.1}%)", "descent/other",
                 total.saturating_sub(sum) as f64 / 1e9,
                 100.0 * total.saturating_sub(sum) as f64 / total as f64);
    }
}
