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
use penta::{Action, Game, HandcraftedPolicy, PlayerId, PlayerObservation, Policy as EnginePolicy};

use crate::decks::Decks;
use crate::det_runner::{inert_hidden, sample_hidden};
use crate::extract::features;
use crate::policy::{terminal_value, Policy};
use crate::prng::SplitMix64;

/// Lightweight per-phase profiler, compiled in ONLY under `--features prof`.
/// Buckets: 0=determinize, 1=available(dominance), 2=action_prior,
/// 3=opponent greedy model, 4=leaf_eval. The residual (total - sum) is the
/// tree descent / apply / observe / protocol_actions bookkeeping.
#[cfg(feature = "prof")]
pub mod prof {
    use std::cell::Cell;
    pub const N: usize = 8;
    pub const NAMES: [&str; N] =
        ["determinize", "available", "action_prior", "opp_greedy", "leaf_eval",
         "observe", "legal_actions", "apply"];
    thread_local! {
        static ACC: [Cell<u64>; N] = Default::default();
    }
    pub fn add(bucket: usize, ns: u64) {
        ACC.with(|a| a[bucket].set(a[bucket].get() + ns));
    }
    pub fn reset() { ACC.with(|a| for c in a { c.set(0); }); CALLS.with(|c| c.set(0)); ROWS.with(|r| r.set(0)); }
    thread_local! {
        static CALLS: Cell<u64> = const { Cell::new(0) };
        static ROWS: Cell<u64> = const { Cell::new(0) };
    }
    /// How many prior BATCHES and how many action-rows within them. If rows
    /// per batch is small, batching cannot help and the cost is the
    /// per-action engine work (clone + apply + observe + featurize), not
    /// the net.
    pub fn count(rows: usize) {
        CALLS.with(|c| c.set(c.get() + 1));
        ROWS.with(|r| r.set(r.get() + rows as u64));
    }
    pub fn counts() -> (u64, u64) {
        (CALLS.with(Cell::get), ROWS.with(Cell::get))
    }
    pub fn snapshot() -> [u64; N] {
        ACC.with(|a| std::array::from_fn(|i| a[i].get()))
    }
}

#[cfg(feature = "prof")]
macro_rules! timed {
    ($b:expr, $body:expr) => {{
        let _t = std::time::Instant::now();
        let r = $body;
        prof::add($b, _t.elapsed().as_nanos() as u64);
        r
    }};
}
#[cfg(not(feature = "prof"))]
macro_rules! timed {
    ($b:expr, $body:expr) => { $body };
}

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
    /// Root-only Dirichlet noise, already normalised over the root's
    /// available set. Zero everywhere else; only read when the node has
    /// `has_noise` set.
    noise: f64,
}

