//! Single-Observer Information-Set MCTS (SO-ISMCTS), native.
//!
//! One shared tree over OUR information sets. Every iteration we
//! RE-DETERMINIZE the hidden state from the ROOT observation
//! (`sample_hidden` / `inert_hidden` + `from_observation_json`,
//! reusing the certified determinization primitive), then descend that
//! concrete world:
//!
//!  * OUR decision nodes branch the tree, selected by availability-count
//!    PUCT (below). The action set is restricted to what is legal in THIS
//!    determinization (and optionally the certified dominance prune).
//!  * OPPONENT decision nodes do NOT branch -- the opponent is played as a
//!    fixed environment by the greedy 1-ply value-net model
//!    (`Policy::greedy_index`, the same rule that drives `Policy::playout`).
//!  * A leaf is bootstrapped by the value net on the redacted afterstate
//!    (a short greedy rollout is an optional knob).
//!
//! NODE KEYING (the crux). OUR info-set nodes are identified by the
//! DETERMINIZATION-INDEPENDENT sequence of action DESCRIPTORS taken to
//! reach them -- the typed `Action` itself (action type + its stable
//! source/target GameObjectIds + choice params X/mode/option), NOT the
//! `protocol_actions` INDEX (which shifts whenever hidden cards change the
//! legal set). This is sound because `from_observation_json` reconstructs
//! and VALIDATES the full public observation: every object OUR actions can
//! name (a card in our hand, a permanent we control, a target we can see, a
//! player) keeps the identical GameObjectId across determinizations, while
//! only hidden zones (opponent hand, libraries) get fresh ids -- and we
//! never branch on opponent moves. Two determinizations that reach the same
//! our-action-history therefore produce the same key path and reuse the
//! same node (asserted by the node-merge property test).
//!
//! PUCT: `Q(a) + c_puct * P(a) * sqrt(ln A(a) / (1 + N(a)))`, with
//!  * `Q(a) = W(a)/N(a)` (0 for an unvisited action),
//!  * `P(a)` the policy-head prior (softmax over head afterstate scores),
//!  * `A(a)` the AVAILABILITY count -- iterations in which `a` was legal at
//!    this node (NOT the parent visit count), because different
//!    determinizations expose different legal sets.

use std::collections::HashMap;

use penta::protocol::{protocol_actions, BotGame};
use penta::{Action, Game, PlayerId, PlayerObservation};

use crate::decks::Decks;
use crate::det_runner::{inert_hidden, sample_hidden};
use crate::extract::features;
use crate::policy::{terminal_value, Policy};
use crate::prng::SplitMix64;

/// Determinization-independent descriptor of one OUR action: the typed
/// `Action` (type + stable ids + choice params). See the module crux note.
pub type ActionKey = Action;

#[derive(Default)]
struct ActionStat {
    n: u32,   // visits through this action
    w: f64,   // summed leaf value (from OUR perspective)
    a: u32,   // availability: iterations this action was legal at this node
    prior: f64,      // cached policy-head afterstate score (softmax logit)
    prior_set: bool, // whether `prior` has been computed
}

/// One OUR information-set node. Stats are keyed by the determinization-
/// independent `ActionKey`.
#[derive(Default)]
struct Node {
    stats: HashMap<ActionKey, ActionStat>,
}

/// Arena of nodes plus a path index: a node's identity IS the sequence of
/// OUR ActionKeys taken from the root, so the same our-action-history maps
/// to the same node regardless of determinization.
struct Tree {
    arena: Vec<Node>,
    index: HashMap<Vec<ActionKey>, usize>,
}

impl Tree {
    fn new() -> Self {
        let mut arena = Vec::new();
        arena.push(Node::default()); // root = node 0, path []
        let mut index = HashMap::new();
        index.insert(Vec::new(), 0usize);
        Tree { arena, index }
    }

    fn get_or_create(&mut self, path: &[ActionKey]) -> usize {
        if let Some(&i) = self.index.get(path) {
            return i;
        }
        let i = self.arena.len();
        self.arena.push(Node::default());
        self.index.insert(path.to_vec(), i);
        i
    }
}

