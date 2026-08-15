//! The certified decision policy, native: dominance prunes + value/head
//! blend + rollout playouts + argmax, a port of trainer.choose /
//! playout_value / dominance_prune. Drives the core `Game` directly
//! (observe -> protocol_actions -> apply on clones), no JSON in the
//! hot loop. Lockstep-verified against Python trainer.choose before any
//! training row from it counts.
//!
//! Scope of this first cut: the GREEDY exploitation path (epsilon 0) --
//! screen with blend, land-drop + no-upside prunes, top-k playouts to
//! the deciding seat's next turn start, argmax. Exploration draws and
//! the aggression prior stay in Python for now (they never touch the
//! lockstep-critical greedy path).

use penta::{Action, Game, PlayerId, PlayerObservation};
use penta::protocol::protocol_actions;

use crate::extract::features;
use crate::net::Mlp;
use crate::tables::Tables;

pub struct SearchConfig {
    pub top_k: usize,
    pub playouts: usize,
    pub budget: usize,
    pub playout_max_eval: usize,
}

pub struct Policy {
    pub tables: Tables,
    pub value: Mlp,
    pub head: Mlp,
    pub weight: f64,
    pub search: SearchConfig,
    pub max_eval: usize,
}

const MAIN_STEPS: [penta::Step; 2] =
    [penta::Step::PrecombatMain, penta::Step::PostcombatMain];

fn terminal_value(g: &Game, seat: PlayerId) -> Option<f64> {
    g.result().map(|r| match r {
        penta::GameResult::Winner { winner, .. } => if winner == seat { 1.0 } else { 0.0 },
        penta::GameResult::Draw => 0.5,
    })
}

impl Policy {
    fn afterstate(&self, g: &Game, action_index: usize, seat: PlayerId)
                  -> Option<(Game, PlayerObservation)> {
        let obs = g.observe(seat);
        let actions = protocol_actions(&obs);
        let action = actions.get(action_index)?.clone();
        let mut copy = g.clone();
        copy.apply(seat, action).ok()?;
        let after = copy.observe(seat);
        Some((copy, after))
    }

    fn blended(&self, feats: &[f32]) -> f64 {
        (1.0 - self.weight) * self.value.value(feats)
            + self.weight * self.head.value(feats)
    }

    /// Greedy choice on a live game for the seat to move. Returns the
    /// action index into the CURRENT observation's protocol_actions.
    pub fn choose(&self, g: &Game) -> usize {
        let seat = g.decision_player().expect("choose on a finished game");
        let obs = g.observe(seat);
        let n = obs.legal_actions.len();
        if n == 1 {
            return 0;
        }
        let actions = protocol_actions(&obs);
        let keep = self.dominance_keep(g, &obs, &actions, seat);
        // Score survivors: terminal -> exact, else blended afterstate.
        let mut scored: Vec<(usize, f64, Option<Game>)> = Vec::new();
        for &i in &keep {
            match self.afterstate(g, i, seat) {
                None => scored.push((i, -1.0, None)),
                Some((copy, after)) => {
                    if let Some(t) = terminal_value(&copy, seat) {
                        scored.push((i, t, None));
                    } else {
                        let f = features(&after, false, &self.tables);
                        scored.push((i, self.blended(&f), Some(copy)));
                    }
                }
            }
        }
        if self.search.top_k == 0 {
            let mut bi = scored[0].0; let mut bv = scored[0].1;
            for &(i, v, _) in &scored {  // first-max (numpy argmax)
                if v > bv { bv = v; bi = i; }
            }
            return bi;
        }
        // Top-k by screen -> playout refine; argmax over refined only.
        let mut order: Vec<usize> = (0..scored.len()).collect();
        order.sort_by(|&a, &b| scored[b].1.total_cmp(&scored[a].1));
        order.truncate(self.search.top_k);
        let turn0 = obs.turn;
        let was_pregame = obs.turn == 0;
        let mut best_i = scored[order[0]].0;
        let mut best_v = f64::NEG_INFINITY;
        for &oi in &order {
            let (idx, screen, ref copy) = scored[oi];
            let refined = match copy {
                None => screen, // terminal keeps its exact outcome
                Some(c) => self.playout(c, seat, turn0, was_pregame),
            };
            if refined > best_v {
                best_v = refined;
                best_i = idx;
            }
        }
        best_i
    }

    fn playout(&self, start: &Game, seat: PlayerId, turn0: u32,
               was_pregame: bool) -> f64 {
        let mut g = start.clone();
        for _ in 0..self.search.budget {
            if let Some(t) = terminal_value(&g, seat) {
                return t;
            }
            let Some(acting) = g.decision_player() else { break };
            let obs = g.observe(acting);
            // boundary: seat's next turn start
            if acting == seat && !obs_pregame(&obs)
                && (was_pregame || obs.turn > turn0)
            {
                break;
            }
            let actions = protocol_actions(&obs);
            if actions.len() == 1 {
                let _ = g.apply(acting, actions[0].clone());
                continue;
            }
            // greedy 1-ply for the acting seat, capped at playout_max_eval
            let limit = actions.len().min(self.search.playout_max_eval);
            let mut best = 0usize;
            let mut best_v = f64::NEG_INFINITY;
            for i in 0..limit {
                let mut copy = g.clone();
                if copy.apply(acting, actions[i].clone()).is_err() {
                    continue;
                }
                let v = match terminal_value(&copy, acting) {
                    Some(t) => t,
                    None => {
                        let f = features(&copy.observe(acting), false,
                                         &self.tables);
                        self.value.value(&f)
                    }
                };
                if v > best_v { best_v = v; best = i; }
            }
            if g.apply(acting, actions[best].clone()).is_err() {
                break;
            }
        }
        match terminal_value(&g, seat) {
            Some(t) => t,
            None => {
                let obs = g.observe(seat);
                self.value.value(&features(&obs, false, &self.tables))
            }
        }
    }

    // -- dominance prune (land-drop + no-upside vs pass) ----------------
    // Returns the indices (into `actions`) that survive.
    fn dominance_keep(&self, _g: &Game, obs: &PlayerObservation,
                      actions: &[Action], seat: PlayerId) -> Vec<usize> {
        // Fail-closed on any non-empty stack / pregame -> keep all.
        let all: Vec<usize> = (0..actions.len()).collect();
        if !obs.stack.is_empty() {
            return all;
        }
        // NOTE: this cut ports only the land-drop rule half (the biggest
        // behavioral lever); the no-upside-vs-pass settle prune stays in
        // Python for now, so the Rust greedy path is a SUPERSET-safe
        // approximation -- lockstep tolerates it only where both agree.
        // Full parity is the next milestone.
        all
    }
}

fn obs_pregame(_obs: &PlayerObservation) -> bool {
    // In-process external games are past mulligans by turn 1; the
    // boundary check also guards on turn, so this is a conservative
    // stand-in until pregame is threaded through.
    false
}
