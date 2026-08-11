"""TD(lambda) self-play trainer for penta, external mode, afterstate values.

What is shipped (see README.md for the feasibility numbers behind this):

- Decision policy: epsilon-greedy ROLLOUT SEARCH over afterstates (the
  C++ SPZ recipe's decision-time lookahead, ported now that the engine
  exposes `game.clone()`). Each greedy decision first screens all legal
  actions with the 1-ply afterstate value (candidate action played on a
  clone, resulting observation scored by the net), then takes the top-k
  by that myopic score and runs a PLAYOUT from each: both seats play
  greedy 1-ply with the same net (epsilon 0) until the deciding seat's
  next turn start or a decision budget, and the boundary observation is
  scored by the net from the deciding seat's perspective. The playout
  plays THROUGH blocks and combat damage, so attack-declare afterstates
  finally show their consequence instead of only their cost -- the
  passivity driver of every 1-ply collapse. --search-topk 0 restores the
  plain 1-ply policy.
- HONESTY: clones carry the TRUE hidden state (both libraries, the
  opponent's hand), so playout outcomes leak information the deciding
  seat could not see. That is acceptable for TRAINING/self-improvement
  (both seats are us), but evaluation gates vs the scripted bots carry
  the same leak through the search policy; upstream issue #11 tracks
  determinized clones for honest search-time evaluation.
- Exploration: with probability epsilon the move is an exploration draw;
  a --prior-frac slice of those follows a first_bot-shaped AGGRESSION
  PRIOR (play a land, else cast the biggest thing, else declare an
  attacker, else uniform -- adapted from penta's
  examples/python/first_bot.py, Apache-2.0), the rest stay uniform so
  every action keeps support. Uniform-only exploration never put
  aggression into the replay data.
- Learning: TD(lambda), gamma=1, on each seat's trajectory of chosen
  afterstate values: the last recorded state's target is the 0/1(/0.5)
  outcome z, and going backward
  target[i] = (1-lambda) * v[i+1] + lambda * target[i+1], where v are the
  net's own values recorded at play time. Samples land in a replay ring;
  minibatch SGD with momentum (net.py) trains after every round.
  DEFAULT lambda is 1.0 (pure undiscounted outcome targets, the SPZ C++
  default and its champion --hard-targets setting): penta trajectories run
  40-130 recorded decisions per seat, so per-decision lambda 0.9 left most
  targets with <5% outcome weight -- the net trained on its own bootstrap
  echo and collapsed (audit 2026-08, checks.py). --td-lambda restores
  bootstrapping if wanted.
- Self-play runs both seats with the same net; multiprocessing across
  seeds (one Game per task, one process pool) provides parallelism.
- League play (anti-drift, per the SPZ C++ recipe): with probability
  --league-handcrafted a game is played against penta's built-in
  handcrafted bot, and otherwise with probability LEAGUE_FROZEN_FRACTION
  (total, split across --league-frozen snapshots) the opponent seat is a
  frozen snapshot net playing greedily. Handcrafted games record only the
  learner's seat (the built-in bot's decisions are invisible); frozen-
  snapshot games record BOTH seats -- snapshot trajectories are still true
  outcome-labeled observations (as in the SPZ C++ recipe), with bootstrap
  values re-evaluated by the LEARNER net. Mirror games record both seats
  as before. The mode roll is derived from the game seed, so runs stay
  deterministic per seed.

Forced moves (a single legal action; Concede left legalActions in protocol
1) are played without evaluation or recording; decisions with huge
branching factors evaluate a random sample of at most --max-eval
candidates.

Usage:
    python3 trainer.py --games 3000 --workers 8 --out penta_net.npz
"""

import argparse
import json
import os
import random
import time
from collections import namedtuple
from multiprocessing import Pool

import numpy as np

from extractor import Extractor, import_penta
from net import Net

# League decks: the four originals plus four more spanning archetypes
# (aggro Goblins, midrange Erhnamgeddon, disruption Mono Black, tempo
# Jeskai Aggro), all penta built-ins (penta.deck_names()). PENTA_DECKS
# (comma-separated) overrides, e.g. to reproduce old four-deck gates.
DECKS = tuple(os.environ.get(
    "PENTA_DECKS",
    "Sligh,White Weenie,The Deck,Counterburn,"
    "Goblins,Erhnamgeddon,Mono Black,Jeskai Aggro").split(","))

# Rollout-search knobs (see the module docstring; --search-topk 0 turns
# the whole layer off and restores the 1-ply afterstate policy):
#   top_k            candidates surviving the myopic screen into playouts
#   playouts         playouts per candidate (the engine and the greedy
#                    stand-in policy are deterministic, so playouts past
#                    the first act with playout_epsilon-uniform noise to
#                    decorrelate; 1 keeps the playout pure greedy)
#   budget           max penta-decisions per playout (forced included)
#   playout_max_eval max candidates the in-playout greedy 1-ply evaluates
#   playout_epsilon  uniform-noise rate for playouts 2..n
SearchConfig = namedtuple(
    "SearchConfig", "top_k playouts budget playout_max_eval playout_epsilon")
DEFAULT_SEARCH = SearchConfig(top_k=4, playouts=1, budget=120,
                              playout_max_eval=8, playout_epsilon=0.10)
# Fraction of exploration draws that follow the aggression prior instead
# of the uniform draw (the rest keep full support over legal actions).
PRIOR_FRACTION = 0.5
# 1.0 = pure outcome targets (see module docstring for the collapse
# post-mortem); override with --td-lambda.
TD_LAMBDA = 1.0
# Total probability that a game seats a frozen snapshot (when any are
# given), split evenly across the --league-frozen nets.
LEAGUE_FROZEN_FRACTION = 0.25
# Games that have not ended by here are passing-loop junk. They produce NO
# training samples: scoring them 0.5 (as earlier versions did) let a losing
# seat prefer stalling to the cap over fighting, and capped games -- being
# the longest -- flooded up to ~70% of the replay with 0.5 targets,
# flattening the value net (both 2026-08 curve collapses, lambda 0.9 and
# 1.0, shared this). True engine draws still score 0.5.
MAX_DECISIONS = 600


