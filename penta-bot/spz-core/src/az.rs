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
