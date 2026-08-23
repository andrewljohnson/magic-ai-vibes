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

fn seat_idx(s: PlayerId) -> usize { if s == PlayerId::One { 0 } else { 1 } }
fn flip(s: PlayerId) -> PlayerId {
    if s == PlayerId::One { PlayerId::Two } else { PlayerId::One }
}

/// One searched decision.
pub struct Record {
    /// `m * feat` candidate afterstate features, row-major.
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
    let mut game = match BotGame::new(d1, d2, Opponent::External,
                                      PlayerId::Two, seed) {
        Ok(g) => g,
        Err(_) => return Episode { records: Vec::new(), result: -1,
                                   decisions: 0 },
    };
    let mut prng = SplitMix64::new(seed ^ 0xA17E);
    let feat = tables.size;
    let mut records = Vec::new();
    let mut scratch = std::collections::HashMap::new();
    let mut n = 0usize;

    while game.result().is_none() && n < MAX_DECISIONS {
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

        if !wide && !visits.is_empty() {
            let pregame = game.core_game().in_pregame();
            let obs_other = game.core_game().observe(flip(seat));
            let mut privileged = features(&obs, pregame, tables, &deck_slots);
            privileged.extend(features(&obs_other, pregame, tables,
                                       &deck_slots));
            let m = acts.len().min(visits.len());
            let mut cand: Vec<f32> = Vec::with_capacity(m * feat);
            let mut ok = true;
            for i in 0..m {
                let mut copy = game.clone();
                if copy.act(i).is_err() { ok = false; break; }
                let after = copy.core_game().observe(seat);
                let pg = copy.core_game().in_pregame();
                cand.extend(features(&after, pg, tables, &deck_slots));
            }
            if ok {
                records.push(Record {
                    cand, m, visits: visits[..m].to_vec(),
                    chosen: best.min(m - 1), privileged, seat: mi as u8,
                });
            }
        }
        if game.act(best).is_err() { break; }
        n += 1;
    }

    let result = match game.result() {
        None => -1,
        Some(penta::GameResult::Draw) => 2,
        Some(penta::GameResult::Winner { winner, .. }) =>
            if winner == PlayerId::One { 0 } else { 1 },
    };
    Episode { records, result, decisions: n }
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
        const CATALOG: &str = "../pinned-catalog.json";
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
        let policy = if std::env::var("AZ_FAST").as_deref() == Ok("1") {
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
            leaf_playout: false, leaf_blend: false, redeterminize_m: 1,
            use_dominance: std::env::var("AZ_DOM").as_deref() != Ok("0"),
            opponent: if opp == "handcrafted" {
                crate::mcts::OpponentModel::Handcrafted
            } else { crate::mcts::OpponentModel::Greedy },
            max_decisions: MAX_DECISIONS,
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
