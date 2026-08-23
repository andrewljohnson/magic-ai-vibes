//! Compact (state, action) encoding — the piece that makes search fast.
//!
//! THE PROBLEM IT SOLVES. Our nets score AFTERSTATES, so getting a prior
//! over a node's actions means SIMULATING each one: clone the game, apply,
//! observe, featurize. Measured in real self-play that is **13.15 ms per
//! action**, of which the neural net is ~0.3 ms — the rest is engine work.
//! Across `action_prior` (64%) and the greedy opponent model (26%) it is
//! ~90% of all search time, and no amount of batching touches it (tried:
//! batching the net changed nothing, which is what pinned the diagnosis).
//!
//! THE FIX. Describe an action by what it IS, read straight off the action
//! and the current observation, with no simulation at all. The policy net
//! then factorises:
//!
//! ```text
//! h_a   = tanh(Ws · state + Wa · action_a + b1)
//! score = w2 · h_a + b2
//! ```
//!
//! `Ws · state` is computed ONCE per node; only `Wa · action_a` is per
//! action, and `action_a` is ~150 dims against the state's 1081. So a
//! node's priors cost one big matmul plus m small ones, and zero game
//! clones — instead of m clones, m featurisations and m big matmuls.
//!
//! This is also just what an AlphaZero policy head is: state in,
//! distribution over legal actions out.

use penta::{Action, PlayerObservation, PlayerId, Target};

use crate::tables::{Tables, N_COST_BUCKETS};

/// Action-type slots. Ordering is fixed and must not be reordered — a
/// trained policy head's weights are indexed by it.
const N_KINDS: usize = 12;

fn kind_slot(a: &Action) -> usize {
    match a {
        Action::KeepHand => 0,
        Action::TakeMulligan => 1,
        Action::BottomCards { .. } => 2,
        Action::DiscardCards { .. } => 3,
        Action::ChooseDecision { .. } | Action::CancelDecision { .. } => 4,
        Action::ChooseUntap { .. } => 5,
        Action::PassPriority => 6,
        Action::PlayLand { .. } => 7,
        Action::ActivateManaAbility { .. } | Action::PayLifeForMana => 8,
        Action::CastSpell { .. } => 9,
        Action::ActivateAbility { .. } => 10,
        _ => 11,     // combat: declare/finish attackers & blockers, damage
    }
}

/// The card definition an action is "about", if any: the card being cast
/// or played, or the source of an activated ability. Read from the
/// observation's zones — no simulation.
fn subject_def(a: &Action, obs: &PlayerObservation) -> Option<u16> {
    let in_hand = |id: u32| obs.hand.iter()
        .find(|(oid, _)| oid.0 == id).map(|(_, d)| d.0);
    let on_field = |id: u32| obs.battlefield.iter()
        .find(|p| p.id.0 == id).map(|p| p.definition.0);
    match a {
        Action::PlayLand { card, .. } => in_hand(card.0).or_else(|| on_field(card.0)),
        Action::CastSpell { card, .. } => in_hand(card.0),
        Action::ActivateAbility { source, .. }
        | Action::ActivateManaAbility { source, .. } => on_field(source.0),
        Action::DeclareAttacker { attacker, .. } => on_field(attacker.0),
        Action::DeclareBlocker { blocker, .. } => on_field(blocker.0),
        _ => None,
    }
}

/// TARGET block. Without this, "Fireball at their face" and "Fireball at
/// their blocker" encode identically -- the same action kind, the same
/// card, the same cost -- and a policy head cannot tell the two apart.
/// In Magic the target is often the whole decision, so it gets real
/// features rather than a flag:
///
///   0  targets a player at all
///   1  that player is the OPPONENT (1) or us (-1)
///   2  targets a permanent
///   3  that permanent is theirs (1) or ours (-1)
///   4  target's printed power / 8
///   5  target's toughness / 8
///   6  target is a creature
///   7  target is tapped
///   8  number of targets / 4
///   9  X value / 8 (Fireball for 1 is not Fireball for 6)
///  10  targets a spell on the stack (counterspells)
///  11  divided-damage amount on the first target / 8
const N_TARGET: usize = 12;

