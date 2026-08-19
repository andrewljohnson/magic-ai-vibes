//! Native SO-ISMCTS game runner. Rust plays FULL games where OUR seat
//! chooses via the `mcts::Ismcts` search and the opponent is the engine's
//! built-in handcrafted bot (`BotGame` with `Opponent::Handcrafted`) --
//! entirely in Rust, no per-move Python/JSON crossing. Mirrors the
//! structure of `det_runner::play_game`.
//!
//! Two uses of the SAME runner:
//!  * GATE mode (`record=false`): return the game score (1/0.5/0 for our
//!    seat), used to tally wins/draws over a deck matchup batch.
//!  * TRAINING-ROW mode (`record=true`): emit redacted afterstate features
//!    for OUR seat with TD(1) target z (the game outcome), matching the
//!    det handcrafted row semantics.

use penta::protocol::{BotGame, Opponent};
use penta::PlayerId;

use crate::decks::Decks;
use crate::extract::features;
use crate::mcts::{Ismcts, MctsConfig};
use crate::policy::Policy;
use crate::prng::SplitMix64;
use crate::tables::Tables;

/// Hard cap on OUR decisions in a single game (the opponent's plies are
/// auto-run by the engine between our decisions and are not counted).
const MAX_OUR_DECISIONS: usize = 800;

fn seat_idx(s: PlayerId) -> usize { if s == PlayerId::One { 0 } else { 1 } }

/// Outcome of one native ISMCTS game.
pub struct GameOutcome {
    /// 1.0 win / 0.5 draw / 0.0 loss for OUR seat; None if the game was
    /// capped before finishing.
    pub score: Option<f64>,
    /// Number of training rows appended (0 unless `record` and finished).
    pub rows: usize,
}

