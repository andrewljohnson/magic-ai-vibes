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
        self.choose_scored(g).0
    }

    /// As `choose`, plus the refined value of the chosen action (the
    /// consensus tie-break needs it). Value is the playout/terminal
    /// score of the winner, or its blended screen score with search off.
    pub fn choose_scored(&self, g: &Game) -> (usize, f64) {
        let seat = g.decision_player().expect("choose on a finished game");
        let obs = g.observe(seat);
        // COUNT via protocol_actions, not the typed legal_actions: a
        // single typed action can project to several protocol entries
        // (a spell with N targets) or vice versa. The whole policy
        // indexes into protocol_actions, so the forced check must too.
        let actions = protocol_actions(&obs);
        if actions.len() == 1 {
            return (0, 0.5);
        }
        let keep = self.dominance_keep(g, &obs, &actions, seat, g.in_pregame());
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
            return (bi, bv);
        }
        // Top-k by screen -> playout refine; argmax over refined only.
        // Top-k by screen; refine those by playout. The final argmax is
        // over the refined vector in POSITION order with -1.0 for
        // non-top-k -- exactly np.argmax(refined) in trainer.choose,
        // which returns the FIRST maximum on ties (position = index).
        let mut order: Vec<usize> = (0..scored.len()).collect();
        // stable sort desc by screen, matching Python's sorted(..., key,
        // reverse=True)[:top_k] (stable, ties keep ascending position).
        order.sort_by(|&a, &b| scored[b].1.total_cmp(&scored[a].1));
        order.truncate(self.search.top_k);
        let turn0 = obs.turn;
        let was_pregame = g.in_pregame();
        let mut refined = vec![-1.0f64; scored.len()];
        for &oi in &order {
            let (_, screen, ref copy) = scored[oi];
            refined[oi] = match copy {
                None => screen, // terminal keeps its exact outcome
                Some(c) => self.playout(c, seat, turn0, was_pregame),
            };
        }
        let mut best_pos = 0usize;
        let mut best_v = f64::NEG_INFINITY;
        for (pos, &v) in refined.iter().enumerate() {
            if v > best_v {  // first-max = lowest position (np.argmax)
                best_v = v;
                best_pos = pos;
            }
        }
        (scored[best_pos].0, best_v)
    }

    /// Refined playout value for ONE candidate action index, for
    /// per-candidate lockstep diffing vs trainer.playout_value.
    pub fn playout_candidate(&self, g: &Game, action_index: usize) -> f64 {
        let seat = g.decision_player().expect("finished game");
        let obs = g.observe(seat);
        let actions = protocol_actions(&obs);
        let mut copy = g.clone();
        copy.apply(seat, actions[action_index].clone())
            .expect("candidate apply");
        if let Some(t) = terminal_value(&copy, seat) {
            return t;
        }
        let turn0 = obs.turn;
        let was_pregame = g.in_pregame();
        self.playout(&copy, seat, turn0, was_pregame)
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
            // boundary: the deciding seat's next TURN start -- our seat is
            // the ACTIVE player (not merely holding priority) on a later
            // turn. Matches trainer.playout_boundary (activeSeat==seat),
            // NOT "we have priority", which also fires on the opponent's
            // turn and would truncate the playout early.
            if !g.in_pregame() && obs.active_player == seat
                && (was_pregame || obs.turn > turn0)
            {
                break;
            }
            let actions = protocol_actions(&obs);
            if actions.len() == 1 {
                let _ = g.apply(acting, actions[0].clone());
                continue;
            }
            // greedy 1-ply for the acting seat -- EVALUATE-ALL policy
            // (playout_max_eval cap retired; matches trainer.EVAL_ALL).
            let limit = actions.len();
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
    // A faithful port of trainer.dominance_prune. Returns the indices
    // (into `actions`) that survive. `pregame` is threaded from the
    // caller (Game::in_pregame).
    fn dominance_keep(&self, g: &Game, obs: &PlayerObservation,
                      actions: &[Action], seat: PlayerId,
                      pregame: bool) -> Vec<usize> {
        let all: Vec<usize> = (0..actions.len()).collect();
        if pregame || !obs.stack.is_empty() {
            return all;
        }
        let passes: Vec<usize> = actions.iter().enumerate()
            .filter(|(_, a)| matches!(a, Action::PassPriority))
            .map(|(i, _)| i).collect();
        if passes.is_empty() {
            return all;
        }
        let non_pass: Vec<usize> = actions.iter().enumerate()
            .filter(|(_, a)| !matches!(a, Action::PassPriority))
            .map(|(i, _)| i).collect();
        let base = DomState::of(obs, seat, &self.tables);
        let mut pruned: std::collections::HashSet<usize> =
            std::collections::HashSet::new();

        // (a) candidates strictly dominated by standing pat.
        let testable: Vec<usize> = non_pass.iter().copied()
            .filter(|&i| matches!(actions[i],
                Action::CastSpell { .. } | Action::ActivateAbility { .. }))
            .collect();
        if non_pass.len() >= 2 && !testable.is_empty() {
            // instance -> definition (hand + battlefield), for the
            // invisible-upside guard.
            let mut inst_def: std::collections::HashMap<u32, u16> =
                std::collections::HashMap::new();
            for (id, d) in &obs.hand { inst_def.insert(id.0, d.0); }
            for perm in &obs.battlefield {
                inst_def.insert(perm.id.0, perm.definition.0);
            }
            for &i in &testable {
                let def = match &actions[i] {
                    Action::CastSpell { card, .. } => inst_def.get(&card.0),
                    Action::ActivateAbility { source, .. } =>
                        inst_def.get(&source.0),
                    _ => None,
                };
                // Unmappable id -> treat as invisible-upside (fail closed).
                let upside = def.map_or(true,
                    |d| self.tables.invisible_upside.contains(d));
                let mut copy = g.clone();
                if copy.apply(seat, actions[i].clone()).is_err() {
                    continue; // fail closed, keep the action
                }
                if !settle_all_pass(&mut copy) {
                    continue;
                }
                let cand = DomState::of(&copy.observe(seat), seat,
                                        &self.tables);
                if cand.dominated_by_pass(&base, upside) {
                    pruned.insert(i);
                }
            }
            if pruned.len() == non_pass.len() {
                pruned.clear(); // never sweep every non-pass action
            }
        }

        // (b) Pass dominated by a free land drop (our main phase only).
        let main = obs.active_player == seat
            && MAIN_STEPS.contains(&obs.step);
        if main {
            for &i in &non_pass {
                if !matches!(actions[i], Action::PlayLand { .. }) {
                    continue;
                }
                let mut copy = g.clone();
                if copy.apply(seat, actions[i].clone()).is_err() {
                    continue;
                }
                if !settle_all_pass(&mut copy) {
                    continue;
                }
                let cand = DomState::of(&copy.observe(seat), seat,
                                        &self.tables);
                if cand.land_dominates_pass(&base) {
                    for &pi in &passes { pruned.insert(pi); }
                    break;
                }
            }
        }

        if pruned.is_empty() {
            return all;
        }
        all.into_iter().filter(|i| !pruned.contains(i)).collect()
    }
}