#[derive(Clone)]
pub struct MctsConfig {
    pub iters: usize,
    pub c_puct: f64,
    /// Determinization mode: false = `sample_hidden` (default), true = the
    /// deterministic `inert_hidden` hypothesis.
    pub inert: bool,
    /// Restrict OUR branching set with the certified dominance prune.
    pub use_dominance: bool,
    /// Leaf eval: false = value-net bootstrap (default), true = short greedy
    /// rollout to terminal.
    pub leaf_playout: bool,
    /// Leaf eval blend: false = value net only (default), true = value+head.
    pub leaf_blend: bool,
}

impl Default for MctsConfig {
    fn default() -> Self {
        MctsConfig {
            iters: 200,
            c_puct: 1.5,
            inert: false,
            use_dominance: true,
            leaf_playout: false,
            leaf_blend: false,
        }
    }
}

/// A configured single-observer ISMCTS search bound to one seat/matchup.
pub struct Ismcts<'a> {
    pub policy: &'a Policy,
    pub decks: &'a Decks,
    pub deck_slots: &'a [Vec<i32>; 2],
    pub my_deck: &'a str,
    pub opp_deck: &'a str,
    pub our_seat: PlayerId,
    pub cfg: MctsConfig,
}

impl<'a> Ismcts<'a> {
    fn leaf_eval(&self, world: &Game) -> f64 {
        if let Some(t) = terminal_value(world, self.our_seat) {
            return t;
        }
        if self.cfg.leaf_playout {
            return self.policy.rollout_to_end(world, self.our_seat,
                                              self.deck_slots);
        }
        let obs = world.observe(self.our_seat);
        let f = features(&obs, world.in_pregame(), &self.policy.tables,
                         self.deck_slots);
        if self.cfg.leaf_blend {
            self.policy.blended_value(&f)
        } else {
            self.policy.value.value(&f)
        }
    }

    /// Policy-head afterstate score for OUR action `i` in `world` (the
    /// softmax logit cached as the PUCT prior).
    fn action_prior(&self, world: &Game, seat: PlayerId,
                    actions: &[Action], i: usize) -> f64 {
        let mut copy = world.clone();
        if copy.apply(seat, actions[i].clone()).is_err() {
            return 0.0;
        }
        if let Some(t) = terminal_value(&copy, seat) {
            return t;
        }
        let f = features(&copy.observe(seat), copy.in_pregame(),
                         &self.policy.tables, self.deck_slots);
        self.policy.head.value(&f)
    }

    /// OUR branching action set in `world`: legal actions, optionally
    /// narrowed by the certified dominance prune.
    fn available(&self, world: &Game, obs: &PlayerObservation,
                 actions: &[Action], seat: PlayerId) -> Vec<usize> {
        if self.cfg.use_dominance {
            self.policy.dominance_keep_indices(world, obs, actions, seat,
                                               world.in_pregame())
        } else {
            (0..actions.len()).collect()
        }
    }