/// Width of the encoding: kinds + card-definition slot + cost bucket
/// + subject scalars + the target block.
pub fn width(t: &Tables) -> usize {
    N_KINDS + t.defs + N_COST_BUCKETS + 4 + N_TARGET
}

/// Fill the target block from a list of target selections.
fn encode_targets(sel: &[penta::TargetSelection], x: u16,
                  obs: &PlayerObservation, t: &Tables, out: &mut [f32]) {
    let me = obs.viewer;
    let mut n = 0usize;
    for s in sel {
        for (k, tgt) in s.targets().iter().enumerate() {
            n += 1;
            match tgt {
                Target::Player(p) => {
                    out[0] = 1.0;
                    out[1] = if *p == me { -1.0 } else { 1.0 };
                }
                Target::Permanent(id) => {
                    out[2] = 1.0;
                    if let Some(perm) = obs.battlefield.iter()
                        .find(|p| p.id.0 == id.0)
                    {
                        out[3] = if perm.controller == me { -1.0 } else { 1.0 };
                        out[4] = f32::from(perm.power.unwrap_or(0)) / 8.0;
                        out[5] = f32::from(perm.toughness.unwrap_or(0)) / 8.0;
                        out[6] = if perm.power.is_some() { 1.0 } else { 0.0 };
                        out[7] = if perm.tapped { 1.0 } else { 0.0 };
                    }
                }
                Target::Spell(_) => out[10] = 1.0,
                Target::Card(_) => {}
            }
            if k == 0 {
                if let Some(a) = s.amounts().first() {
                    out[11] = f32::from(*a) / 8.0;
                }
            }
        }
    }
    out[8] = n as f32 / 4.0;
    out[9] = f32::from(x) / 8.0;
    let _ = t;
}

/// Encode one action into `out` (length `width(t)`), zeroing first.
///
/// Everything here is a lookup against the observation the caller already
/// has. Nothing applies the action.
pub fn encode(a: &Action, obs: &PlayerObservation, t: &Tables,
              out: &mut [f32]) {
    debug_assert_eq!(out.len(), width(t));
    out.fill(0.0);
    out[kind_slot(a)] = 1.0;
    let base = N_KINDS;
    let cost_base = base + t.defs;
    let scalar = cost_base + N_COST_BUCKETS;

    if let Some(d) = subject_def(a, obs) {
        if let Some(&i) = t.def_slot.get(&d) {
            out[base + i] = 1.0;
        }
        out[cost_base + t.cost_bucket(d)] = 1.0;
        out[scalar] = t.power.get(&d).copied().unwrap_or(0) as f32 / 8.0;
        out[scalar + 1] = if t.is_creature_kind(d) { 1.0 } else { 0.0 };
        out[scalar + 2] = if t.flying.contains(&d) { 1.0 } else { 0.0 };
    }
    // Attacking/blocking is a strong signal on its own and is not carried
    // by the subject card's identity.
    out[scalar + 3] = match a {
        Action::DeclareAttacker { .. } => 1.0,
        Action::DeclareBlocker { .. } => -1.0,
        _ => 0.0,
    };

    let tb = scalar + 4;
    match a {
        Action::CastSpell { choices, .. } => {
            encode_targets(choices.targets(), choices.x(), obs, t,
                           &mut out[tb..tb + N_TARGET]);
        }
        Action::ActivateAbility { targets, .. } => {
            encode_targets(targets, 0, obs, t, &mut out[tb..tb + N_TARGET]);
        }
        Action::DeclareBlocker { attacker, .. } => {
            // Which attacker we block is the decision; describe it the same
            // way a spell target is described.
            if let Some(p) = obs.battlefield.iter()
                .find(|p| p.id.0 == attacker.0)
            {
                out[tb + 2] = 1.0;
                out[tb + 3] = 1.0;                       // theirs, by definition
                out[tb + 4] = f32::from(p.power.unwrap_or(0)) / 8.0;
                out[tb + 5] = f32::from(p.toughness.unwrap_or(0)) / 8.0;
                out[tb + 6] = 1.0;
            }
        }
        _ => {}
    }
    let _ = PlayerId::One;
}

