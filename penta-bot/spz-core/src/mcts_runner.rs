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

fn seat_idx(s: PlayerId) -> usize { if s == PlayerId::One { 0 } else { 1 } }

/// Outcome of one native ISMCTS game.
pub struct GameOutcome {
    /// 1.0 win / 0.5 draw / 0.0 loss for OUR seat. A game that reached the
    /// per-game decision cap without a natural result is scored 0.0 (loss)
    /// with `capped = true`, so no game runs forever and the gate win%
    /// stays bounded. `None` only when the game could not even start.
    pub score: Option<f64>,
    /// Number of training rows appended (0 unless `record` and finished
    /// naturally -- capped games record no rows, their target is unknown).
    pub rows: usize,
    /// Whether the game hit the decision cap instead of finishing.
    pub capped: bool,
}

/// Play one full ISMCTS game. `d1`/`d2` are the p1/p2 decklists; `our_p1`
/// selects which seat WE drive (the other is the handcrafted opponent).
/// When `record`, appends (redacted afterstate features, z) rows for OUR
/// seat into `x_out`/`y_out`.
#[allow(clippy::too_many_arguments)]
pub fn play_ismcts_game(
    policy: &Policy, tables: &Tables, decks: &Decks,
    book: &crate::decks::DeckBook,
    d1: &str, d2: &str, our_p1: bool, seed: u64, cfg: &MctsConfig,
    classify: bool,
    record: bool, x_out: &mut Vec<f32>, y_out: &mut Vec<f32>,
) -> GameOutcome {
    let our_seat = if our_p1 { PlayerId::One } else { PlayerId::Two };
    let opp_seat = if our_p1 { PlayerId::Two } else { PlayerId::One };
    // OUR deck is known (we pilot it). The opponent's is CLASSIFIED from
    // its revealed cards, recomputed each move as more is revealed --
    // never taken from d1/d2.
    //
    // This used to read `decks.get(d1)` / `decks.get(d2)` directly, which
    // handed the search the true archetype in two places at once: the
    // belief block's unseen-pool counts AND the determinization sampler
    // that decides which cards can be in the opponent's hand. The AAC path
    // classifies, so any ISMCTS number measured against the true decklists
    // was not comparable to it -- it was a strictly easier game.
    let my_deck = if our_p1 { d1 } else { d2 };

    let cap = cfg.max_decisions.max(1);
    // Same wall-clock guard self-play uses. A gate waits for every thread,
    // so one pathological game stalls the whole evaluation -- observed as a
    // 12-thread run sitting at load average 2.4 while a single straggler
    // ground on. A timed-out game is reported `capped`, exactly like one
    // that reached the decision cap.
    // AZ_GATE_SECS, falling back to AZ_GAME_SECS. A gate game runs against
    // the built-in bot and lasts longer than a self-play game, so one
    // budget does not fit both: at 45s a gate truncated 28 of 120 games and
    // read 10.8% as-loss against 22.5% as-draw -- a 12-point band opened by
    // the guard itself. The budget exists to stop a straggler stalling a
    // whole batch, not to decide games.
    let budget = std::env::var("AZ_GATE_SECS").ok()
        .or_else(|| std::env::var("AZ_GAME_SECS").ok())
        .and_then(|v| v.parse::<u64>().ok()).unwrap_or(0);
    let started = std::time::Instant::now();
    let mut game = match BotGame::new(d1, d2, Opponent::Handcrafted,
                                      opp_seat, seed) {
        Ok(g) => g,
        Err(_) => return GameOutcome { score: None, rows: 0, capped: false },
    };
    let mut prng = SplitMix64::new(
        seed.wrapping_mul(0x9E3779B97F4A7C15) ^ 0xD1B5);

    let mut rows: Vec<Vec<f32>> = Vec::new();
    let mut n = 0usize;
    while game.result().is_none() && n < cap {
        if budget > 0 && started.elapsed().as_secs() >= budget {
            break;
        }
        // With a handcrafted opponent, only OUR seat is ever the decider.
        let Some(seat) = game.decision_seat() else { break };
        debug_assert_eq!(seat, our_seat);
        // Re-classify each move: the opponent's revealed cards accumulate,
        // so the guess sharpens as the game goes on (measured 0% accurate
        // on turn one, ~90% by turn eight).
        let obs = game.core_game().observe(our_seat);
        let opp_seen = crate::det_runner::seen_defs(
            &obs, if our_seat == PlayerId::One { PlayerId::Two }
                  else { PlayerId::One }, false);
        let mut scratch = std::collections::HashMap::new();
        // classify=false takes the TRUE opponent decklist. That is a deck
        // oracle the AAC path never gets, so it is only for isolating
        // whether classification is what degrades the search -- never for
        // a number quoted against the 1-ply baseline.
        let true_opp = if our_p1 { d2 } else { d1 };
        let opp_deck = if classify {
            crate::aac::classify_deck(book, &opp_seen, &mut scratch)
                .unwrap_or(my_deck)
        } else { true_opp };
        let mut deck_slots: [Vec<i32>; 2] = [Vec::new(), Vec::new()];
        let mi = if our_seat == PlayerId::One { 0 } else { 1 };
        deck_slots[mi] = tables.deck_slots(book.counts(my_deck));
        deck_slots[1 - mi] = tables.deck_slots(book.counts(opp_deck));
        let search = Ismcts {
            policy, decks, deck_slots: &deck_slots,
            my_deck, opp_deck, our_seat, cfg: cfg.clone(),
        };
        // Wide decisions are scored once by the policy head instead of
        // searched. Without this the gate spent nearly all its time on the
        // rare 500-candidate decision: 12x slower PER GAME than self-play,
        // which has always had this cap.
        let acts = penta::protocol::protocol_actions(&obs);
        let wide = cfg.max_actions > 0 && acts.len() > cfg.max_actions;
        let best = if wide {
            match policy.fast_head.as_ref() {
                Some(fh) => {
                    let state = features(&obs, game.core_game().in_pregame(),
                                         tables, &deck_slots);
                    let pre = fh.state_pre(&state);
                    let enc = crate::action_feat::encode_all(&acts, &obs,
                                                             tables);
                    let sc = fh.scores(&pre, &enc, acts.len());
                    let mut b = 0usize;
                    for (j, v) in sc.iter().enumerate() {
                        if *v > sc[b] { b = j; }
                    }
                    b
                }
                None => 0,
            }
        } else {
            search.search(&game, &mut prng).0
        };
        if game.act(best).is_err() { break; }
        n += 1;
        if record {
            let after = game.core_game().observe(our_seat);
            let pg = game.core_game().in_pregame();
            rows.push(features(&after, pg, tables, &deck_slots));
        }
    }

    let Some(res) = game.result() else {
        // Straggler guard: reached the decision cap without a natural result.
        // Score it as a LOSS so the game is counted (bounded win%) and record
        // no training rows (the true outcome is unknown).
        return GameOutcome { score: Some(0.0), rows: 0, capped: true };
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
    GameOutcome { score: Some(z), rows: count, capped: false }
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
            fast_head: None,
            max_eval: 999,
        }
    }

    fn cfg(iters: usize, m: usize) -> MctsConfig {
        MctsConfig { iters, redeterminize_m: m, ..Default::default() }
    }

    // --- The default opponent model is the handcrafted engine bot, and a
    // full game against it completes with a legal-only line (an illegal
    // opponent move would abort the game before a natural result). ---------
    #[test]
    fn handcrafted_opponent_game_completes() {
        use crate::mcts::OpponentModel;
        let policy = make_policy(-4.0);
        let decks = crate::decks::load(DECKLISTS).expect("decks");
        let book = crate::decks::DeckBook::load(DECKLISTS).expect("book");
        let mut c = cfg(24, 1);
        assert_eq!(c.opponent, OpponentModel::Handcrafted,
                   "handcrafted must be the default in-tree opponent");
        c.max_decisions = 400;
        let mut x = Vec::new();
        let mut y = Vec::new();
        let out = play_ismcts_game(&policy, &policy.tables, &decks, &book,
            "Goblins", "The Deck", true, 42, &c, true, false, &mut x, &mut y);
        let s = out.score.expect("game must finish");
        assert!(s == 0.0 || s == 0.5 || s == 1.0);
        assert!(!out.capped, "a 400-cap Goblins game should finish naturally");
    }

    // --- The Greedy opponent path still works behind the flag. -------------
    #[test]
    fn greedy_opponent_path_completes() {
        use crate::mcts::OpponentModel;
        let policy = make_policy(-4.0);
        let decks = crate::decks::load(DECKLISTS).expect("decks");
        let book = crate::decks::DeckBook::load(DECKLISTS).expect("book");
        let mut c = cfg(16, 1);
        c.opponent = OpponentModel::Greedy;
        let mut x = Vec::new();
        let mut y = Vec::new();
        let out = play_ismcts_game(&policy, &policy.tables, &decks, &book,
            "Goblins", "The Deck", true, 7, &c, true, false, &mut x, &mut y);
        let s = out.score.expect("game must finish");
        assert!(s == 0.0 || s == 0.5 || s == 1.0);
    }

    // --- The per-game decision cap terminates a game deterministically:
    // with a tiny cap the game cannot finish naturally, so it is scored as a
    // capped loss (0.0) rather than running forever. -----------------------
    #[test]
    fn move_cap_terminates_long_game() {
        let policy = make_policy(-4.0);
        let decks = crate::decks::load(DECKLISTS).expect("decks");
        let book = crate::decks::DeckBook::load(DECKLISTS).expect("book");
        let mut c = cfg(8, 1);
        c.max_decisions = 3;  // far too few OUR decisions to end a game
        let mut x = Vec::new();
        let mut y = Vec::new();
        let out = play_ismcts_game(&policy, &policy.tables, &decks, &book,
            "Goblins", "The Deck", true, 42, &c, true, true, &mut x, &mut y);
        assert_eq!(out.score, Some(0.0), "capped game scores as a loss");
        assert!(out.capped, "hitting the cap must flag `capped`");
        assert_eq!(out.rows, 0, "a capped game records no training rows");
        assert!(y.is_empty(), "no targets emitted for a capped game");
    }

    // A full native ISMCTS game completes and returns a plausible score.
    #[test]
    fn game_completes_and_scores() {
        let policy = make_policy(-4.0);
        let decks = crate::decks::load(DECKLISTS).expect("decks");
        let book = crate::decks::DeckBook::load(DECKLISTS).expect("book");
        let mut x = Vec::new();
        let mut y = Vec::new();
        let out = play_ismcts_game(&policy, &policy.tables, &decks, &book,
            "Goblins", "The Deck", true, 42, &cfg(24, 1), true, true, &mut x, &mut y);
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
        let book = crate::decks::DeckBook::load(DECKLISTS).expect("book");
        let mut x = Vec::new();
        let mut y = Vec::new();
        let out = play_ismcts_game(&policy, &policy.tables, &decks, &book,
            "Sligh", "White Weenie", false, 7, &cfg(32, 4), true, true,
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
        let book = crate::decks::DeckBook::load(DECKLISTS).expect("book");
        let n = 6;
        let mut wins = 0; let mut draws = 0; let mut finished = 0;
        let mut x = Vec::new(); let mut y = Vec::new();
        for g in 0..n {
            let out = play_ismcts_game(&policy, &policy.tables, &decks, &book,
                "Goblins", "The Deck", g % 2 == 0, 100 + g as u64,
                &cfg(16, 1), true, false, &mut x, &mut y);
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