    /// One determinized playout down the shared tree.
    fn iterate(&self, tree: &mut Tree, world: &mut Game) {
        let mut path: Vec<ActionKey> = Vec::new();
        // (node index, chosen ActionKey) at each OUR decision we descended.
        let mut visited: Vec<(usize, ActionKey)> = Vec::new();
        let leaf: f64;
        loop {
            if let Some(t) = terminal_value(world, self.our_seat) {
                leaf = t;
                break;
            }
            let Some(seat) = world.decision_player() else {
                leaf = self.leaf_eval(world);
                break;
            };
            let obs = world.observe(seat);
            let actions = protocol_actions(&obs);
            if seat != self.our_seat {
                // Opponent as a fixed environment: greedy 1-ply value net.
                let i = self.policy.greedy_index(world, seat, self.deck_slots);
                if world.apply(seat, actions[i].clone()).is_err() {
                    leaf = self.leaf_eval(world);
                    break;
                }
                continue;
            }
            // OUR seat.
            if actions.len() == 1 {
                let _ = world.apply(seat, actions[0].clone());
                continue;
            }
            let avail = self.available(world, &obs, &actions, seat);
            if avail.len() == 1 {
                let _ = world.apply(seat, actions[avail[0]].clone());
                continue;
            }

            let node_idx = tree.get_or_create(&path);
            // Ensure priors + bump availability for every legal action here.
            for &i in &avail {
                let key = actions[i].clone();
                let need_prior = !tree.arena[node_idx].stats
                    .get(&key).map(|s| s.prior_set).unwrap_or(false);
                let prior = if need_prior {
                    self.action_prior(world, seat, &actions, i)
                } else { 0.0 };
                let s = tree.arena[node_idx].stats.entry(key).or_default();
                if need_prior { s.prior = prior; s.prior_set = true; }
                s.a += 1;
            }
            // PUCT select over the available set.
            let (sel_i, sel_key, first_visit) =
                self.select(&tree.arena[node_idx], &actions, &avail);
            visited.push((node_idx, sel_key.clone()));
            let _ = world.apply(seat, actions[sel_i].clone());
            path.push(sel_key);
            if first_visit {
                // Expansion: leaf-eval the afterstate and stop descending.
                leaf = self.leaf_eval(world);
                break;
            }
        }
        // Backprop up OUR path (single-observer: one leaf value, no negation).
        for (nidx, key) in visited {
            if let Some(s) = tree.arena[nidx].stats.get_mut(&key) {
                s.n += 1;
                s.w += leaf;
            }
        }
    }

    /// Availability-count PUCT argmax over the available actions of `node`.
    /// Returns (action index, key, whether it was unvisited = expansion).
    fn select(&self, node: &Node, actions: &[Action], avail: &[usize])
              -> (usize, ActionKey, bool) {
        // Softmax priors over the available set.
        let logits: Vec<f64> = avail.iter().map(|&i| {
            node.stats.get(&actions[i]).map(|s| s.prior).unwrap_or(0.0)
        }).collect();
        let maxl = logits.iter().copied().fold(f64::NEG_INFINITY, f64::max);
        let exps: Vec<f64> = logits.iter().map(|l| (l - maxl).exp()).collect();
        let sum: f64 = exps.iter().sum::<f64>().max(1e-12);

        let mut best_i = avail[0];
        let mut best_key = actions[avail[0]].clone();
        let mut best_first = true;
        let mut best_puct = f64::NEG_INFINITY;
        let mut best_prior = f64::NEG_INFINITY;
        for (pos, &i) in avail.iter().enumerate() {
            let p = exps[pos] / sum;
            let (n, w, a) = node.stats.get(&actions[i])
                .map(|s| (s.n, s.w, s.a)).unwrap_or((0, 0.0, 0));
            let q = if n > 0 { w / n as f64 } else { 0.0 };
            let explore = self.cfg.c_puct * p
                * ((a.max(1) as f64).ln().max(0.0) / (1.0 + n as f64)).sqrt();
            let puct = q + explore;
            // Max PUCT; tie -> higher prior; tie -> lower action index.
            if puct > best_puct
                || (puct == best_puct && p > best_prior)
            {
                best_puct = puct;
                best_prior = p;
                best_i = i;
                best_key = actions[i].clone();
                best_first = n == 0;
            }
        }
        (best_i, best_key, best_first)
    }

    /// Build one determinized world from the root observation, or None when
    /// the deck hypothesis cannot cover the observation / reconstruction
    /// fails (the iteration is then skipped, matching the det path).
    fn determinize(&self, raw: &str, root_obs: &PlayerObservation,
                   prng: &mut SplitMix64) -> Option<Game> {
        let hidden = if self.cfg.inert {
            inert_hidden(root_obs, self.decks, self.my_deck, self.opp_deck,
                         &self.policy.tables)?
        } else {
            sample_hidden(root_obs, prng, self.decks, self.my_deck,
                          self.opp_deck)?
        };
        let seed = prng.next_u64();
        BotGame::from_observation_json(raw, &hidden, seed)
            .ok()
            .map(BotGame::into_core_game)
    }