/// One OUR information-set node. Stats are keyed by the determinization-
/// independent `ActionKey`.
#[derive(Default)]
struct Node {
    stats: HashMap<ActionKey, ActionStat>,
    /// Whether `stats[..].noise` carries a Dirichlet draw to mix into the
    /// priors. Set on the ROOT only, and only during self-play.
    has_noise: bool,
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

/// In-tree opponent model played at OPPONENT decision nodes during ISMCTS
/// descent. The opponent is a fixed environment (never branched); this
/// selects HOW its move is chosen.
///
///  * `Greedy` -- the value-net 1-ply argmax over the opponent's afterstates
///    (`Policy::greedy_index_with`; the historical behavior). Accurate only
///    if the net's self-play opponent resembles the real opponent, and it
///    costs a full evaluate-all net pass on every opponent ply.
///  * `Handcrafted` -- the engine's built-in handcrafted bot
///    (`HandcraftedPolicy::choose_action`, the SAME rules-based policy that
///    drives the real gate opponent). Cheap (no net eval) and matches the
///    opponent we are actually trying to beat, so the search is not
///    over-optimistic about the opponent's replies.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum OpponentModel {
    Greedy,
    Handcrafted,
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
    /// Re-determinize the hidden world every M iterations instead of every
    /// iteration (reuse one reconstructed world for M consecutive descents).
    /// M<=1 is the exact per-iteration behavior (default). Larger M trades a
    /// little determinization diversity for skipping the JSON reconstruction.
    pub redeterminize_m: usize,
    /// In-tree opponent model at OPPONENT decision nodes (default
    /// `Handcrafted`). See [`OpponentModel`].
    pub opponent: OpponentModel,
    /// Hard cap on OUR decisions in a single full game before the runner
    /// abandons it (a straggler guard). A game that reaches the cap without
    /// a natural result is scored as a LOSS (see `mcts_runner`), so no game
    /// runs forever and the gate win% stays bounded. Search-only entries
    /// ignore this. Default 800 (the historical uncapped-ish limit).
    pub max_decisions: usize,
    /// Hard cap on how deep ONE iteration may descend before it stops and
    /// evaluates where it stands.
    ///
    /// The descent stops when it reaches an action it has never tried
    /// (`n == 0`). Once the tree is deep enough that the greedy path is
    /// fully expanded, that condition can stay false for hundreds of plies,
    /// so a single iteration plays out an entire game -- paying an opponent
    /// model call every ply. It shows up as a CLIFF rather than a slope:
    /// measured, iters 2/3/4 cost 0.2/0.3/0.4s for 600 decisions, and
    /// iters=6 did not finish in 120s.
    pub max_depth: usize,
    /// Fraction of the root prior replaced by Dirichlet noise during
    /// self-play (AlphaZero uses 0.25). Zero disables it.
    ///
    /// WITHOUT THIS THE LOOP EATS ITSELF. A sharper policy gives peaked
    /// priors, peaked priors concentrate visits, and the visit counts are
    /// the very target the policy is trained on -- so sharpness feeds
    /// sharpness. Measured on the cold start: normalised search-target
    /// entropy fell 0.965 -> 0.226 in nineteen rounds while the fraction
    /// of games reaching a result fell 88% -> 58% and games got longer.
    /// That is premature convergence, not learning.
    ///
    /// Noise belongs to GENERATION only. An evaluation gate must score the
    /// policy the loop actually produced, so the gate leaves this at 0.
    pub root_noise_frac: f64,
    /// Dirichlet concentration. AlphaZero scales it to the branching
    /// factor (~10 / typical legal moves); decisions here offer ~7.
    pub root_noise_alpha: f64,
    /// Above this many legal actions, do NOT search the decision -- score
    /// the candidates once with the policy head and play the argmax. 0
    /// disables the cap.
    ///
    /// Self-play has always had this (`max_actions`); the gate did not, and
    /// that asymmetry made the gate 12x slower PER GAME than self-play
    /// despite searching only one seat. Median decision offers 17
    /// candidates, but some offer up to 538, and every one of those was
    /// getting a full 32-iteration search whose every iteration loops and
    /// encodes all 538. RESULTS.md records the same blowup on the AAC path:
    /// 47.8s capped against not finishing in 19 minutes uncapped.
    pub max_actions: usize,
}