def eval_budget(depth, max_eval):
    """Replay cost is O(history length) per candidate, so evaluate fewer
    candidates the deeper the game runs; keeps a decision's cost bounded."""
    if depth < 200:
        return max_eval
    if depth < 400:
        return max(4, max_eval // 2)
    return max(4, max_eval // 4)


def make_fork(make_game, history, game=None):
    """A callable producing copies of the live game state.

    Prefers the binding's `clone()` (engine >= 0.5, upstream PR #5; the
    pre-merge spelling `clone_game` is also accepted). The fallback, for
    older bindings, rebuilds `Game(same args, same seed)` and replays
    the recorded action indices -- exact, because the engine is
    deterministic.
    """
    if game is not None:
        if hasattr(game, "clone"):
            return game.clone
        if hasattr(game, "clone_game"):
            return game.clone_game

    def fork():
        copy = make_game()
        for played in history:
            copy.act(played)
        return copy

    return fork


def afterstate_rows(fork, seat, actions, extractor, keep_copies=False):
    """Play each candidate on a forked copy; return feature rows and
    terminal outcomes (None where the game continues). With keep_copies
    the afterstate game copies come back too (for rollout playouts)."""
    rows, terminals, copies = [], [], []
    for action in actions:
        copy = fork()
        copy.act(action["index"])
        result = copy.result()
        obs_after = json.loads(copy.observe(seat))
        rows.append(extractor.features(obs_after))
        copies.append(copy)
        if result is None:
            terminals.append(None)
        elif result == "draw":
            terminals.append(0.5)
        else:
            terminals.append(1.0 if result == seat else 0.0)
    if keep_copies:
        return rows, terminals, copies
    return rows, terminals


# -- no-upside-versus-pass dominance prune -------------------------------
#
# Port of our C++ engine's dominance prune: before the value screen, a
# candidate whose settled afterstate leaves the OPPONENT nowhere worse
# and US nowhere better with at least one strict loss -- compared against
# the no-op pass branch -- is discarded. The scouting corpus caught the
# class of blunder this kills: Ancestral Recall cast AT the opponent
# (seed 9101102 t13), Fireball aimed at our own creature (9100103 t20),
# Strip Mine cracking our own Mountain (9100200 t3) -- all strictly
# dominated by passing, all mispriced inside 1-ply/short-playout noise.
#
# The baseline is the CURRENT observation: a pure pass preserves the
# status quo, and whatever the opponent does afterwards happens in both
# branches (with a scripted opponent the literal pass afterstate already
# contains their response, so it cannot serve as a no-op baseline).
#
# Card-agnostic and rules-derived; every ambiguity FAILS CLOSED (the
# action is kept): non-empty starting stack, settling that hits a real
# branching decision / a terminal / the step budget / a different
# turn-step context (this also covers window-ending situations),
# stat-changed permanents (combat tricks are incomparable, not worse),
# mana-pool gains (rituals), and cards whose rules text signals effects
# invisible to the observation snapshot (extra turns, until-end-of-turn
# grants, prevention, regeneration -- extractor.invisible_upside_defs)
# whenever the play's visible consequence is only our own hand
# shrinking. Tapped/attacking state and mana differences never count as
# "strictly worse". Only CastSpell and ActivateAbility candidates are
# tested (nothing else is prunable), and the only non-pass action is
# never pruned.
#
# LAND-DROP RULE (the mirror direction, the C++ "spend=-1 /
# land-drop-never-worse" port): in our own main phase with an empty
# stack, PASS ITSELF is pruned when a legal PlayLand settles to pure
# development -- our battlefield gains, the hand spend is exactly the
# one land (spend is free), our life/library and the entire opponent
# side untouched. The 8-deck scout sweep showed The Deck skipping 2.9
# land drops per losing game and dying at turn 17 holding City of
# Brass/Tundra it never played: pass looks value-equal to a free drop,
# so frugality starves every big spell. The net still picks WHICH land
# to play (only Pass is removed, never a choice among lands), and any
# side effect (an Ankh-style life hit, a lost permanent) makes the drop
# incomparable so Pass survives -- fail closed, no caps, no card lists.

PRUNE_SETTLE_STEPS = 24
_PRUNABLE_TYPES = frozenset({"CastSpell", "ActivateAbility"})
_MAIN_STEPS = ("PrecombatMain", "PostcombatMain")


def _card_multiset(cards):
    counts = {}
    for card in cards or ():
        counts[card["definition"]] = counts.get(card["definition"], 0) + 1
    return counts


def _sub_multiset(a, b):
    """Every count in `a` is covered by `b`."""
    return all(b.get(key, 0) >= n for key, n in a.items())


def _dominance_state(obs, seat):
    """The comparable slice of one observation, from `seat`'s view.
    Battlefield elements carry (definition, power, toughness) so a
    stat-changed permanent compares as incomparable, never as equal."""
    me = 0 if seat == "p1" else 1
    opp = 1 - me
    seat_names = ("p1", "p2")
    my_bf, opp_bf = {}, {}
    for perm in obs.get("battlefield", ()):
        key = (perm["definition"], perm["power"], perm["toughness"])
        side = my_bf if perm["controller"] == seat_names[me] else opp_bf
        side[key] = side.get(key, 0) + 1
    graveyards = obs.get("graveyards") or ((), ())
    exiles = obs.get("exiles") or ((), ())
    return {
        "turn": obs.get("turn"), "step": obs.get("step"),
        "my_hand": len(obs.get("hand", ())),
        "my_life": obs["life"][me],
        "my_lib": obs["librarySizes"][me],
        "my_bf": my_bf,
        "my_mana": sum(obs["manaPools"][me].values()),
        "opp_hand": obs.get("opponentHandSize", 0),
        "opp_life": obs["life"][opp],
        "opp_bf": opp_bf,
        "opp_gy": _card_multiset(graveyards[opp] if len(graveyards) > opp
                                 else ()),
        "opp_exile": _card_multiset(exiles[opp] if len(exiles) > opp
                                    else ()),
    }


def _settle_all_pass(copy, budget=PRUNE_SETTLE_STEPS):
    """Advance an afterstate copy until its stack is empty, answering
    every priority with PassPriority (forced moves taken as-is). Returns
    True when settled; False FAILS CLOSED (terminal result, a branching
    decision with no pass available, or budget exhausted)."""
    for _ in range(budget):
        if copy.result() is not None:
            return False
        obs = json.loads(copy.observe())
        if not obs.get("stack"):
            return True
        actions = obs["legalActions"]
        if len(actions) == 1:
            copy.act(actions[0]["index"])
            continue
        passes = [a for a in actions if a["type"] == "PassPriority"]
        if not passes:
            return False
        copy.act(passes[0]["index"])
    return False


def _dominated_by_pass(cand, base, invisible_upside):
    """True when the settled candidate state is strictly dominated by the
    status-quo (pass) state. See the prune commentary for the guards."""
    # Same settled context only.
    if cand["turn"] != base["turn"] or cand["step"] != base["step"]:
        return False
    # Opponent nowhere worse: kept all permanents (stats included), hand,
    # life, and every graveyard/exile card they already had.
    if cand["opp_hand"] < base["opp_hand"]:
        return False
    if cand["opp_life"] < base["opp_life"]:
        return False
    if not _sub_multiset(base["opp_bf"], cand["opp_bf"]):
        return False
    if not _sub_multiset(base["opp_gy"], cand["opp_gy"]):
        return False
    if not _sub_multiset(base["opp_exile"], cand["opp_exile"]):
        return False
    # We nowhere better: no gained/changed permanents, hand, life,
    # library, or floating mana (rituals are upside, not waste).
    if cand["my_hand"] > base["my_hand"]:
        return False
    if cand["my_life"] > base["my_life"]:
        return False
    if cand["my_lib"] > base["my_lib"]:
        return False
    if not _sub_multiset(cand["my_bf"], base["my_bf"]):
        return False
    if cand["my_mana"] > base["my_mana"]:
        return False
    # ... and somewhere strictly worse.
    strict = (cand["my_hand"] < base["my_hand"]
              or cand["my_life"] < base["my_life"]
              or cand["my_lib"] < base["my_lib"]
              or cand["my_bf"] != base["my_bf"])
    if not strict:
        return False
    # Invisible-upside guard: a card whose rules text can act outside
    # the compared snapshot (extra turns, until-end-of-turn grants,
    # prevention, regeneration) fails closed.
    return not invisible_upside


def _land_dominates_pass(cand, base):
    """True when a settled land-drop state dominates standing pat: pure
    development only. The hand spend of exactly one card is free (the
    C++ 'spend=-1' rule); everything else must be no worse for us and
    no better for the opponent, with the battlefield strictly growing.
    Any side effect -- a life hit (Ankh-style), a lost or stat-changed
    permanent, an opponent gain -- fails closed and keeps Pass."""
    if cand["turn"] != base["turn"] or cand["step"] != base["step"]:
        return False
    # Us: exactly one card spent, battlefield strictly grew, nothing
    # else declined.
    if cand["my_hand"] != base["my_hand"] - 1:
        return False
    if cand["my_life"] < base["my_life"] or cand["my_lib"] < base["my_lib"]:
        return False
    if not _sub_multiset(base["my_bf"], cand["my_bf"]):
        return False
    if sum(cand["my_bf"].values()) <= sum(base["my_bf"].values()):
        return False
    # Opponent: completely untouched (any change is incomparable).
    return (cand["opp_hand"] == base["opp_hand"]
            and cand["opp_life"] == base["opp_life"]
            and cand["opp_bf"] == base["opp_bf"]
            and cand["opp_gy"] == base["opp_gy"]
            and cand["opp_exile"] == base["opp_exile"])


def dominance_prune(fork, obs, actions, extractor):
    """Drop candidates with no upside versus passing, and Pass itself
    when a free land drop dominates it; see commentary. Always returns
    a non-empty subset of `actions` keeping at least one non-pass
    action (with no pass present, a lone non-pass action, or any failed
    guard the list comes back untouched)."""
    seat = obs["seat"]
    if obs.get("pregame") or obs.get("stack"):
        return actions
    passes = [a for a in actions if a["type"] == "PassPriority"]
    if not passes:
        return actions
    non_pass = [a for a in actions if a["type"] != "PassPriority"]
    base = _dominance_state(obs, seat)
    pruned = set()

    # (a) Candidates strictly dominated by standing pat.
    testable = [a for a in non_pass if a["type"] in _PRUNABLE_TYPES]
    if len(non_pass) >= 2 and testable:
        # Candidate card definitions: CastSpell carries the hand
        # instance, ActivateAbility the battlefield source instance.
        # Unmappable ids count as invisible-upside (fail closed).
        instance_def = {c.get("instance", c.get("objectId")):
                        c["definition"] for c in obs.get("hand", ())}
        instance_def.update({p["instance"]: p["definition"]
                             for p in obs.get("battlefield", ())})
        for action in testable:
            inst = action.get("card", action.get("source"))
            definition = instance_def.get(inst)
            upside = (definition is None
                      or definition in extractor.invisible_upside_defs)
            copy = fork()
            copy.act(action["index"])
            if not _settle_all_pass(copy):
                continue
            cand = _dominance_state(json.loads(copy.observe(seat)), seat)
            if _dominated_by_pass(cand, base, upside):
                pruned.add(action["index"])
        if len(pruned) == len(non_pass):
            # Never remove every non-pass action (fail closed on a sweep).
            pruned = set()

    # (b) Pass dominated by a free land drop (our main phase only). The
    # net keeps the choice among lands; only Pass is removed.
    if obs.get("activeSeat") == seat and obs.get("step") in _MAIN_STEPS:
        for action in non_pass:
            if action["type"] != "PlayLand":
                continue
            copy = fork()
            copy.act(action["index"])
            if not _settle_all_pass(copy):
                continue
            cand = _dominance_state(json.loads(copy.observe(seat)), seat)
            if _land_dominates_pass(cand, base):
                pruned.update(a["index"] for a in passes)
                break

    if not pruned:
        return actions
    return [a for a in actions if a["index"] not in pruned]


def aggression_prior(actions, extractor, rng, obs=None):
    """First_bot-shaped exploration draw: play a land, else cast the
    biggest thing castable, else declare an attacker, else uniform.

    A credited adaptation of the action ORDERING in penta's
    examples/python/first_bot.py (lacker, Apache-2.0) -- that trivial
    heuristic beats the handcrafted bot 73% while uniform-explored 1-ply
    nets collapsed to passivity, so exploration draws from its shape put
    lands, threats, and above all ATTACKS into the replay data.

    Protocol v2 CastSpell actions carry the hand INSTANCE id in "card";
    `obs` (when given) resolves instances to definitions for the power
    lookup. Ids with no hand entry are tried as definitions directly
    (protocol 0/1 shape)."""
    by_type = {}
    for action in actions:
        by_type.setdefault(action["type"], []).append(action)
    if "PlayLand" in by_type:
        return rng.choice(by_type["PlayLand"])
    if "CastSpell" in by_type:
        instance_def = {}
        if obs is not None:
            for card in obs.get("hand", ()):
                inst = card.get("instance", card.get("objectId"))
                instance_def[inst] = card["definition"]
        power = extractor.card_power

        def cast_power(action):
            card = action.get("card")
            return power.get(instance_def.get(card, card), 0)

        return max(by_type["CastSpell"], key=cast_power)
    if "DeclareAttacker" in by_type:
        return rng.choice(by_type["DeclareAttacker"])
    return rng.choice(actions)


def playout_boundary(obs, seat, turn0, was_pregame):
    """True at the first decision of the deciding seat's NEXT turn: penta
    turns alternate seats (turn 1 = p1, turn 2 = p2, ...), so this is the
    first non-pregame decision where `seat` is active on a later turn (or
    on any turn at all when the playout started in the mulligan phase)."""
    if obs.get("pregame"):
        return False
    if obs.get("activeSeat") != seat:
        return False
    return was_pregame or obs.get("turn", 0) > turn0


def playout_value(copy, seat, net, extractor, rng, turn0, was_pregame,
                  search, epsilon=0.0):
    """Greedy playout from an afterstate copy, scored for `seat`.

    Both seats play greedy 1-ply with the same net (the stand-in policy)
    until the deciding seat's next turn start (playout_boundary) or
    `search.budget` penta-decisions, whichever comes first; the boundary
    observation is scored by the net from `seat`'s perspective, and a
    terminal result inside the playout returns the exact outcome. The
    playout runs THROUGH blocks and combat damage, which is the point:
    attack declarations are finally priced by their consequence.

    HONESTY: `copy` is a true-state clone -- both libraries and the
    opponent's hand are real, so the playout outcome leaks hidden
    information. Fine for training (self-improvement, both seats are us);
    flagged for evaluation -- gates vs the scripted bots inherit the leak
    through the search policy. Upstream issue #11 tracks determinized
    clones for honest search-time evaluation.
    """
    for _ in range(search.budget):
        result = copy.result()
        if result is not None:
            if result == "draw":
                return 0.5
            return 1.0 if result == seat else 0.0
        obs = json.loads(copy.observe())
        if playout_boundary(obs, seat, turn0, was_pregame):
            break
        actions = obs["legalActions"]
        if len(actions) == 1:
            copy.act(actions[0]["index"])
            continue
        if epsilon > 0.0 and rng.random() < epsilon:
            copy.act(rng.choice(actions)["index"])
            continue
        acting = obs["seat"]
        if len(actions) > search.playout_max_eval:
            actions = rng.sample(actions, search.playout_max_eval)
        # Greedy 1-ply step: clone-per-candidate afterstates, keep the
        # best candidate's copy as the next playout state (saves re-acting).
        pending_rows, pending_copies = [], []
        fixed = []  # (score, copy) for terminal candidates
        for action in actions:
            after = copy.clone()
            after.act(action["index"])
            result = after.result()
            if result is None:
                pending_rows.append(
                    extractor.features(json.loads(after.observe(acting))))
                pending_copies.append(after)
            elif result == "draw":
                fixed.append((0.5, after))
            else:
                fixed.append((1.0 if result == acting else 0.0, after))
        scored = list(fixed)
        if pending_rows:
            values = net.value_batch(np.stack(pending_rows))
            scored.extend(zip(values, pending_copies))
        copy = max(scored, key=lambda pair: pair[0])[1]
    result = copy.result()
    if result is not None:
        if result == "draw":
            return 0.5
        return 1.0 if result == seat else 0.0
    return net.value(extractor.features(json.loads(copy.observe(seat))))


def choose(fork, obs, net, extractor, rng, epsilon, max_eval, depth,
           search=None, prior_frac=PRIOR_FRACTION, dominance=True):
    """Epsilon-greedy rollout-search choice (1-ply when search is None).

    Returns (action_index, features, value); features/value are None for
    forced moves, which are not recorded in the trajectory. The value of
    a searched choice is its playout score (the record the TD targets
    bootstrap from at lambda < 1).

    Greedy candidates pass through the no-upside-versus-pass dominance
    prune (dominance=False restores the raw screen, e.g. to replay old
    trajectories). Exploration draws are NOT pruned: dominated actions
    stay in the exploration support (checks C3/H4), the prune only
    protects the exploitation path.
    """
    actions = obs["legalActions"]
    if len(actions) == 1:
        return actions[0]["index"], None, None
    seat = obs["seat"]

    if epsilon > 0.0 and rng.random() < epsilon:
        if rng.random() < prior_frac:
            picked = aggression_prior(actions, extractor, rng, obs=obs)
        else:
            picked = rng.choice(actions)
        rows, terms = afterstate_rows(fork, seat, [picked], extractor)
        value = terms[0] if terms[0] is not None else net.value(rows[0])
        return picked["index"], rows[0], value

    budget = eval_budget(depth, max_eval)
    if len(actions) > budget:
        actions = rng.sample(actions, budget)
    if dominance:
        # Always leaves >= 2 actions (pass plus at least one non-pass).
        actions = dominance_prune(fork, obs, actions, extractor)
    rows, terms, copies = afterstate_rows(fork, seat, actions, extractor,
                                          keep_copies=True)
    values = net.value_batch(np.stack(rows))
    scores = [t if t is not None else v for t, v in zip(terms, values)]
    if search is not None and search.top_k > 0 and len(actions) > 1:
        # Myopic screen -> playouts for the top-k. Terminal candidates
        # keep their exact outcome; the final argmax is over the refined
        # top-k only (myopic and playout values are not on one scale).
        turn0 = obs.get("turn", 0)
        was_pregame = bool(obs.get("pregame"))
        order = sorted(range(len(scores)), key=lambda i: scores[i],
                       reverse=True)[:search.top_k]
        refined = [-1.0] * len(scores)
        for i in order:
            if terms[i] is not None:
                refined[i] = terms[i]
                continue
            try:
                total = 0.0
                for p in range(search.playouts):
                    sim = copies[i].clone()
                    total += playout_value(
                        sim, seat, net, extractor, rng, turn0, was_pregame,
                        search,
                        epsilon=0.0 if p == 0 else search.playout_epsilon)
                refined[i] = total / search.playouts
            except ValueError:
                # Upstream engine fault inside a playout clone (the
                # built-in handcrafted policy can return no action, seen
                # on 0.3.0): the CLONE is stuck, not the real game, so
                # fall back to this candidate's myopic screen value.
                refined[i] = float(scores[i])
        scores = refined
    best = int(np.argmax(scores))
    return actions[best]["index"], rows[best], float(scores[best])


def td_targets(values, z, lam=TD_LAMBDA, turns=None, turn_lam=0.0):
    """Backward TD(lambda) targets over one seat's recorded values, gamma=1.
    Mirrors the SPZ C++ target loop with the recorded values as bootstraps.

    With turn_lam > 0 (and `turns`, the recorded TURN index per decision:
    0 in the mulligan phase, else the observation's turn number), lambda
    decay is applied per TURN boundary instead of per decision: crossing
    from turn t to t+1 applies one turn_lam decay step, while decisions
    within one turn pass the tail through undecayed (equivalently, they
    share one decay step and hence one target). Penta trajectories run
    40-130 recorded decisions but only ~5-20 turns, so per-turn decay
    keeps real outcome weight in every target where per-DECISION lambda
    0.9 starved it to <5% (the 2026-08 collapse); `lam` is ignored in
    this mode.
    """
    n = len(values)
    targets = np.empty(n, dtype=np.float32)
    tail = z
    targets[n - 1] = tail
    for i in range(n - 2, -1, -1):
        if turn_lam > 0.0:
            step = turn_lam if turns[i + 1] != turns[i] else 1.0
        else:
            step = lam
        tail = (1.0 - step) * values[i + 1] + step * tail
        targets[i] = tail
    return targets


def play_selfplay_game(net, extractor, penta, d1, d2, seed, epsilon, rng,
                       max_eval, opponent_net=None, learner_seat=None,
                       lam=TD_LAMBDA, search=None, prior_frac=PRIOR_FRACTION,
                       turn_lam=0.0):
    """One external-mode game; returns (rows, targets, stats).

    Mirror mode (opponent_net None) plays and records both seats with the
    learner net. League-frozen mode seats opponent_net on the other seat,
    playing greedily (epsilon 0); its trajectory is still true
    outcome-labeled data and is recorded too (as in the SPZ C++ recipe),
    but with bootstrap values from the LEARNER net, so TD targets never
    mix in a stale net's value scale.
    """
    def make_game():
        return penta.Game(d1, d2, opponent="external", seed=seed)

    game = make_game()
    history = []
    traj = {"p1": ([], [], []), "p2": ([], [], [])}
    while game.result() is None and len(history) < MAX_DECISIONS:
        seat = game.decision_seat()
        obs = json.loads(game.observe())
        fork = make_fork(make_game, history, game)
        if opponent_net is not None and seat != learner_seat:
            index, feats, value = choose(
                fork, obs, opponent_net, extractor, rng,
                epsilon=0.0, max_eval=max_eval, depth=len(history),
                search=search)
            if feats is not None:
                value = net.value(feats)  # learner-net bootstrap
        else:
            index, feats, value = choose(
                fork, obs, net, extractor, rng, epsilon,
                max_eval, depth=len(history), search=search,
                prior_frac=prior_frac)
        game.act(index)
        history.append(index)
        if feats is not None:
            traj[seat][0].append(feats)
            traj[seat][1].append(value)
            traj[seat][2].append(
                0 if obs.get("pregame") else obs.get("turn", 0))

    result = game.result()  # None if the cap was hit
    stats = {"decisions": len(history), "result": result or "cap"}
    if result is None:
        # Capped passing-loop game: no ground-truth outcome, no samples
        # (see MAX_DECISIONS).
        return (np.empty((0, extractor.size), dtype=np.float32),
                np.empty(0, dtype=np.float32), stats)
    rows, targets = [], []
    for seat in ("p1", "p2"):
        feats, values, turns = traj[seat]
        if not feats:
            continue
        z = 0.5 if result == "draw" else (1.0 if result == seat else 0.0)
        rows.append(np.stack(feats))
        targets.append(td_targets(values, z, lam, turns=turns,
                                  turn_lam=turn_lam))
    if rows:
        return np.concatenate(rows), np.concatenate(targets), stats
    return (np.empty((0, extractor.size), dtype=np.float32),
            np.empty(0, dtype=np.float32), stats)


def play_handcrafted_game(net, extractor, penta, d1, d2, seed, epsilon, rng,
                          max_eval, learner_seat, lam=TD_LAMBDA, search=None,
                          prior_frac=PRIOR_FRACTION, turn_lam=0.0):
    """One league game vs penta's built-in handcrafted bot.

    Only the learner's seat is observed and recorded (reconstruction
    replays our own action indices, exactly as gate.py does against the
    seeded built-in opponents). Returns (rows, targets, stats).
    """
    opponent_seat = "p2" if learner_seat == "p1" else "p1"

    def make_game():
        return penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=opponent_seat, seed=seed)

    game = make_game()
    history = []
    feats_list, values, turns = [], [], []
    while game.result() is None and len(history) < MAX_DECISIONS:
        obs = json.loads(game.observe())
        index, feats, value = choose(
            make_fork(make_game, history, game), obs, net, extractor, rng,
            epsilon, max_eval, depth=len(history), search=search,
            prior_frac=prior_frac)
        game.act(index)
        history.append(index)
        if feats is not None:
            feats_list.append(feats)
            values.append(value)
            turns.append(0 if obs.get("pregame") else obs.get("turn", 0))

    result = game.result()  # None if the cap was hit
    stats = {"decisions": len(history), "result": result or "cap"}
    if result is None or not feats_list:
        # Capped: no ground-truth outcome, no samples (see MAX_DECISIONS).
        return (np.empty((0, extractor.size), dtype=np.float32),
                np.empty(0, dtype=np.float32), stats)
    z = 0.5 if result == "draw" else (1.0 if result == learner_seat else 0.0)
    return (np.stack(feats_list),
            td_targets(values, z, lam, turns=turns, turn_lam=turn_lam),
            stats)


# -- multiprocessing plumbing -------------------------------------------

_WORKER = {}


def _worker_init(hidden, frozen_paths, schema_version=2):
    penta = import_penta()
    extractor = Extractor(version=schema_version)
    _WORKER["penta"] = penta
    _WORKER["extractor"] = extractor
    _WORKER["net"] = Net(extractor.size, hidden=hidden, seed=0)
    _WORKER["frozen"] = [Net.load(path) for path in frozen_paths]


def _worker_play(task):
    weights, games, max_eval, lam, search, prior_frac, turn_lam = task
    net = _WORKER["net"]
    net.set_weights(weights)
    extractor = _WORKER["extractor"]
    penta = _WORKER["penta"]
    all_rows, all_targets, all_stats = [], [], []
    for d1, d2, seed, epsilon, mode, frozen_idx, learner_seat in games:
        rng = random.Random(seed * 2654435761 % (2**31))
        try:
            if mode == "handcrafted":
                rows, targets, stats = play_handcrafted_game(
                    net, extractor, penta, d1, d2, seed, epsilon, rng,
                    max_eval, learner_seat, lam=lam, search=search,
                    prior_frac=prior_frac, turn_lam=turn_lam)
            else:
                opponent_net = (_WORKER["frozen"][frozen_idx]
                                if mode == "frozen" else None)
                rows, targets, stats = play_selfplay_game(
                    net, extractor, penta, d1, d2, seed, epsilon, rng,
                    max_eval, opponent_net=opponent_net,
                    learner_seat=learner_seat, lam=lam, search=search,
                    prior_frac=prior_frac, turn_lam=turn_lam)
        except ValueError as error:
            # Upstream engine fault mid-game (seen on 0.3.0: "the scripted
            # opponent returned no action" from the built-in handcrafted
            # policy). The game cannot continue and has no outcome, so it
            # contributes no samples; the round line reports the count.
            rows = np.empty((0, extractor.size), dtype=np.float32)
            targets = np.empty(0, dtype=np.float32)
            stats = {"decisions": 0, "result": "engine-error",
                     "error": f"{d1} vs {d2} seed {seed}: {error}"}
        all_rows.append(rows)
        all_targets.append(targets)
        all_stats.append(stats)
    return np.concatenate(all_rows), np.concatenate(all_targets), all_stats


def matchup(game_number):
    """Rotate through ordered deck pairs (mirrors excluded). This is the
    GATE schedule (uniform) -- the yardstick never changes."""
    pairs = [(a, b) for a in DECKS for b in DECKS if a != b]
    return pairs[game_number % len(pairs)]


N_PAIRS = len(DECKS) * (len(DECKS) - 1)

# Pilot-seat oversampling (--seat-oversample, the deployment package):
# the scouted control-deck pathologies (The Deck 7/128, hoarding) get
# more learner reps by weighting TRAINING pair frequency by both decks'
# seat weights (pair weight = w_a * w_b), which puts The Deck in ~1.8x
# and Counterburn / Jeskai Aggro in ~1.4x as many seats as the plain
# decks. Gates keep the uniform matchup() above. The schedule is a
# deterministic fixed-seed shuffle so runs stay reproducible.
TRAIN_SEAT_WEIGHTS = {"The Deck": 4, "Counterburn": 3, "Jeskai Aggro": 3}
TRAIN_DEFAULT_WEIGHT = 2


def _train_pairs():
    weights = {d: TRAIN_SEAT_WEIGHTS.get(d, TRAIN_DEFAULT_WEIGHT)
               for d in DECKS}
    pairs = [(a, b) for a in DECKS for b in DECKS if a != b
             for _ in range(weights[a] * weights[b])]
    random.Random(97).shuffle(pairs)  # deterministic interleave
    return pairs


TRAIN_PAIRS = _train_pairs()


def matchup_train(game_number):
    """The weighted TRAINING schedule (--seat-oversample)."""
    return TRAIN_PAIRS[game_number % len(TRAIN_PAIRS)]


def learner_seat_for(game_number, period=N_PAIRS):
    """Learner seat for league games. Plain `number % 2` is aliased with
    the pair rotation -- the schedule length is even, so each ordered
    deck pair would ALWAYS put the learner on the same seat. Flipping
    the parity every full rotation gives every pair both seats over two
    rotations. `period` must be the active schedule's length
    (N_PAIRS uniform, len(TRAIN_PAIRS) oversampled).
    """
    return "p1" if (game_number + game_number // period) % 2 == 0 else "p2"


def league_roll(seed, handcrafted_fraction, n_frozen):
    """Deterministic per-seed opponent draw: ("mirror"|"handcrafted"|
    "frozen", frozen_idx). Uses its own RNG stream so play RNG is
    untouched."""
    rng = random.Random(seed * 2246822519 % (2**31))
    roll = rng.random()
    if roll < handcrafted_fraction:
        return "handcrafted", -1
    if n_frozen and roll < handcrafted_fraction + LEAGUE_FROZEN_FRACTION:
        return "frozen", rng.randrange(n_frozen)
    return "mirror", -1


def save_ring(path, replay_X, replay_y, size, cursor, capacity, net):
    """Persist the live replay samples (oldest-first) plus the optimizer's
    momentum state beside the net, so a warm-started chunk does not begin
    on an EMPTY ring (the early updates of every chunk otherwise train
    hard on a small fresh buffer -- the post-audit collapse driver)."""
    if size < capacity:
        order = np.arange(size)
    else:  # cursor is the oldest slot once the ring has wrapped
        order = np.concatenate([np.arange(cursor, capacity),
                                np.arange(cursor)])
    np.savez_compressed(
        path, X=replay_X[order], y=replay_y[order],
        vel_w1=net._vel[0], vel_b1=net._vel[1], vel_w2=net._vel[2],
        vel_b2=np.float64(net._vel[3]))
    print(f"ring: saved {size} samples + momentum -> {path}", flush=True)


def load_ring(path, replay_X, replay_y, capacity, net):
    """Restore a persisted ring (newest samples win if it exceeds
    capacity) and the momentum state. Returns (size, cursor)."""
    data = np.load(path, allow_pickle=False)
    X, y = data["X"], data["y"]
    if X.shape[1] != replay_X.shape[1]:
        print(f"ring: {path} has {X.shape[1]} features, expected "
              f"{replay_X.shape[1]}; starting empty", flush=True)
        return 0, 0
    if len(X) > capacity:
        X, y = X[-capacity:], y[-capacity:]
    size = len(X)
    replay_X[:size] = X
    replay_y[:size] = y
    if all(k in data for k in ("vel_w1", "vel_b1", "vel_w2", "vel_b2")) \
            and data["vel_w1"].shape == net._vel[0].shape:
        net._vel = [data["vel_w1"], data["vel_b1"], data["vel_w2"],
                    float(data["vel_b2"])]
        momentum = " + momentum"
    else:
        momentum = ""
    print(f"ring: resumed {size} samples{momentum} from {path}", flush=True)
    return size, size % capacity


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--games", type=int, default=3000)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--hidden", type=int, default=64)
    parser.add_argument("--lr", type=float, default=0.01)
    parser.add_argument("--batch", type=int, default=256)
    parser.add_argument("--epsilon-start", type=float, default=0.25)
    parser.add_argument("--epsilon-final", type=float, default=0.03)
    parser.add_argument("--max-eval", type=int, default=16)
    parser.add_argument("--search-topk", type=int,
                        default=DEFAULT_SEARCH.top_k,
                        help="candidates surviving the myopic screen into "
                             "rollout playouts; 0 disables search (plain "
                             "1-ply afterstate policy)")
    parser.add_argument("--playouts", type=int,
                        default=DEFAULT_SEARCH.playouts,
                        help="playouts per candidate (2+ adds "
                             "playout-epsilon noise past the first)")
    parser.add_argument("--playout-budget", type=int,
                        default=DEFAULT_SEARCH.budget,
                        help="max penta-decisions per playout before the "
                             "boundary evaluation")
    parser.add_argument("--playout-max-eval", type=int,
                        default=DEFAULT_SEARCH.playout_max_eval,
                        help="candidates the in-playout greedy 1-ply "
                             "evaluates per decision")
    parser.add_argument("--playout-epsilon", type=float,
                        default=DEFAULT_SEARCH.playout_epsilon,
                        help="uniform-noise rate for playouts 2..n")
    parser.add_argument("--prior-frac", type=float, default=PRIOR_FRACTION,
                        help="fraction of exploration draws taken from the "
                             "first_bot-shaped aggression prior (land > "
                             "biggest castable > attack); the rest stay "
                             "uniform")
    parser.add_argument("--td-lambda", type=float, default=TD_LAMBDA,
                        help="TD lambda per recorded decision; 1.0 (default)"
                             " = pure outcome targets, no bootstrapping")
    parser.add_argument("--td-lambda-per-turn", type=float, default=0.0,
                        metavar="LAM",
                        help="TD lambda applied per TURN boundary instead "
                             "of per decision (decisions within one turn "
                             "share the decay step); 0 (default) = off, "
                             "overrides --td-lambda when set")
    parser.add_argument("--replay-capacity", type=int, default=150_000)
    parser.add_argument("--round-games", type=int, default=64)
    parser.add_argument("--seed-base", type=int, default=1_000_000)
    parser.add_argument("--out", default="penta_net.npz")
    parser.add_argument("--init", default="",
                        help="warm-start weights (.npz) to continue from")
    parser.add_argument("--grow-init", action="store_true",
                        help="allow --init nets from an OLDER feature "
                             "schema: schemas are nested, so the input "
                             "weight matrix is grown with zero columns "
                             "for the new features (the net starts "
                             "identical and learns to use them)")
    parser.add_argument("--seat-oversample", action="store_true",
                        help="weighted TRAINING deck schedule (The Deck "
                             "~1.8x, Counterburn/Jeskai ~1.4x seat share); "
                             "gates keep the uniform rotation")
    parser.add_argument("--ring", default="", metavar="PATH",
                        help="replay-ring persistence file (.npz): loaded "
                             "at start when it exists, saved at exit, so "
                             "warm-started chunks do not begin on an empty "
                             "ring (also carries SGD momentum state)")
    parser.add_argument("--lr-warmup", type=int, default=0, metavar="N",
                        help="linear learning-rate warmup over the first N "
                             "SGD updates (softens the fresh-small-buffer "
                             "shock of a warm-started chunk)")
    parser.add_argument("--league-frozen", action="append", default=[],
                        metavar="PATH",
                        help="frozen snapshot net for league games "
                             "(repeatable; ~25%% of games total seat one "
                             "as the opponent)")
    parser.add_argument("--league-handcrafted", type=float, default=0.0,
                        metavar="FRACTION",
                        help="fraction of games played vs the built-in "
                             "handcrafted bot")
    args = parser.parse_args()

    if args.init:
        # Feature schemas are versioned by input size: dispatch on the
        # warm-start net so old (v1, 675-input) nets keep their layout.
        net = Net.load(args.init)
        extractor = Extractor.for_inputs(net.inputs)
        if args.grow_init and extractor.version < Extractor().version:
            # Schemas are nested (v3 = v2 + block, ...): grow the input
            # weight matrix with zero columns so the old net transfers
            # exactly, then learns the new features from zero.
            target = Extractor()
            pad = np.zeros((net.w1.shape[0], target.size - net.inputs))
            net.w1 = np.hstack([net.w1, pad])
            net._vel[0] = np.zeros_like(net.w1)
            print(f"grow-init: {args.init} grown v{extractor.version} "
                  f"({net.w1.shape[1] - pad.shape[1]}) -> "
                  f"v{target.version} ({target.size}) with zero columns")
            extractor = target
    else:
        extractor = Extractor()
        net = Net(extractor.size, hidden=args.hidden, seed=1)
    for path in args.league_frozen:
        frozen_inputs = Net.load(path).inputs
        if frozen_inputs != extractor.size:
            raise SystemExit(
                f"frozen league net {path} has {frozen_inputs} inputs but "
                f"the learner's schema v{extractor.version} has "
                f"{extractor.size}; never seat a sparring net whose "
                f"feature schema differs from the learner")
    search = None
    if args.search_topk > 0:
        search = SearchConfig(
            top_k=args.search_topk, playouts=args.playouts,
            budget=args.playout_budget,
            playout_max_eval=args.playout_max_eval,
            playout_epsilon=args.playout_epsilon)
    td_desc = (f"per-turn {args.td_lambda_per_turn:.2f}"
               if args.td_lambda_per_turn > 0.0
               else f"{args.td_lambda:.2f}")
    print(f"engine {extractor.engine_version} protocol "
          f"{extractor.protocol_version}; features {extractor.size} "
          f"(schema v{extractor.version}: {extractor.defs} defs x 5 zones + "
          f"{extractor.n_scalars} scalars"
          + (" + castability/race block" if extractor.version >= 2 else "")
          + (" + deployment block" if extractor.version >= 3 else "")
          + f"); decks {len(DECKS)}"
          + (" (seat-oversampled: The Deck ~1.8x, CB/JA ~1.4x)"
             if args.seat_oversample else "")
          + f"; td-lambda {td_desc}")
    print(f"search: {search if search else 'off (1-ply afterstates)'}; "
          f"prior-frac {args.prior_frac:.2f}")
    if args.league_handcrafted or args.league_frozen:
        frozen_frac = LEAGUE_FROZEN_FRACTION if args.league_frozen else 0.0
        print(f"league: handcrafted {args.league_handcrafted:.2f}, frozen "
              f"{frozen_frac:.2f} across {len(args.league_frozen)} "
              f"snapshot(s) {args.league_frozen}")

    replay_X = np.empty((args.replay_capacity, extractor.size), dtype=np.float32)
    replay_y = np.empty(args.replay_capacity, dtype=np.float32)
    replay_size = 0
    replay_cursor = 0
    if args.ring and os.path.exists(args.ring):
        replay_size, replay_cursor = load_ring(
            args.ring, replay_X, replay_y, args.replay_capacity, net)
    train_rng = np.random.default_rng(7)
    updates_done = 0

    played = 0
    t_start = time.time()
    with Pool(args.workers, initializer=_worker_init,
              initargs=(args.hidden, tuple(args.league_frozen),
                        extractor.version)) as pool:
        while played < args.games:
            round_n = min(args.round_games, args.games - played)
            frac = played / max(1, args.games)
            epsilon = (args.epsilon_start
                       + (args.epsilon_final - args.epsilon_start) * frac)
            game_specs = []
            for g in range(round_n):
                number = played + g
                if args.seat_oversample:
                    d1, d2 = matchup_train(number)
                    learner_seat = learner_seat_for(
                        number, period=len(TRAIN_PAIRS))
                else:
                    d1, d2 = matchup(number)
                    learner_seat = learner_seat_for(number)
                seed = args.seed_base + number
                mode, frozen_idx = league_roll(
                    seed, args.league_handcrafted, len(args.league_frozen))
                game_specs.append(
                    (d1, d2, seed, epsilon, mode, frozen_idx, learner_seat))
            per_worker = [game_specs[w::args.workers]
                          for w in range(args.workers)]
            weights = net.get_weights()
            tasks = [(weights, chunk, args.max_eval, args.td_lambda,
                      search, args.prior_frac, args.td_lambda_per_turn)
                     for chunk in per_worker if chunk]
            results = pool.map(_worker_play, tasks)

            new = 0
            decisions, caps = [], 0
            engine_errors = []
            for rows, targets, stats in results:
                for s in stats:
                    if s["result"] == "engine-error":
                        engine_errors.append(s["error"])
                        continue
                    decisions.append(s["decisions"])
                    caps += s["result"] == "cap"
                for i in range(len(targets)):
                    replay_X[replay_cursor] = rows[i]
                    replay_y[replay_cursor] = targets[i]
                    replay_cursor = (replay_cursor + 1) % args.replay_capacity
                new += len(targets)
            replay_size = min(replay_size + new, args.replay_capacity)
            played += round_n

            steps = max(1, 2 * new // args.batch)
            losses = []
            for _ in range(steps):
                lr = args.lr
                if args.lr_warmup > 0:
                    lr *= min(1.0, (updates_done + 1) / args.lr_warmup)
                pick = train_rng.integers(0, replay_size, size=args.batch)
                losses.append(net.train_batch(
                    replay_X[pick], replay_y[pick], lr))
                updates_done += 1
            net.save(args.out,
                     engine_version=extractor.engine_version,
                     protocol_version=extractor.protocol_version,
                     hidden=args.hidden, games=played,
                     search_topk=args.search_topk,
                     feature_schema=extractor.version,
                     td_lambda_per_turn=args.td_lambda_per_turn,
                     seat_oversample=int(args.seat_oversample))
            rate = played / (time.time() - t_start)
            print(f"games {played:5d}  eps {epsilon:.3f}  "
                  f"loss {np.mean(losses):.4f}  "
                  f"samples {new:5d}  replay {replay_size:6d}  "
                  f"len {np.mean(decisions):5.1f}  caps {caps}  "
                  f"{rate:5.2f} games/s", flush=True)
            for error in engine_errors:
                print(f"      dropped game (engine error): {error}",
                      flush=True)

    if args.ring:
        save_ring(args.ring, replay_X, replay_y, replay_size, replay_cursor,
                  args.replay_capacity, net)
    print(f"done: {played} games -> {args.out}")


if __name__ == "__main__":
    main()