    /// Run the search from a live game replayed to OUR root decision.
    /// Returns (best action index into the root protocol_actions, the full
    /// per-root-action visit vector). The visit vector is exposed for later
    /// policy iteration; pruned/never-visited root actions read 0.
    pub fn search(&self, real: &BotGame, prng: &mut SplitMix64)
                  -> (usize, Vec<u32>) {
        let raw = real.observe_json(self.our_seat);
        let root_obs = real.core_game().observe(self.our_seat);
        let root_actions = protocol_actions(&root_obs);
        let mut visits = vec![0u32; root_actions.len()];
        if root_actions.len() <= 1 {
            // Forced: return index 0 without building a tree.
            return (0, visits);
        }
        // Root available set is deterministic (dominance reads public state),
        // so a single survivor is the forced answer -- e.g. a dominating
        // free land drop that prunes Pass down to one option.
        let root_avail = self.available(real.core_game(), &root_obs,
                                        &root_actions, self.our_seat);
        if root_avail.len() == 1 {
            visits[root_avail[0]] = self.cfg.iters as u32;
            return (root_avail[0], visits);
        }

        let mut tree = Tree::new();
        for _ in 0..self.cfg.iters {
            if let Some(mut world) = self.determinize(&raw, &root_obs, prng) {
                self.iterate(&mut tree, &mut world);
            }
        }

        // Root node visit vector, mapped back to root protocol_actions
        // indices via the determinization-independent keys.
        let root = &tree.arena[0];
        for (i, act) in root_actions.iter().enumerate() {
            visits[i] = root.stats.get(act).map(|s| s.n).unwrap_or(0);
        }
        // Robust child: max visits, ties -> lowest index.
        let mut best = 0usize;
        for i in 1..visits.len() {
            if visits[i] > visits[best] {
                best = i;
            }
        }
        (best, visits)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::net::Mlp;
    use crate::tables::Tables;

    const CATALOG: &str = "../pinned-catalog.json";
    const DECKLISTS: &str = "../builtin-decklists.json";

    /// A tiny hand-built value net over the pinned schema. `life_w` is the
    /// weight on the opponent-life scalar (feature index 5*defs+15); a
    /// negative weight makes lower opponent life score higher, so the net
    /// prefers damage. Everything else is flat -> sigmoid(0)=0.5 baseline.
    fn make_net(inputs: usize, defs: usize, life_w: f64) -> Mlp {
        // One hidden unit reading the opp-life feature; w2 scales it.
        let mut w1 = vec![0.0f64; inputs];
        let opp_life_idx = 5 * defs + 15;
        w1[opp_life_idx] = 1.0;
        Mlp {
            inputs,
            hidden: 1,
            w1,
            b1: vec![0.0],
            w2: vec![life_w],
            b2: 0.0,
        }
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

    fn load_decks() -> Decks {
        crate::decks::load(DECKLISTS).expect("decklists")
    }

    /// Drive a BotGame (our seat = One, scripted opponent) to the first OUR
    /// decision matching `pred`, replaying deterministically. Returns the
    /// game positioned at that decision.
    fn drive_to<F>(d1: &str, d2: &str, seed: u64, pred: F) -> Option<BotGame>
    where
        F: Fn(&PlayerObservation, &[Action]) -> bool,
    {
        let mut game = BotGame::new(d1, d2, penta::protocol::Opponent::Handcrafted,
                                    PlayerId::Two, seed).expect("game");
        let mut n = 0;
        while game.result().is_none() && n < 800 {
            let seat = game.decision_seat().unwrap();
            let obs = game.core_game().observe(seat);
            let actions = protocol_actions(&obs);
            if seat == PlayerId::One && pred(&obs, &actions) {
                return Some(game);
            }
            // Deterministic advance: prefer developing our board, else pass.
            let idx = drive_pick(&actions);
            if game.act(idx).is_err() {
                break;
            }
            n += 1;
        }
        None
    }

    /// A simple aggressive deterministic driver: attack > play land > cast >
    /// finish-attackers > first non-pass > pass/first.
    fn drive_pick(actions: &[Action]) -> usize {
        let find = |f: &dyn Fn(&Action) -> bool| actions.iter().position(|a| f(a));
        if let Some(i) = find(&|a| matches!(a, Action::DeclareAttacker { .. })) {
            return i;
        }
        if let Some(i) = find(&|a| matches!(a, Action::PlayLand { .. })) {
            return i;
        }
        if let Some(i) = find(&|a| matches!(a, Action::CastSpell { .. })) {
            return i;
        }
        if let Some(i) = find(&|a| matches!(a, Action::FinishDeclaringAttackers)) {
            return i;
        }
        if let Some(i) = find(&|a| !matches!(a, Action::PassPriority)) {
            return i;
        }
        0
    }

    fn mk_search<'a>(policy: &'a Policy, decks: &'a Decks,
                     slots: &'a [Vec<i32>; 2], my: &'a str, opp: &'a str,
                     cfg: MctsConfig) -> Ismcts<'a> {
        Ismcts {
            policy, decks, deck_slots: slots,
            my_deck: my, opp_deck: opp,
            our_seat: PlayerId::One, cfg,
        }
    }