/// Encode every action into one row-major `m * width` block.
pub fn encode_all(actions: &[Action], obs: &PlayerObservation, t: &Tables)
                  -> Vec<f32> {
    let w = width(t);
    let mut out = vec![0.0f32; actions.len() * w];
    for (i, a) in actions.iter().enumerate() {
        let (lo, hi) = (i * w, (i + 1) * w);
        encode(a, obs, t, &mut out[lo..hi]);
    }
    out
}

/// Factorised policy head: state -> scores over legal actions.
///
/// `ws` is `hidden x state_dim`, `wa` is `hidden x action_dim`, both
/// row-major, matching a torch `Linear` weight.
pub struct PolicyHead {
    pub hidden: usize,
    pub state_dim: usize,
    pub action_dim: usize,
    pub ws: Vec<f64>,
    pub wa: Vec<f64>,
    pub b1: Vec<f64>,
    pub w2: Vec<f64>,
    pub b2: f64,
}

impl PolicyHead {
    /// Precompute `Ws · state + b1` — once per node, reused by every action.
    pub fn state_pre(&self, state: &[f32]) -> Vec<f64> {
        debug_assert_eq!(state.len(), self.state_dim);
        let mut pre = self.b1.clone();
        for h in 0..self.hidden {
            let row = &self.ws[h * self.state_dim..(h + 1) * self.state_dim];
            let mut acc = 0.0f64;
            for (w, x) in row.iter().zip(state) {
                acc += w * f64::from(*x);
            }
            pre[h] += acc;
        }
        pre
    }