/// Play one full ISMCTS game. `d1`/`d2` are the p1/p2 decklists; `our_p1`
/// selects which seat WE drive (the other is the handcrafted opponent).
/// When `record`, appends (redacted afterstate features, z) rows for OUR
/// seat into `x_out`/`y_out`.
#[allow(clippy::too_many_arguments)]
pub fn play_ismcts_game(
    policy: &Policy, tables: &Tables, decks: &Decks,
    d1: &str, d2: &str, our_p1: bool, seed: u64, cfg: &MctsConfig,
    record: bool, x_out: &mut Vec<f32>, y_out: &mut Vec<f32>,
) -> GameOutcome {
    let our_seat = if our_p1 { PlayerId::One } else { PlayerId::Two };
    let opp_seat = if our_p1 { PlayerId::Two } else { PlayerId::One };
    // Seat-relative deck names for the determinization sampler.
    let (my_deck, opp_deck) = if our_p1 { (d1, d2) } else { (d2, d1) };
    // Seat-indexed decklist counts for the belief block (p1 plays d1).
    let empty = std::collections::HashMap::new();
    let deck_slots: [Vec<i32>; 2] = [
        tables.deck_slots(decks.get(d1).unwrap_or(&empty)),
        tables.deck_slots(decks.get(d2).unwrap_or(&empty)),
    ];

    let mut game = match BotGame::new(d1, d2, Opponent::Handcrafted,
                                      opp_seat, seed) {
        Ok(g) => g,
        Err(_) => return GameOutcome { score: None, rows: 0 },
    };
    let search = Ismcts {
        policy, decks, deck_slots: &deck_slots,
        my_deck, opp_deck, our_seat, cfg: cfg.clone(),
    };
    let mut prng = SplitMix64::new(
        seed.wrapping_mul(0x9E3779B97F4A7C15) ^ 0xD1B5);

    let mut rows: Vec<Vec<f32>> = Vec::new();
    let mut n = 0usize;
    while game.result().is_none() && n < MAX_OUR_DECISIONS {
        // With a handcrafted opponent, only OUR seat is ever the decider.
        let Some(seat) = game.decision_seat() else { break };
        debug_assert_eq!(seat, our_seat);
        let (best, _visits) = search.search(&game, &mut prng);
        if game.act(best).is_err() { break; }
        n += 1;
        if record {
            let after = game.core_game().observe(our_seat);
            let pg = game.core_game().in_pregame();
            rows.push(features(&after, pg, tables, &deck_slots));
        }
    }

    let Some(res) = game.result() else {
        return GameOutcome { score: None, rows: 0 };  // capped
    };
    let z = match res {
        penta::GameResult::Winner { winner, .. } =>
            if winner == our_seat { 1.0 } else { 0.0 },
        penta::GameResult::Draw => 0.5,
    };
    let mut count = 0;
    if record {
        for row in &rows {
            x_out.extend_from_slice(row);
            y_out.push(z as f32);
            count += 1;
        }
    }
    let _ = seat_idx(our_seat);
    GameOutcome { score: Some(z), rows: count }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::net::Mlp;

    const CATALOG: &str = "../pinned-catalog.json";
    const DECKLISTS: &str = "../builtin-decklists.json";

    fn make_net(inputs: usize, defs: usize, life_w: f64) -> Mlp {
        let mut w1 = vec![0.0f64; inputs];
        w1[5 * defs + 15] = 1.0;
        Mlp { inputs, hidden: 1, w1, b1: vec![0.0],
              w2: vec![life_w], b2: 0.0 }
    }

    fn make_policy(life_w: f64) -> Policy {
        let mut tables = Tables::load(
            &std::fs::read_to_string(CATALOG).expect("catalog"))
            .expect("tables");
        tables.set_belief(false);
        let inputs = tables.size;
        let defs = tables.defs;
        Policy {
            tables,
            value: make_net(inputs, defs, life_w),
            head: make_net(inputs, defs, life_w),
            weight: 0.0,
            search: crate::policy::SearchConfig {
                top_k: 0, playouts: 1, budget: 400, playout_max_eval: 999,
            },
            max_eval: 999,
        }
    }

    fn cfg(iters: usize, m: usize) -> MctsConfig {
        MctsConfig { iters, redeterminize_m: m, ..Default::default() }
    }

    // A full native ISMCTS game completes and returns a plausible score.
    #[test]
    fn game_completes_and_scores() {
        let policy = make_policy(-4.0);
        let decks = crate::decks::load(DECKLISTS).expect("decks");
        let mut x = Vec::new();
        let mut y = Vec::new();
        let out = play_ismcts_game(&policy, &policy.tables, &decks,
            "Goblins", "The Deck", true, 42, &cfg(24, 1), true, &mut x, &mut y);
        let score = out.score.expect("game must finish");
        assert!(score == 0.0 || score == 0.5 || score == 1.0);
        // Rows: one target per row, feature width consistent.
        assert_eq!(y.len(), out.rows);
        if out.rows > 0 {
            assert_eq!(x.len(), out.rows * policy.tables.size);
            for &t in &y { assert!(t == score as f32); }
        }
    }

    // redeterminize_m > 1 also completes and produces sane rows.
    #[test]
    fn redeterminize_m_completes() {
        let policy = make_policy(-4.0);
        let decks = crate::decks::load(DECKLISTS).expect("decks");
        let mut x = Vec::new();
        let mut y = Vec::new();
        let out = play_ismcts_game(&policy, &policy.tables, &decks,
            "Sligh", "White Weenie", false, 7, &cfg(32, 4), true,
            &mut x, &mut y);
        assert!(out.score.is_some(), "game must finish");
        assert_eq!(y.len(), out.rows);
        assert_eq!(x.len(), out.rows * policy.tables.size);
    }

    // A small gate tally over a few games returns plausible counts.
    #[test]
    fn gate_tally_is_plausible() {
        let policy = make_policy(-4.0);
        let decks = crate::decks::load(DECKLISTS).expect("decks");
        let n = 6;
        let mut wins = 0; let mut draws = 0; let mut finished = 0;
        let mut x = Vec::new(); let mut y = Vec::new();
        for g in 0..n {
            let out = play_ismcts_game(&policy, &policy.tables, &decks,
                "Goblins", "The Deck", g % 2 == 0, 100 + g as u64,
                &cfg(16, 1), false, &mut x, &mut y);
            if let Some(s) = out.score {
                finished += 1;
                if s == 1.0 { wins += 1; } else if s == 0.5 { draws += 1; }
            }
        }
        assert!(finished > 0, "some games must finish");
        assert!(wins <= finished && draws <= finished);
        assert!(x.is_empty() && y.is_empty(), "gate mode records no rows");
    }
}