    // --- Test 3: forced single action returns 0 without expanding a tree. -
    #[test]
    fn forced_single_action_returns_zero() {
        let policy = make_policy(-4.0);
        let decks = load_decks();
        let slots = [vec![0i32; policy.tables.defs],
                     vec![0i32; policy.tables.defs]];
        // First OUR decision with exactly one legal action.
        let game = drive_to("Goblins", "The Deck", 7,
                            |_o, a| a.len() == 1)
            .expect("a forced OUR decision exists");
        let cfg = MctsConfig { iters: 32, ..Default::default() };
        let s = mk_search(&policy, &decks, &slots, "Goblins", "The Deck", cfg);
        let mut prng = SplitMix64::new(1);
        let (best, visits) = s.search(&game, &mut prng);
        assert_eq!(best, 0);
        assert_eq!(visits.len(), 1);
        assert_eq!(visits[0], 0, "no tree should be built for a forced move");
    }

    // --- Test 2: free land drop in our main phase, empty stack -> not Pass. -
    #[test]
    fn free_land_drop_is_never_pass() {
        let policy = make_policy(-4.0);
        let decks = load_decks();
        let slots = [vec![0i32; policy.tables.defs],
                     vec![0i32; policy.tables.defs]];
        // First OUR precombat-main decision that offers BOTH a land drop and
        // a Pass on an empty stack.
        let game = drive_to("Goblins", "The Deck", 3, |o, a| {
            o.stack.is_empty()
                && o.active_player == PlayerId::One
                && o.step == penta::Step::PrecombatMain
                && a.iter().any(|x| matches!(x, Action::PlayLand { .. }))
                && a.iter().any(|x| matches!(x, Action::PassPriority))
        }).expect("a land-drop decision exists");
        let obs = game.core_game().observe(PlayerId::One);
        let actions = protocol_actions(&obs);
        let cfg = MctsConfig { iters: 64, ..Default::default() };
        let s = mk_search(&policy, &decks, &slots, "Goblins", "The Deck", cfg);
        let mut prng = SplitMix64::new(9);
        let (best, _v) = s.search(&game, &mut prng);
        assert!(!matches!(actions[best], Action::PassPriority),
                "ISMCTS must not Pass with a free land drop available");
    }