    /// Scores for `m` encoded actions given a precomputed state term.
    pub fn scores(&self, pre: &[f64], acts: &[f32], m: usize) -> Vec<f64> {
        debug_assert_eq!(acts.len(), m * self.action_dim);
        let mut out = vec![self.b2; m];
        let mut acc = vec![0.0f64; m];
        for h in 0..self.hidden {
            let row = &self.wa[h * self.action_dim..(h + 1) * self.action_dim];
            let p = pre[h];
            for (j, a) in acc.iter_mut().enumerate() {
                let x = &acts[j * self.action_dim..(j + 1) * self.action_dim];
                let mut s = p;
                for (w, xi) in row.iter().zip(x) {
                    s += w * f64::from(*xi);
                }
                *a = s;
            }
            let w2h = self.w2[h];
            for (o, a) in out.iter_mut().zip(&acc) {
                *o += w2h * a.tanh();
            }
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use penta::protocol::{protocol_actions, BotGame, Opponent};
    use penta::PlayerId;

    fn tables() -> Tables {
        Tables::load(&std::fs::read_to_string("../pinned-catalog.json")
            .expect("catalog")).expect("tables")
    }

    /// The claim this module exists for: encoding an action is orders of
    /// magnitude cheaper than SIMULATING it. Measured in real self-play,
    /// the afterstate path costs 13.15 ms per action; this should be in
    /// microseconds, because it never touches the engine.
    #[test]
    fn encoding_is_far_cheaper_than_simulating() {
        let t = tables();
        let mut g = BotGame::new("Sligh", "Goblins", Opponent::External,
                                 PlayerId::Two, 99).expect("game");
        // walk to a position with real branching
        let mut acts;
        loop {
            let Some(seat) = g.decision_seat() else { break };
            let obs = g.core_game().observe(seat);
            acts = protocol_actions(&obs);
            if acts.len() >= 4 { break; }
            if g.act(0).is_err() { break; }
        }
        let seat = g.decision_seat().expect("a decision");
        let obs = g.core_game().observe(seat);
        let acts = protocol_actions(&obs);
        assert!(acts.len() >= 4, "want a branching position");

        // (a) SIMULATE: what action_prior does today.
        let t0 = std::time::Instant::now();
        let mut sunk = 0usize;
        for _ in 0..20 {
            for i in 0..acts.len() {
                let mut c = g.clone();
                if c.act(i).is_err() { continue; }
                let f = crate::extract::features(
                    &c.core_game().observe(seat), c.core_game().in_pregame(),
                    &t, &[Vec::new(), Vec::new()]);
                sunk += f.len();
            }
        }
        let sim_ns = t0.elapsed().as_nanos() as f64 / (20.0 * acts.len() as f64);

        // (b) ENCODE: what this module does.
        let t1 = std::time::Instant::now();
        for _ in 0..20 {
            let e = encode_all(&acts, &obs, &t);
            sunk += e.len();
        }
        let enc_ns = t1.elapsed().as_nanos() as f64 / (20.0 * acts.len() as f64);

        println!("\n  simulate afterstate: {:>9.1} us/action", sim_ns / 1e3);
        println!("  encode action      : {:>9.1} us/action", enc_ns / 1e3);
        println!("  speedup            : {:>9.0}x", sim_ns / enc_ns.max(1.0));
        assert!(sunk > 0);
        assert!(enc_ns * 10.0 < sim_ns,
                "encoding must be far cheaper than simulating");
    }

    /// The point of the target block: the SAME spell aimed at different
    /// things must encode differently. Without it, "Fireball at their
    /// face" and "Fireball at their blocker" are the same vector -- same
    /// kind, same card, same cost -- and no policy head can choose between
    /// them, which in Magic is often the entire decision.
    #[test]
    fn same_spell_different_targets_encode_differently() {
        let t = tables();
        let w = width(&t);
        // Search real games for a decision offering one card with several
        // distinct targets.
        // Walk with a land>cast>attack preference: always playing action 0
        // passes priority forever and never casts anything, so the case we
        // are testing for never arises.
        let step = |acts: &[penta::Action]| -> usize {
            let rank = |a: &penta::Action| match a {
                penta::Action::PlayLand { .. } => 0,
                penta::Action::CastSpell { .. } => 1,
                penta::Action::DeclareAttacker { .. } => 2,
                penta::Action::PassPriority => 9,
                _ => 5,
            };
            (0..acts.len()).min_by_key(|&i| (rank(&acts[i]), i)).unwrap_or(0)
        };
        let mut found = None;
        'outer: for seed in 0..60u64 {
            let mut g = BotGame::new("Sligh", "Goblins", Opponent::Handcrafted,
                                     PlayerId::Two, seed).expect("game");
            for _ in 0..600 {
                let Some(seat) = g.decision_seat() else { break };
                let obs = g.core_game().observe(seat);
                let acts = protocol_actions(&obs);
                // group CastSpell actions by the card being cast
                let mut by_card: std::collections::HashMap<u32, Vec<usize>> =
                    std::collections::HashMap::new();
                for (i, a) in acts.iter().enumerate() {
                    if let penta::Action::CastSpell { card, .. } = a {
                        by_card.entry(card.0).or_default().push(i);
                    }
                }
                if let Some((_, idxs)) = by_card.iter().find(|(_, v)| v.len() > 1) {
                    found = Some((encode_all(&acts, &obs, &t), idxs.clone()));
                    break 'outer;
                }
                if g.act(step(&acts)).is_err() { break; }
            }
        }
        let Some((enc, idxs)) = found else {
            println!("  (no multi-target cast found in 40 games; skipping)");
            return;
        };
        let a = &enc[idxs[0] * w..(idxs[0] + 1) * w];
        let mut differing = 0;
        for &j in &idxs[1..] {
            if a != &enc[j * w..(j + 1) * w] { differing += 1; }
        }
        println!("  same card, {} target choices, {} encode differently",
                 idxs.len(), differing);
        assert!(differing > 0,
                "the same spell at different targets must not collapse");
    }

    #[test]
    fn encoding_distinguishes_actions() {
        let t = tables();
        let mut g = BotGame::new("Sligh", "Goblins", Opponent::External,
                                 PlayerId::Two, 7).expect("game");
        loop {
            let Some(seat) = g.decision_seat() else { break };
            let obs = g.core_game().observe(seat);
            if protocol_actions(&obs).len() >= 3 { break; }
            if g.act(0).is_err() { break; }
        }
        let seat = g.decision_seat().expect("a decision");
        let obs = g.core_game().observe(seat);
        let acts = protocol_actions(&obs);
        let w = width(&t);
        let enc = encode_all(&acts, &obs, &t);
        // distinct actions must not encode identically, or the policy head
        // cannot rank them -- the exact failure the value head had.
        let mut distinct = 0;
        for i in 1..acts.len() {
            if enc[..w] != enc[i * w..(i + 1) * w] { distinct += 1; }
        }
        println!("  {} of {} actions differ from the first",
                 distinct, acts.len() - 1);
        assert!(distinct > 0, "encoding collapses every action to one vector");
    }
}