/// Settle a clone to an empty stack by answering every priority with
/// PassPriority (forced moves taken as-is). False FAILS CLOSED
/// (terminal, a branching decision with no pass, or budget exhausted).
fn settle_all_pass(g: &mut Game) -> bool {
    const BUDGET: usize = 24;
    for _ in 0..BUDGET {
        if g.result().is_some() {
            return false;
        }
        let Some(seat) = g.decision_player() else { return false };
        let obs = g.observe(seat);
        if obs.stack.is_empty() {
            return true;
        }
        let actions = protocol_actions(&obs);
        if actions.len() == 1 {
            if g.apply(seat, actions[0].clone()).is_err() { return false; }
            continue;
        }
        match actions.iter().position(|a| matches!(a, Action::PassPriority)) {
            None => return false,
            Some(pi) => {
                if g.apply(seat, actions[pi].clone()).is_err() {
                    return false;
                }
            }
        }
    }
    false
}

/// The comparable slice of one observation, from `seat`'s view --
/// trainer._dominance_state. Battlefield keys carry (def, power,
/// toughness) so a stat-changed permanent is incomparable, never equal.
struct DomState {
    turn: u32,
    step: penta::Step,
    my_hand: usize,
    my_life: i16,
    my_lib: usize,
    my_bf: std::collections::HashMap<(u16, i16, i16), u32>,
    my_mana: u16,
    opp_hand: usize,
    opp_life: i16,
    opp_bf: std::collections::HashMap<(u16, i16, i16), u32>,
    opp_gy: std::collections::HashMap<u16, u32>,
    opp_exile: std::collections::HashMap<u16, u32>,
}