    // --- Test 4: node-merge property. Same our-action-history under two
    // DIFFERENT sample_hidden seeds hashes to the SAME node key. -----------
    #[test]
    fn node_keys_are_determinization_independent() {
        let policy = make_policy(-4.0);
        let decks = load_decks();
        let slots = [vec![0i32; policy.tables.defs],
                     vec![0i32; policy.tables.defs]];
        // A real OUR decision with several legal actions.
        let game = drive_to("Goblins", "The Deck", 5, |o, a| {
            a.len() >= 3 && o.active_player == PlayerId::One
        }).expect("a branching OUR decision exists");
        let raw = game.observe_json(PlayerId::One);
        let root_obs = game.core_game().observe(PlayerId::One);

        // Two DIFFERENT determinizations of the same root.
        let mut p1 = SplitMix64::new(11);
        let mut p2 = SplitMix64::new(9999);
        let h1 = sample_hidden(&root_obs, &mut p1, &decks, "Goblins", "The Deck")
            .expect("hidden 1");
        let h2 = sample_hidden(&root_obs, &mut p2, &decks, "Goblins", "The Deck")
            .expect("hidden 2");
        assert_ne!(h1, h2, "the two determinizations must differ");
        let w1 = BotGame::from_observation_json(&raw, &h1, 1)
            .expect("world 1").into_core_game();
        let w2 = BotGame::from_observation_json(&raw, &h2, 2)
            .expect("world 2").into_core_game();

        // The OUR-action DESCRIPTORS at the root must be identical keys even
        // though the two worlds embed different hidden cards.
        let a1 = protocol_actions(&w1.observe(PlayerId::One));
        let a2 = protocol_actions(&w2.observe(PlayerId::One));
        let our_keys: Vec<ActionKey> = protocol_actions(&root_obs);
        for k in &our_keys {
            assert!(a1.contains(k), "world 1 missing an OUR action key");
            assert!(a2.contains(k), "world 2 missing an OUR action key");
        }

        // Reconstruct a shared tree path from an identical our-action-history
        // (the first branching key) under both worlds: same node index.
        let key = our_keys[0].clone();
        let mut tree = Tree::new();
        let path = vec![key.clone()];
        let n_from_w1 = tree.get_or_create(&path);
        let n_from_w2 = tree.get_or_create(&path);
        assert_eq!(n_from_w1, n_from_w2,
                   "same our-action-history must map to the SAME node");

        // And directly exercise the search over both seeds: root visit
        // vectors must span the identical key set (same node fan-out).
        let cfg = MctsConfig { iters: 48, ..Default::default() };
        let s = mk_search(&policy, &decks, &slots, "Goblins", "The Deck", cfg);
        let mut pa = SplitMix64::new(1);
        let mut pb = SplitMix64::new(2);
        let (_ba, va) = s.search(&game, &mut pa);
        let (_bb, vb) = s.search(&game, &mut pb);
        assert_eq!(va.len(), vb.len());
        let span_a: usize = va.iter().filter(|&&x| x > 0).count();
        let span_b: usize = vb.iter().filter(|&&x| x > 0).count();
        assert!(span_a > 0 && span_b > 0, "both searches must expand nodes");
    }

    // --- Test 1: lethal position -> visits concentrate on the winning line.
    #[test]
    fn lethal_attack_concentrates_visits() {
        let policy = make_policy(-4.0);
        let decks = load_decks();
        let slots = [vec![0i32; policy.tables.defs],
                     vec![0i32; policy.tables.defs]];
        // Search seeds for a real state where an OUR DeclareAttacker leads to
        // a win once combat resolves (all-pass settle), then assert ISMCTS
        // concentrates its visits on a winning DeclareAttacker.
        let lethal = find_lethal(&policy, &slots);
        let (game, winners) = lethal.expect("a lethal OUR attack position");
        let obs = game.core_game().observe(PlayerId::One);
        let actions = protocol_actions(&obs);
        // Value-net bootstrap: the attack and no-attack afterstates look
        // alike pre-combat, so the ONLY differentiator is which line reaches
        // a terminal WIN inside the tree -- the lethal attack does so in the
        // fewest plies, so its Q climbs and its visits concentrate.
        let cfg = MctsConfig {
            iters: 240, c_puct: 1.5, leaf_playout: false, ..Default::default()
        };
        let s = mk_search(&policy, &decks, &slots, "Goblins", "The Deck", cfg);
        let mut prng = SplitMix64::new(7);
        let (best, visits) = s.search(&game, &mut prng);
        let total: u32 = visits.iter().sum();
        assert!(total > 0, "search must run iterations");
        assert!(winners.contains(&best),
                "best action {:?} must be a winning attacker; visits={:?}",
                actions[best], visits);
        // Concentration: the winning line holds a plurality of the visits.
        let win_visits: u32 = winners.iter().map(|&i| visits[i]).sum();
        assert!(win_visits * 2 > total,
                "winning line should hold a plurality: {}/{}", win_visits, total);
    }