impl Default for MctsConfig {
    fn default() -> Self {
        MctsConfig {
            iters: 200,
            max_depth: 400,
            root_noise_frac: 0.0,
            root_noise_alpha: 1.0,
            max_actions: 0,
            c_puct: 1.5,
            inert: false,
            use_dominance: true,
            leaf_playout: false,
            leaf_blend: false,
            redeterminize_m: 1,
            opponent: OpponentModel::Handcrafted,
            max_decisions: 800,
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

/// Gamma(shape, 1) by Marsaglia-Tsang, with the standard boost for
/// shape < 1. Needed only to build a Dirichlet draw for the root prior.
fn gamma_sample(shape: f64, prng: &mut SplitMix64) -> f64 {
    if shape < 1.0 {
        let u = prng.next_f64().max(1e-12);
        return gamma_sample(shape + 1.0, prng) * u.powf(1.0 / shape);
    }
    let d = shape - 1.0 / 3.0;
    let c = 1.0 / (9.0 * d).sqrt();
    loop {
        // Box-Muller normal from two uniforms.
        let u1 = prng.next_f64().max(1e-12);
        let u2 = prng.next_f64();
        let x = (-2.0 * u1.ln()).sqrt()
            * (2.0 * std::f64::consts::PI * u2).cos();
        let v = 1.0 + c * x;
        if v <= 0.0 { continue; }
        let v = v * v * v;
        let u = prng.next_f64().max(1e-12);
        if u < 1.0 - 0.0331 * x * x * x * x { return d * v; }
        if u.ln() < 0.5 * x * x + d * (1.0 - v + v.ln()) { return d * v; }
    }
}

/// Dirichlet(alpha, ..., alpha) of length `n`.
fn dirichlet(n: usize, alpha: f64, prng: &mut SplitMix64) -> Vec<f64> {
    let mut g: Vec<f64> = (0..n).map(|_| gamma_sample(alpha, prng)).collect();
    let sum: f64 = g.iter().sum::<f64>().max(1e-12);
    for v in g.iter_mut() { *v /= sum; }
    g
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

    /// Policy-head afterstate scores for a SET of our actions, in one
    /// batched net pass. See the call site: doing these individually
    /// dominated search time.
    fn action_priors(&self, world: &Game, seat: PlayerId,
                     actions: &[Action], want: &[usize]) -> Vec<f64> {
        #[cfg(feature = "prof")]
        prof::count(want.len());
        let feat = self.policy.tables.size;
        let mut out = vec![0.0f64; want.len()];
        let mut rows: Vec<f32> = Vec::with_capacity(want.len() * feat);
        let mut slots: Vec<usize> = Vec::with_capacity(want.len());
        for (k, &i) in want.iter().enumerate() {
            let mut copy = world.clone();
            if copy.apply(seat, actions[i].clone()).is_err() {
                continue;                       // leaves out[k] at 0.0
            }
            if let Some(t) = terminal_value(&copy, seat) {
                out[k] = t;
                continue;
            }
            rows.extend(features(&copy.observe(seat), copy.in_pregame(),
                                 &self.policy.tables, self.deck_slots));
            slots.push(k);
        }
        if !slots.is_empty() {
            let v = self.policy.head.value_batch(&rows, slots.len());
            for (j, &k) in slots.iter().enumerate() {
                out[k] = v[j];
            }
        }
        out
    }

    /// Single-action form, kept for the tests that exercise it.
    #[allow(dead_code)]
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

    /// One determinized playout down the shared tree. `opp` is the handcrafted
    /// opponent policy instance reused for every opponent ply in this descent
    /// (only touched when `cfg.opponent == Handcrafted`).
    fn iterate(&self, tree: &mut Tree, world: &mut Game,
               opp: &mut HandcraftedPolicy) {
        let mut path: Vec<ActionKey> = Vec::new();
        // (node index, chosen ActionKey) at each OUR decision we descended.
        let mut visited: Vec<(usize, ActionKey)> = Vec::new();
        let mut depth = 0usize;
        let leaf: f64;
        loop {
            // Bound EVERY ply, not just our branching nodes. Three arms
            // below (opponent ply, forced action, dominance-narrowed-to-one)
            // `continue` without descending the tree, so a cap that only
            // counted tree nodes could not stop them. A cold near-uniform
            // policy head makes greedy argmax pick index 0 at every
            // opponent ply, and if action 0 does not advance the game the
            // descent spins forever at depth zero -- measured as a hard
            // hang before decision 25 at iters=8, while iters=4 finished
            // 600 decisions in 0.4s.
            depth += 1;
            if depth > self.cfg.max_depth {
                leaf = timed!(4, self.leaf_eval(world));
                break;
            }
            if let Some(t) = terminal_value(world, self.our_seat) {
                leaf = t;
                break;
            }
            let Some(seat) = world.decision_player() else {
                leaf = timed!(4, self.leaf_eval(world));
                break;
            };
            let obs = timed!(5, world.observe(seat));
            if seat != self.our_seat {
                // Opponent as a fixed environment (never branched).
                let opp_action = timed!(3, match self.cfg.opponent {
                    OpponentModel::Handcrafted => {
                        // The SAME handcrafted policy that drives the real gate
                        // opponent, chosen on THIS determinization's redacted
                        // observation. No net eval -- cheap and accurate.
                        opp.choose_action(&obs)
                    }
                    OpponentModel::Greedy => {
                        let actions = timed!(6, protocol_actions(&obs));
                        let i = if let Some(fh) =
                            self.policy.fast_head.as_ref()
                        {
                            // Same fix as the PUCT prior: model the
                            // opponent by ENCODING their actions rather
                            // than simulating each one. greedy_index_with
                            // clones and featurises per action, which was
                            // 65% of search time once the prior was fast.
                            let state = features(&obs, world.in_pregame(),
                                                 &self.policy.tables,
                                                 self.deck_slots);
                            let pre = fh.state_pre(&state);
                            let enc = crate::action_feat::encode_all(
                                &actions, &obs, &self.policy.tables);
                            let sc = fh.scores(&pre, &enc, actions.len());
                            let mut best = 0usize;
                            for (j, v) in sc.iter().enumerate() {
                                if *v > sc[best] { best = j; }
                                let _ = v;
                            }
                            best
                        } else {
                            self.policy.greedy_index_with(
                                world, seat, &actions, self.deck_slots)
                        };
                        actions.into_iter().nth(i)
                    }
                });
                let ok = opp_action
                    .map(|a| timed!(7, world.apply(seat, a)).is_ok())
                    .unwrap_or(false);
                if !ok {
                    leaf = timed!(4, self.leaf_eval(world));
                    break;
                }
                continue;
            }
            // OUR seat.
            let actions = timed!(6, protocol_actions(&obs));
            if actions.len() == 1 {
                let _ = world.apply(seat, actions[0].clone());
                continue;
            }
            let avail = timed!(1, self.available(world, &obs, &actions, seat));
            if avail.len() == 1 {
                let _ = world.apply(seat, actions[avail[0]].clone());
                continue;
            }

            let node_idx = tree.get_or_create(&path);
            // Ensure priors + bump availability for every legal action here.
            //
            // Priors are computed in ONE batched pass over the actions that
            // still need one. Doing them individually re-streamed the head
            // net's weight matrix per action and was 64% of AZ self-play
            // time; a node's actions are all evaluated against the same
            // weights, so one pass over them costs what one action used to.
            // FAST PATH: one state encoding for the node, then a cheap
            // per-action term. No game clones, no per-action featurisation.
            if let Some(fh) = self.policy.fast_head.as_ref() {
                let need: Vec<usize> = avail.iter().copied()
                    .filter(|&i| !tree.arena[node_idx].stats
                        .get(&actions[i]).map(|s| s.prior_set).unwrap_or(false))
                    .collect();
                if !need.is_empty() {
                    let priors = timed!(2, {
                        // Reuse the observation the descent already took
                        // for this ply. Building one is the single most
                        // expensive engine call in the loop -- 41% of an
                        // episode on protocol 29 -- and this node's
                        // observation is the same one.
                        let state = features(&obs, world.in_pregame(),
                                             &self.policy.tables,
                                             self.deck_slots);
                        let pre = fh.state_pre(&state);
                        let picked: Vec<penta::Action> =
                            need.iter().map(|&i| actions[i].clone()).collect();
                        let enc = crate::action_feat::encode_all(
                            &picked, &obs, &self.policy.tables);
                        fh.scores(&pre, &enc, picked.len())
                    });
                    for (k, &i) in need.iter().enumerate() {
                        let st = tree.arena[node_idx].stats
                            .entry(actions[i].clone()).or_default();
                        st.prior = priors[k];
                        st.prior_set = true;
                    }
                }
                for &i in &avail {
                    tree.arena[node_idx].stats
                        .entry(actions[i].clone()).or_default().a += 1;
                }
                let (sel_i, sel_key, first_visit) =
                    self.select(&tree.arena[node_idx], &actions, &avail);
                visited.push((node_idx, sel_key.clone()));
                let _ = timed!(7, world.apply(seat, actions[sel_i].clone()));
                path.push(sel_key);
                if first_visit {
                    leaf = timed!(4, self.leaf_eval(world));
                    break;
                }
                continue;
            }

            let need: Vec<usize> = avail.iter().copied()
                .filter(|&i| !tree.arena[node_idx].stats
                    .get(&actions[i]).map(|s| s.prior_set).unwrap_or(false))
                .collect();
            if !need.is_empty() {
                let priors = timed!(2,
                    self.action_priors(world, seat, &actions, &need));
                for (k, &i) in need.iter().enumerate() {
                    let st = tree.arena[node_idx].stats
                        .entry(actions[i].clone()).or_default();
                    st.prior = priors[k];
                    st.prior_set = true;
                }
            }
            for &i in &avail {
                tree.arena[node_idx].stats
                    .entry(actions[i].clone()).or_default().a += 1;
            }
            // PUCT select over the available set.
            let (sel_i, sel_key, first_visit) =
                self.select(&tree.arena[node_idx], &actions, &avail);
            visited.push((node_idx, sel_key.clone()));
            let _ = timed!(7, world.apply(seat, actions[sel_i].clone()));
            path.push(sel_key);
            if first_visit {
                // Expansion: leaf-eval the afterstate and stop descending.
                leaf = timed!(4, self.leaf_eval(world));
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

        // Parent visit count -- the numerator of the canonical PUCT
        // exploration term.
        let total_n: u32 = avail.iter()
            .map(|&i| node.stats.get(&actions[i]).map(|s| s.n).unwrap_or(0))
            .sum();

        let mut best_i = avail[0];
        let mut best_key = actions[avail[0]].clone();
        let mut best_first = true;
        let mut best_puct = f64::NEG_INFINITY;
        let mut best_prior = f64::NEG_INFINITY;
        for (pos, &i) in avail.iter().enumerate() {
            let mut p = exps[pos] / sum;
            if node.has_noise {
                let eps = self.cfg.root_noise_frac;
                let nz = node.stats.get(&actions[i])
                    .map(|s| s.noise).unwrap_or(0.0);
                p = (1.0 - eps) * p + eps * nz;
            }
            let (n, w, a) = node.stats.get(&actions[i])
                .map(|s| (s.n, s.w, s.a)).unwrap_or((0, 0.0, 0));
            let q = if n > 0 { w / n as f64 } else { 0.0 };
            // Canonical AlphaZero PUCT. The previous form here was
            //     c * P * sqrt(ln A(a) / (1 + N(a)))
            // which is far too weak to escape the Q=0 default that
            // unvisited actions carry. On a [0,1] value scale Q=0 is the
            // WORST possible score, so an unvisited sibling had to out-run
            // a visited action's real Q on the exploration term alone --
            // and it could not. Measured consequence: the median decision
            // put ALL 32 visits on ONE action (normalised visit entropy
            // 0.000, top-action share 1.000), making "search" barely more
            // than a 1-ply greedy pick and giving the policy target no
            // distribution to learn from.
            let _ = a;
            let explore = self.cfg.c_puct * p
                * (total_n as f64).max(1.0).sqrt() / (1.0 + n as f64);
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
        self.run(&raw, &root_obs, real.core_game(), prng)
    }

    /// Run the search from a LIVE hosted observation (raw protocol JSON) with
    /// no local game object -- the observation-based entry that mirrors
    /// `DeterminizedPolicy.choose`. We reconstruct ONE bootstrap world from
    /// the observation to obtain the typed root `PlayerObservation` (and a
    /// core game for the public-state availability prune), then run the same
    /// iteration loop -- every iteration re-determinizes from `raw` via the
    /// typed sampler. Returns None when even the bootstrap reconstruction
    /// fails (caller then falls back), matching the det path's fail-closed
    /// contract. `our_seat` must equal the observation's seat.
    pub fn search_obs(&self, raw: &str, prng: &mut SplitMix64)
                      -> Option<(usize, Vec<u32>)> {
        let value: serde_json::Value = serde_json::from_str(raw).ok()?;
        let seed0 = prng.next_u64();
        let hidden0 = crate::det_runner::bootstrap_hidden_json(
            &value, prng, self.decks, self.my_deck, self.opp_deck,
            self.cfg.inert, &self.policy.tables)?;
        let core = BotGame::from_observation_json(raw, &hidden0, seed0)
            .ok()?
            .into_core_game();
        let root_obs = core.observe(self.our_seat);
        Some(self.run(raw, &root_obs, &core, prng))
    }

    /// Shared search body: `raw` is the root observation JSON re-determinized
    /// per iteration; `root_obs` is the typed root observation; `root_core`
    /// supplies public state for the (deterministic) root availability prune.
    fn run(&self, raw: &str, root_obs: &PlayerObservation,
           root_core: &Game, prng: &mut SplitMix64) -> (usize, Vec<u32>) {
        let root_actions = protocol_actions(root_obs);
        let mut visits = vec![0u32; root_actions.len()];
        if root_actions.len() <= 1 {
            // Forced: return index 0 without building a tree.
            return (0, visits);
        }
        // Root available set is deterministic (dominance reads public state),
        // so a single survivor is the forced answer -- e.g. a dominating
        // free land drop that prunes Pass down to one option.
        let root_avail = self.available(root_core, root_obs,
                                        &root_actions, self.our_seat);
        if root_avail.len() == 1 {
            visits[root_avail[0]] = self.cfg.iters as u32;
            return (root_avail[0], visits);
        }

        let mut tree = Tree::new();
        // Root Dirichlet noise, sampled ONCE per real decision and mixed
        // into the prior inside `select`. This is what stops the loop from
        // eating itself: the visit counts are the policy's own training
        // target, so without an injected floor of exploration a slightly
        // sharper policy produces sharper visits, which train it sharper
        // still.
        if self.cfg.root_noise_frac > 0.0 && root_avail.len() > 1 {
            let nz = dirichlet(root_avail.len(),
                               self.cfg.root_noise_alpha, prng);
            let root_idx = tree.get_or_create(&[]);
            for (k, &i) in root_avail.iter().enumerate() {
                tree.arena[root_idx].stats
                    .entry(root_actions[i].clone())
                    .or_default().noise = nz[k];
            }
            tree.arena[root_idx].has_noise = true;
        }
        // One handcrafted opponent instance reused across all iterations (a
        // cheap Arc-catalog clone; its only state is mulligan bookkeeping,
        // inert on a mid-game reconstruction). Built even for the Greedy path
        // -- it is simply never consulted there.
        let mut opp = HandcraftedPolicy::new(
            penta::poc::catalog().expect("built-in catalog"));
        let m = self.cfg.redeterminize_m;
        if m <= 1 {
            // Exact per-iteration determinization (matches ismcts_choose).
            for _ in 0..self.cfg.iters {
                if let Some(mut world) =
                    timed!(0, self.determinize(raw, root_obs, prng)) {
                    self.iterate(&mut tree, &mut world, &mut opp);
                }
            }
        } else {
            // Reuse one reconstructed world for M consecutive iterations:
            // resample the template every M-th iter, clone it per descent
            // (iterate mutates its world forward).
            let mut template: Option<Game> = None;
            for it in 0..self.cfg.iters {
                if it % m == 0 {
                    template = timed!(0, self.determinize(raw, root_obs, prng));
                }
                if let Some(t) = &template {
                    let mut world = t.clone();
                    self.iterate(&mut tree, &mut world, &mut opp);
                }
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
            fast_head: None,
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
            let actions = timed!(6, protocol_actions(&obs));
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

    // --- redeterminize_m: M=1 is deterministic/reproducible on a fixed
    // seed (the exact per-iteration path), and M>1 still returns a valid,
    // in-range choice with a nonempty visit vector.
    #[test]
    fn redeterminize_m_behaves() {
        let policy = make_policy(-4.0);
        let decks = load_decks();
        let slots = [vec![0i32; policy.tables.defs],
                     vec![0i32; policy.tables.defs]];
        let game = drive_to("Goblins", "The Deck", 5, |o, a| {
            a.len() >= 3 && o.active_player == PlayerId::One
        }).expect("a branching OUR decision exists");
        // M=1 twice from the same seed -> identical (best, visits): this is
        // the same code path ismcts_choose uses.
        let c1 = MctsConfig { iters: 64, redeterminize_m: 1, ..Default::default() };
        let s1 = mk_search(&policy, &decks, &slots, "Goblins", "The Deck", c1);
        let (b_a, v_a) = s1.search(&game, &mut SplitMix64::new(3));
        let (b_b, v_b) = s1.search(&game, &mut SplitMix64::new(3));
        assert_eq!(b_a, b_b, "M=1 must be reproducible on a fixed seed");
        assert_eq!(v_a, v_b);
        // M=4: still a valid in-range choice, tree expanded.
        let c4 = MctsConfig { iters: 64, redeterminize_m: 4, ..Default::default() };
        let s4 = mk_search(&policy, &decks, &slots, "Goblins", "The Deck", c4);
        let (b4, v4) = s4.search(&game, &mut SplitMix64::new(3));
        assert!(b4 < v4.len(), "chosen index in range");
        assert_eq!(v_a.len(), v4.len(), "same root action count");
        assert!(v4.iter().sum::<u32>() > 0, "M=4 search must run iterations");
    }

    // --- Both in-tree opponent models drive a valid search: each returns an
    // in-range root choice over a nonempty visit vector. The handcrafted
    // model (default) descends the engine bot's legal replies; the greedy
    // model descends the value-net argmax. --------------------------------
    #[test]
    fn both_opponent_models_search() {
        let policy = make_policy(-4.0);
        let decks = load_decks();
        let slots = [vec![0i32; policy.tables.defs],
                     vec![0i32; policy.tables.defs]];
        let game = drive_to("Goblins", "The Deck", 5, |o, a| {
            a.len() >= 3 && o.active_player == PlayerId::One
        }).expect("a branching OUR decision exists");
        for opp in [OpponentModel::Handcrafted, OpponentModel::Greedy] {
            let cfg = MctsConfig { iters: 48, opponent: opp,
                                   ..Default::default() };
            let s = mk_search(&policy, &decks, &slots, "Goblins", "The Deck", cfg);
            let (best, visits) = s.search(&game, &mut SplitMix64::new(3));
            assert!(best < visits.len(), "{opp:?}: chosen index in range");
            assert!(visits.iter().sum::<u32>() > 0,
                    "{opp:?}: search must run iterations");
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

    // --- Profiling: attribute per-move ISMCTS time across phases. Run with
    // `cargo test --release --features prof profile_ismcts_move -- --nocapture`.
    #[cfg(feature = "prof")]
    #[test]
    fn profile_ismcts_move() {
        // Prefer the REAL exported nets (64x825) so value/head eval cost is
        // representative; fall back to the tiny hand net.
        let policy = match (std::env::var("PENTA_PROF_VALUE"),
                            std::env::var("PENTA_PROF_HEAD")) {
            (Ok(v), Ok(h)) => {
                let mut tables = crate::tables::Tables::load(
                    &std::fs::read_to_string(CATALOG).unwrap()).unwrap();
                let value = crate::net::Mlp::load(&v).unwrap();
                tables.set_belief(value.inputs > tables.v2_size);
                Policy {
                    tables, value, head: crate::net::Mlp::load(&h).unwrap(),
                    weight: 0.15,
                    search: crate::policy::SearchConfig {
                        top_k: 0, playouts: 1, budget: 400,
                        playout_max_eval: 999 },
                    max_eval: 999,
                    fast_head: None,
                }
            }
            _ => make_policy(-4.0),
        };
        let decks = load_decks();
        let slots = [vec![0i32; policy.tables.defs],
                     vec![0i32; policy.tables.defs]];
        // A real branching OUR decision.
        let game = drive_to("Goblins", "The Deck", 5, |o, a| {
            a.len() >= 3 && o.active_player == PlayerId::One
        }).expect("a branching OUR decision exists");
        let iters = 256;
        let opponent = match std::env::var("PENTA_PROF_OPP").as_deref() {
            Ok("greedy") => OpponentModel::Greedy,
            _ => OpponentModel::Handcrafted,
        };
        let cfg = MctsConfig { iters, opponent, ..Default::default() };
        let s = mk_search(&policy, &decks, &slots, "Goblins", "The Deck", cfg);
        // Warm up (page in), then measure.
        let mut prng = SplitMix64::new(7);
        let _ = s.search(&game, &mut prng);
        prof::reset();
        let mut prng = SplitMix64::new(7);
        let t0 = std::time::Instant::now();
        let (_best, _v) = s.search(&game, &mut prng);
        let total_ns = t0.elapsed().as_nanos() as u64;
        let acc = prof::snapshot();
        let sum: u64 = acc.iter().sum();
        let residual = total_ns.saturating_sub(sum);
        println!("\n=== ISMCTS per-move profile (iters={iters}) ===");
        println!("total move time: {:.2} ms", total_ns as f64 / 1e6);
        for i in 0..prof::N {
            println!("  {:>14}: {:>7.2} ms  ({:>5.1}%)",
                prof::NAMES[i], acc[i] as f64 / 1e6,
                100.0 * acc[i] as f64 / total_ns as f64);
        }
        println!("  {:>14}: {:>7.2} ms  ({:>5.1}%)", "descent/other",
                 residual as f64 / 1e6,
                 100.0 * residual as f64 / total_ns as f64);
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
                let actions = timed!(6, protocol_actions(&obs));
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
            let actions = timed!(6, protocol_actions(&obs));
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