fn bf_key(perm: &penta::PermanentObservation) -> (u16, i16, i16) {
    (perm.definition.0, perm.power.unwrap_or(i16::MIN),
     perm.toughness.unwrap_or(i16::MIN))
}

fn multiset<T: std::hash::Hash + Eq>(it: impl Iterator<Item = T>)
    -> std::collections::HashMap<T, u32> {
    let mut m = std::collections::HashMap::new();
    for k in it { *m.entry(k).or_insert(0) += 1; }
    m
}

fn covered<T: std::hash::Hash + Eq>(
    a: &std::collections::HashMap<T, u32>,
    b: &std::collections::HashMap<T, u32>) -> bool {
    a.iter().all(|(k, n)| b.get(k).copied().unwrap_or(0) >= *n)
}

impl DomState {
    fn of(obs: &PlayerObservation, seat: PlayerId, _t: &Tables) -> Self {
        let mi = if seat == PlayerId::One { 0 } else { 1 };
        let oi = 1 - mi;
        let mut my_bf = std::collections::HashMap::new();
        let mut opp_bf = std::collections::HashMap::new();
        for perm in &obs.battlefield {
            let m = if perm.controller == seat { &mut my_bf } else { &mut opp_bf };
            *m.entry(bf_key(perm)).or_insert(0) += 1;
        }
        let pool = &obs.mana_pools[mi];
        DomState {
            turn: obs.turn, step: obs.step,
            my_hand: obs.hand.len(),
            my_life: obs.life_totals[mi],
            my_lib: obs.library_sizes[mi],
            my_bf,
            my_mana: pool.white + pool.blue + pool.black + pool.red
                + pool.green + pool.colorless,
            opp_hand: obs.opponent_hand_size,
            opp_life: obs.life_totals[oi],
            opp_bf,
            opp_gy: multiset(obs.graveyards[oi].iter().map(|(_, d)| d.0)),
            opp_exile: multiset(obs.exiles[oi].iter().map(|(_, d)| d.0)),
        }
    }

    fn dominated_by_pass(&self, base: &DomState, invisible_upside: bool) -> bool {
        if self.turn != base.turn || self.step != base.step { return false; }
        if self.opp_hand < base.opp_hand { return false; }
        if self.opp_life < base.opp_life { return false; }
        if !covered(&base.opp_bf, &self.opp_bf) { return false; }
        if !covered(&base.opp_gy, &self.opp_gy) { return false; }
        if !covered(&base.opp_exile, &self.opp_exile) { return false; }
        if self.my_hand > base.my_hand { return false; }
        if self.my_life > base.my_life { return false; }
        if self.my_lib > base.my_lib { return false; }
        if !covered(&self.my_bf, &base.my_bf) { return false; }
        if self.my_mana > base.my_mana { return false; }
        let strict = self.my_hand < base.my_hand
            || self.my_life < base.my_life
            || self.my_lib < base.my_lib
            || self.my_bf != base.my_bf;
        if !strict { return false; }
        !invisible_upside
    }

    fn land_dominates_pass(&self, base: &DomState) -> bool {
        if self.turn != base.turn || self.step != base.step { return false; }
        if self.my_hand != base.my_hand.wrapping_sub(1) { return false; }
        if self.my_life < base.my_life || self.my_lib < base.my_lib {
            return false;
        }
        if !covered(&base.my_bf, &self.my_bf) { return false; }
        let cand_bf: u32 = self.my_bf.values().sum();
        let base_bf: u32 = base.my_bf.values().sum();
        if cand_bf <= base_bf { return false; }
        self.opp_hand == base.opp_hand
            && self.opp_life == base.opp_life
            && self.opp_bf == base.opp_bf
            && self.opp_gy == base.opp_gy
            && self.opp_exile == base.opp_exile
    }
}