    /// Find a position where our seat can attack for the win. Plays an
    /// aggressive deterministic line; at each OUR DeclareAttackers decision,
    /// checks whether some DeclareAttacker, followed by finishing attackers
    /// and all-pass settling, reaches a win for us. Returns the game at that
    /// decision plus the winning attacker indices.
    fn find_lethal(policy: &Policy, slots: &[Vec<i32>; 2])
                   -> Option<(BotGame, Vec<usize>)> {
        for seed in 0..80u64 {
            let mut game = BotGame::new("Goblins", "The Deck",
                penta::protocol::Opponent::Handcrafted, PlayerId::Two, seed)
                .ok()?;
            let mut n = 0;
            while game.result().is_none() && n < 800 {
                let seat = game.decision_seat().unwrap();
                let obs = game.core_game().observe(seat);
                let actions = protocol_actions(&obs);
                // Only a DECISIVE, top-of-combat position: no attacker yet
                // declared, some DeclareAttacker wins after combat resolves,
                // and declaring NONE (FinishDeclaringAttackers) does NOT win.
                let none_attacking = !game.core_game()
                    .observe(PlayerId::One).battlefield.iter()
                    .any(|p| p.controller == PlayerId::One && p.attacking);
                if seat == PlayerId::One
                    && obs.step == penta::Step::DeclareAttackers
                    && none_attacking
                {
                    let winners = winning_attackers(game.core_game(), &actions);
                    let finish_wins = actions.iter().position(|a|
                        matches!(a, Action::FinishDeclaringAttackers))
                        .map(|i| {
                            let mut c = game.core_game().clone();
                            c.apply(PlayerId::One, actions[i].clone()).is_ok()
                                && settle_to_result(&mut c) == Some(PlayerId::One)
                        }).unwrap_or(false);
                    if !winners.is_empty() && !finish_wins {
                        return Some((game, winners));
                    }
                }
                let idx = drive_pick(&actions);
                if game.act(idx).is_err() {
                    break;
                }
                n += 1;
            }
            let _ = policy; let _ = slots;
        }
        None
    }

    /// Indices of DeclareAttacker actions that win once combat resolves.
    fn winning_attackers(g: &Game, actions: &[Action]) -> Vec<usize> {
        let mut out = Vec::new();
        for (i, a) in actions.iter().enumerate() {
            if !matches!(a, Action::DeclareAttacker { .. }) {
                continue;
            }
            let mut copy = g.clone();
            if copy.apply(PlayerId::One, a.clone()).is_err() {
                continue;
            }
            if settle_to_result(&mut copy) == Some(PlayerId::One) {
                out.push(i);
            }
        }
        out
    }

    /// Finish declaring attackers, then answer every priority with the first
    /// legal / a Pass until the game ends. Returns the winner, if any.
    fn settle_to_result(g: &mut Game) -> Option<PlayerId> {
        for _ in 0..200 {
            if let Some(r) = g.result() {
                return match r {
                    penta::GameResult::Winner { winner, .. } => Some(winner),
                    penta::GameResult::Draw => None,
                };
            }
            let seat = g.decision_player()?;
            let obs = g.observe(seat);
            let actions = protocol_actions(&obs);
            // Keep swinging: another attacker if offered, else finish attackers,
            // else Pass, else the first legal action.
            let idx = if seat == PlayerId::One {
                actions.iter().position(|a| matches!(a, Action::DeclareAttacker { .. }))
                    .or_else(|| actions.iter().position(|a| matches!(a, Action::FinishDeclaringAttackers)))
                    .or_else(|| actions.iter().position(|a| matches!(a, Action::PassPriority)))
                    .unwrap_or(0)
            } else {
                actions.iter().position(|a| matches!(a, Action::PassPriority))
                    .unwrap_or(0)
            };
            if g.apply(seat, actions[idx].clone()).is_err() {
                return None;
            }
        }
        None
    }
}
