"""Observation-only policy for hosted play (no game object, no clone).

Hosted rooms hand a bot redacted observations over a wire and never a
game object (docs/bots.md), so the rollout-search recipe cannot run.
This policy scores APPROXIMATE afterstates instead: for each legal
action it predicts the next observation in pure Python -- only the
fields the 825-feature schema actually reads -- and scores it with the
certified pair (value net + policy head, the standard blend). The
prediction deliberately mirrors what the engine's own 1-ply afterstates
looked like during training (a cast sits ON the stack, hand -1; a land
hits the battlefield; a declared attacker is flagged), so the nets see
their native distribution.

What survives from the full recipe: the myopic screen with the policy
blend, and the land-drop dominance rule (pass is never chosen over an
available land drop in our main phase -- the predictor's land afterstate
is pure development by construction). What cannot survive without an
engine: playouts, the settle-based no-upside prune, and Ankh-style
side-effect detection. Ties (actions whose predicted afterstates are
identical, e.g. decision picks) break by the first_bot-shaped
aggression ordering, then by lowest index.

The same class also drives the local protocol-18 gate harness
(gate_hosted.py) so the number reported for the hosted bot is measured
through exactly this code path.
"""

import copy
import json
import os

import numpy as np

from extractor import Extractor
from net import Net

_MAIN_STEPS = ("PrecombatMain", "PostcombatMain")
# Aggression-prior ordering for ties (first_bot-shaped, lower = better).
_TIE_ORDER = {"PlayLand": 0, "CastSpell": 1, "DeclareAttacker": 2,
              "DeclareBlocker": 3, "ActivateAbility": 4,
              "FinishDeclaringAttackers": 5, "FinishDeclaringBlockers": 5,
              "ChooseDecision": 6, "PassPriority": 8}


def _find(cards, object_id):
    for i, card in enumerate(cards):
        if card.get("objectId", card.get("instance")) == object_id or \
                card.get("instance") == object_id:
            return i
    return None


class HostedPolicy:
    def __init__(self, value_path="penta_net.npz",
                 head_path="policy_head.ckpt-dagger1.npz", weight=0.15,
                 shaped=True, our_deck=None,
                 decklists_path="builtin-decklists.json"):
        here = os.path.dirname(os.path.abspath(__file__))
        self.net = Net.load(os.path.join(here, value_path)
                            if not os.path.isabs(value_path) else value_path)
        self.head = Net.load(os.path.join(here, head_path)
                             if not os.path.isabs(head_path) else head_path)
        self.weight = weight
        self.shaped = shaped
        self.extractor = Extractor.for_inputs(self.net.inputs)
        if self.head.inputs != self.net.inputs:
            raise ValueError("value net and policy head disagree on schema")
        # Belief schema needs the two seats' decklists; deck GUESSING is
        # done in belief_deck_context (classify), never in the extractor.
        # `_our_deck` is private on purpose: the gate dispatches on the
        # public `our_deck` attribute (DeterminizedPolicy only), so adding
        # it here must not change HostedPolicy's choose(obs) contract.
        self._our_deck = our_deck
        self._decks = (load_decklists(decklists_path)
                       if getattr(self.extractor, "belief", False) else {})

    # -- approximate afterstates ----------------------------------------

    def predict_afterstate(self, obs, action):
        """A lightweight predicted observation after `action`, touching
        only the fields the feature schema reads. Unknown action types
        return the observation unchanged (their afterstate is then
        indistinguishable and the tie-break ordering decides)."""
        after = dict(obs)
        after.pop("legalActions", None)  # castability zeros, as in training
        kind = action.get("type")
        me = obs.get("seat")

        def drop_from_hand(object_id):
            hand = list(after.get("hand", ()))
            i = _find(hand, object_id)
            if i is not None:
                removed = hand.pop(i)
                after["hand"] = hand
                return removed
            return None

        if kind == "PlayLand":
            card = drop_from_hand(action.get("card"))
            if card is not None:
                bf = list(after.get("battlefield", ()))
                bf.append({"controller": me, "definition":
                           card["definition"], "power": None,
                           "toughness": None, "tapped": False,
                           "attacking": False,
                           "instance": card.get("objectId",
                                                card.get("instance")),
                           "name": card.get("name")})
                after["battlefield"] = bf
        elif kind == "CastSpell":
            if drop_from_hand(action.get("card")) is not None:
                after["stack"] = list(after.get("stack", ())) + [{}]
        elif kind == "ActivateAbility":
            after["stack"] = list(after.get("stack", ())) + [{}]
        elif kind == "ActivateManaAbility":
            idx = 0 if me == "p1" else 1
            pools = copy.deepcopy(obs.get("manaPools",
                                          [{}, {}]))
            color = action.get("color") or "colorless"
            pools[idx][color] = pools[idx].get(color, 0) + 1
            after["manaPools"] = pools
            after["battlefield"] = self._tap(after, action.get("source"))
        elif kind == "PayLifeForMana":
            idx = 0 if me == "p1" else 1
            life = list(obs.get("life", (20, 20)))
            life[idx] -= 1
            after["life"] = life
        elif kind == "DiscardCards":
            gy = [list(pile) for pile in
                  (obs.get("graveyards") or ((), ()))]
            idx = 0 if me == "p1" else 1
            for object_id in action.get("cards", ()):
                card = drop_from_hand(object_id)
                if card is not None:
                    gy[idx].append({"definition": card["definition"],
                                    "name": card.get("name")})
            after["graveyards"] = gy
        elif kind == "DeclareAttacker":
            bf = []
            for perm in obs.get("battlefield", ()):
                if perm.get("objectId", perm.get("instance")) == \
                        action.get("attacker"):
                    perm = dict(perm)
                    perm["attacking"] = True
                bf.append(perm)
            after["battlefield"] = bf
        return after

    @staticmethod
    def _tap(after, source):
        bf = []
        for perm in after.get("battlefield", ()):
            if perm.get("objectId", perm.get("instance")) == source:
                perm = dict(perm)
                perm["tapped"] = True
            bf.append(perm)
        return bf

    # -- choice ----------------------------------------------------------

    def _blend(self, obs, candidates):
        try:
            rows = np.stack([
                self.extractor.features(self.predict_afterstate(obs, a))
                for a in candidates])
            return ((1.0 - self.weight) * self.net.value_batch(rows)
                    + self.weight * self.head.value_batch(rows))
        except Exception:
            return np.zeros(len(candidates))

    def _pick(self, obs, candidates):
        blend = self._blend(obs, candidates)
        cost = self._action_costs(obs, candidates)
        order = sorted(
            range(len(candidates)),
            key=lambda i: (-blend[i],
                           _TIE_ORDER.get(candidates[i].get("type"), 7),
                           -cost[i],
                           candidates[i]["index"]))
        return candidates[order[0]]["index"]

    def _action_costs(self, obs, candidates):
        """Converted cost of each candidate's card (0 where n/a): the
        first_bot 'biggest castable' bias, used as a late tiebreak."""
        hand_defs = {c.get("objectId", c.get("instance")): c["definition"]
                     for c in obs.get("hand", ())}
        costs = []
        for action in candidates:
            definition = hand_defs.get(action.get("card"))
            costs.append(self.extractor.card_cost.get(definition, 0)
                         + action.get("x", 0) if definition else 0)
        return costs

    def _filter_casts(self, obs, casts):
        """Drop known-junk casts, property-derived: an X spell cast with
        x=0 does nothing (the scouted Fireball-dud class the engine-side
        settle prune used to remove). Falls back to the unfiltered list
        if everything got dropped."""
        kept = [a for a in casts
                if (a.get("choices") or {}).get("x", a.get("x", 0)) != 0
                or not self._is_x_spell(obs, a)]
        return kept or casts

    def _is_x_spell(self, obs, action):
        hand_defs = {c.get("objectId", c.get("instance")): c["definition"]
                     for c in obs.get("hand", ())}
        definition = hand_defs.get(action.get("card"))
        return definition in self._x_defs

    @property
    def _x_defs(self):
        if not hasattr(self, "_x_defs_cache"):
            from extractor import pinned_catalog
            self._x_defs_cache = {
                c["definition"] for c in pinned_catalog()["cards"]
                if (c.get("manaCost") or {}).get("variableX")}
        return self._x_defs_cache

    def choose(self, obs):
        """Action index for one observation; never raises on odd shapes
        (falls back to the tie ordering, then index 0).

        SHAPED mode (default; the observation-only hosted fallback): without
        playouts the raw 1-ply blend reverts to the passivity every
        pre-search era collapsed into (measured: 2 casts from 157 cast
        offers, zero attacks, 0/12 vs handcrafted). The cure that
        worked then works here: a first_bot-shaped category order --
        land > cast > attack > the rest -- with the NETS selecting
        WITHIN each category (which land, which cast/target/X, which
        blocker). shaped=False keeps the pure blend for comparison gates.

        NOTE: this docstring used to claim "first_bot's bare ordering
        alone beats handcrafted ~73%". That is FALSE as written -- measured
        2026-08-22 with gate_heuristic.py over 400 games on the same
        protocol the actors use, the bare ordering scores 3.9% +- 1.0.
        Whatever the 73% referred to, it was not this ordering against the
        handcrafted bot in this engine. The ordering is a deadline
        fallback and a tie-break, not a strong policy."""
        actions = obs.get("legalActions") or ()
        if not actions:
            return 0
        if len(actions) == 1:
            return actions[0]["index"]
        if getattr(self.extractor, "belief", False):
            belief_deck_context(self.extractor, self._decks, obs,
                                self._our_deck)
        if not self.shaped:
            candidates = list(actions)
            if (obs.get("activeSeat") == obs.get("seat")
                    and obs.get("step") in _MAIN_STEPS
                    and not obs.get("stack")
                    and any(a.get("type") == "PlayLand"
                            for a in candidates)):
                non_pass = [a for a in candidates
                            if a.get("type") != "PassPriority"]
                if non_pass:
                    candidates = non_pass
            return self._pick(obs, candidates)

        by_type = {}
        for action in actions:
            by_type.setdefault(action.get("type"), []).append(action)
        # Mulligans: Keep/Mulligan afterstates are featurally identical
        # (nothing visible changes until resolution), so the nets cannot
        # judge them; use the land-count rule (keep 2-5 lands).
        if "KeepHand" in by_type:
            lands = sum(1 for c in obs.get("hand", ())
                        if self.extractor.card_kind.get(
                            c["definition"]) == "Land")
            if 2 <= lands <= 5 or "TakeMulligan" not in by_type:
                return by_type["KeepHand"][0]["index"]
            return by_type["TakeMulligan"][0]["index"]
        if "CastSpell" in by_type:
            by_type["CastSpell"] = self._filter_casts(obs,
                                                      by_type["CastSpell"])
        # Category order; the nets choose within the category.
        for kind in ("PlayLand", "CastSpell", "DeclareAttacker"):
            if kind in by_type and by_type[kind]:
                group = by_type[kind]
                return (group[0]["index"] if len(group) == 1
                        else self._pick(obs, group))
        # Blocks: score blockers against declining to block.
        if "DeclareBlocker" in by_type:
            group = by_type["DeclareBlocker"] + by_type.get(
                "FinishDeclaringBlockers", [])
            return self._pick(obs, group)
        # Everything else (decisions, discards, activations, passes):
        # the blend over the full list, tie-ordered.
        return self._pick(obs, list(actions))


def load_decklists(path=None):
    """Built-in decklists as {display name: {definition: count}}."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(path or os.path.join(here, "builtin-decklists.json")) as f:
        raw = json.load(f)
    return {name: {int(d): c for d, c in zone["main"].items()}
            for name, zone in raw.items()}


def seen_defs(obs, seat_name, seat_idx, include_hand):
    """Definitions of `seat_name`'s cards visible in this observation."""
    seen = []
    if include_hand:
        seen += [c["definition"] for c in obs.get("hand", ())]
    seen += [p["definition"] for p in obs.get("battlefield", ())
             if p.get("controller") == seat_name]
    for zone in ("graveyards", "exiles"):
        piles = obs.get(zone) or ((), ())
        if len(piles) > seat_idx:
            seen += [c["definition"] for c in piles[seat_idx]]
    return seen


def unseen_pool(decks, deck_name, seen):
    pool = []
    for d, c in decks.get(deck_name, {}).items():
        pool += [d] * c
    for d in seen:
        if d in pool:
            pool.remove(d)
    return pool


def classify_deck(decks, seen):
    """Best-overlap built-in deck for a list of seen definitions."""
    if not seen:
        return None
    best, best_score = None, -1.0
    for name, deck in decks.items():
        pool = dict(deck)
        hits = 0
        for d in seen:
            if pool.get(d, 0) > 0:
                pool[d] -= 1
                hits += 1
        score = hits / len(seen)
        if score > best_score:
            best, best_score = name, score
    return best


def classify_opponent(decks, obs):
    """Best-overlap guess at the opponent's built-in deck from every card
    of theirs we have seen (their hand stays hidden)."""
    opp = "p2" if obs.get("seat") == "p1" else "p1"
    oi = 0 if opp == "p1" else 1
    return classify_deck(decks, seen_defs(obs, opp, oi, include_hand=False))


def belief_deck_context(extractor, decks, obs, our_deck):
    """Set the belief extractor's per-seat decklists for one observation:
    our_deck (or, if unknown, a classification of our own visible cards)
    for our seat, and the classified opponent deck for theirs. No-op unless
    the extractor carries the belief block. Deck GUESSING lives HERE, in
    the caller -- the extractor only receives decided count arrays."""
    if not getattr(extractor, "belief", False):
        return
    me = obs.get("seat", "p1")
    mi = 0 if me == "p1" else 1
    my_name = our_deck or classify_deck(
        decks, seen_defs(obs, me, mi, include_hand=True))
    opp_name = classify_opponent(decks, obs) or my_name
    my_deck = decks.get(my_name, {}) if my_name else {}
    opp_deck = decks.get(opp_name, {}) if opp_name else {}
    if me == "p1":
        extractor.set_deck_context(my_deck, opp_deck)
    else:
        extractor.set_deck_context(opp_deck, my_deck)


def sample_hidden(obs, rng, decks, our_deck, opp_deck):
    """One hidden-zone hypothesis for Game.from_observation, or None
    when the deck hypothesis cannot cover the observation."""
    me = obs["seat"]
    opp = "p2" if me == "p1" else "p1"
    mi, oi = (0, 1) if me == "p1" else (1, 0)
    my_pool = unseen_pool(decks, our_deck, seen_defs(obs, me, mi, True))
    opp_pool = unseen_pool(decks, opp_deck,
                           seen_defs(obs, opp, oi, False))
    rng.shuffle(my_pool)
    rng.shuffle(opp_pool)
    need_opp = obs.get("opponentHandSize", 0) + obs["librarySizes"][oi]
    if len(my_pool) < obs["librarySizes"][mi] or len(opp_pool) < need_opp:
        return None
    opp_hand = opp_pool[:obs.get("opponentHandSize", 0)]
    opp_lib = opp_pool[len(opp_hand):len(opp_hand)
                       + obs["librarySizes"][oi]]
    return {
        "hands": {opp: opp_hand},
        "libraries": {me: my_pool[:obs["librarySizes"][mi]],
                      opp: opp_lib},
        "outsideGame": {"p1": [], "p2": []},
    }


def inert_hidden(obs, decks, our_deck, opp_deck, card_kind):
    """DETERMINISTIC 'inert' hypothesis: fill the opponent's hidden HAND
    with the most benign unseen cards (lands first) so the rollout
    opponent cannot spring threats from hand -- the old C++ bot's
    blank/inert-hidden-card model, approximated with real benign cards.
    One world, no RNG (K collapses to 1)."""
    me = obs["seat"]
    opp = "p2" if me == "p1" else "p1"
    mi, oi = (0, 1) if me == "p1" else (1, 0)
    my_pool = unseen_pool(decks, our_deck, seen_defs(obs, me, mi, True))
    opp_pool = unseen_pool(decks, opp_deck, seen_defs(obs, opp, oi, False))
    need_opp = obs.get("opponentHandSize", 0) + obs["librarySizes"][oi]
    if len(my_pool) < obs["librarySizes"][mi] or len(opp_pool) < need_opp:
        return None
    # benign first: lands, then the rest; stable by definition id.
    opp_pool.sort(key=lambda d: (0 if card_kind.get(d) == "Land" else 1, d))
    my_pool.sort()
    oh = obs.get("opponentHandSize", 0)
    opp_hand = opp_pool[:oh]
    opp_lib = opp_pool[oh:oh + obs["librarySizes"][oi]]
    return {
        "hands": {opp: opp_hand},
        "libraries": {me: my_pool[:obs["librarySizes"][mi]],
                      opp: opp_lib},
        "outsideGame": {"p1": [], "p2": []},
    }


class DeterminizedPolicy:
    """Full search on hypothesis worlds via Game.from_observation
    (lacker/penta#57): at each observation, sample K hidden-zone
    hypotheses (opponent hand/library from their deck's unseen pool,
    our library from our own known list minus seen zones), reconstruct
    each into a live local game, run the CERTIFIED in-process search
    (trainer.choose: value net + policy head blend, dominance prunes,
    rollout playouts at DEFAULT_SEARCH) in every world, and act on the
    majority vote (ties broken by the shaped observation-only blend).

    Fail-closed everywhere: a world whose reconstruction or search
    fails is dropped (the observation JSON is logged -- reconstruction
    failures on ordinary observations are exactly the reopen evidence
    #57 asks for); when no world survives, the shaped observation-only
    policy answers, so the move clock is never at risk. A per-decision
    time budget stops sampling worlds early and votes among those
    finished.

    After local randomness fires (shuffles, random discards) or a
    hypothesized card is revealed, each world is a sample rather than a
    replica -- the documented, intended semantics of determinized
    search.
    """

    def __init__(self, engine, our_deck, k_worlds=6, weight=0.15,
                 value_path="penta_net.npz",
                 head_path="policy_head.ckpt-dagger1.npz",
                 decklists_path="builtin-decklists.json",
                 time_budget=20.0, fail_log=None, inert=False):
        self.inert = inert  # single INERT world (opp hidden cards benign)
        import os as _os
        here = _os.path.dirname(_os.path.abspath(__file__))
        # trainer's blend loads from env at import; pin it before.
        _os.environ.setdefault("PENTA_POLICY_NET",
                               _os.path.join(here, head_path))
        _os.environ.setdefault("PENTA_POLICY_WEIGHT", str(weight))
        import trainer as _trainer  # noqa: PLC0415
        self.trainer = _trainer
        self.engine = engine
        self.our_deck = our_deck
        self.k = k_worlds
        self.time_budget = time_budget
        self.fail_log = fail_log
        self.fallback = HostedPolicy(value_path=value_path,
                                     head_path=head_path, weight=weight)
        self.net = self.fallback.net
        self.extractor = self.fallback.extractor
        self.decks = load_decklists(
            _os.path.join(here, decklists_path))
        self.worlds_used = []  # per-decision survivor counts (telemetry)

    # -- hypothesis construction ----------------------------------------

    def _seen_defs(self, obs, seat_name, seat_idx, include_hand):
        return seen_defs(obs, seat_name, seat_idx, include_hand)

    def _pool(self, deck_name, seen):
        return unseen_pool(self.decks, deck_name, seen)

    def classify_opponent(self, obs):
        """Best-overlap guess at the opponent's (built-in) deck from
        every card of theirs we have seen."""
        opp = "p2" if obs.get("seat") == "p1" else "p1"
        oi = 0 if opp == "p1" else 1
        seen = self._seen_defs(obs, opp, oi, include_hand=False)
        if not seen:
            return None
        best, best_score = None, -1.0
        for name, deck in self.decks.items():
            pool = dict(deck)
            hits = 0
            for d in seen:
                if pool.get(d, 0) > 0:
                    pool[d] -= 1
                    hits += 1
            score = hits / len(seen)
            if score > best_score:
                best, best_score = name, score
        return best

    def build_hidden(self, obs, rng, opp_deck):
        if self.inert:
            return inert_hidden(obs, self.decks, self.our_deck, opp_deck,
                                self.extractor.card_kind)
        return sample_hidden(obs, rng, self.decks, self.our_deck,
                             opp_deck)

    # -- choice ----------------------------------------------------------

    def choose(self, obs, raw_json=None):
        import time as _time
        actions = obs.get("legalActions") or ()
        if not actions:
            return 0
        if len(actions) == 1:
            return actions[0]["index"]
        if "checkpoint" not in obs:
            # Server predates reconstruction.checkpoint.v2: shaped
            # observation-only play, no K futile exceptions per move.
            return self.fallback.choose(obs)
        raw = raw_json if raw_json is not None else json.dumps(obs)
        opp_deck = self.classify_opponent(obs) or self.our_deck
        # Belief schema: fix the two seats' decklists for the certified
        # in-world search (trainer.choose reads them via self.extractor).
        # We KNOW our deck; the opponent's is the best-overlap guess.
        self.fallback._our_deck = self.our_deck
        if getattr(self.extractor, "belief", False):
            me = obs.get("seat", "p1")
            my_deck = self.decks.get(self.our_deck, {})
            their_deck = self.decks.get(opp_deck, {})
            if me == "p1":
                self.extractor.set_deck_context(my_deck, their_deck)
            else:
                self.extractor.set_deck_context(their_deck, my_deck)
        votes = {}
        t0 = _time.time()
        survivors = 0
        for k in range(1 if self.inert else self.k):
            if _time.time() - t0 > self.time_budget and votes:
                break
            rng = np.random.default_rng()  # sampling entropy per world
            py_rng = __import__("random").Random(int(rng.integers(2**31)))
            hidden = self.build_hidden(obs, py_rng, opp_deck)
            if hidden is None:
                continue
            try:
                world = self.engine.Game.from_observation(
                    raw, json.dumps(hidden), int(rng.integers(2**31)))
            except Exception as error:
                self._log_failure(obs, raw, error)
                continue
            try:
                index, _, _ = self.trainer.choose(
                    world.clone, obs, self.net, self.extractor, py_rng,
                    epsilon=0.0, max_eval=16, depth=0,
                    search=self.trainer.DEFAULT_SEARCH)
            except Exception:
                continue  # panicked/stuck world: drop it
            votes[index] = votes.get(index, 0) + 1
            survivors += 1
        self.worlds_used.append(survivors)
        if not votes:
            return self.fallback.choose(obs)
        top = max(votes.values())
        tied = [i for i, v in votes.items() if v == top]
        if len(tied) == 1:
            return tied[0]
        # Tie: shaped observation-only blend decides among the tied.
        tied_actions = [a for a in actions if a["index"] in tied]
        return self.fallback._pick(obs, tied_actions)

    def _log_failure(self, obs, raw, error):
        if not self.fail_log:
            return
        try:
            with open(self.fail_log, "a") as f:
                f.write(json.dumps({"error": str(error)[:300],
                                    "observation": obs}) + "\n")
        except OSError:
            pass


def _export_spzw(net, path):
    """Write a Net to the flat LE binary spz_core reads (u64 hidden, u64
    inputs, then w1/b1/w2/b2 f64) -- same layout as trainer._export_spzw."""
    hidden, inputs = net.w1.shape
    with open(path, "wb") as f:
        f.write(np.array([hidden, inputs], dtype="<u8").tobytes())
        f.write(np.ascontiguousarray(net.w1, dtype="<f8").tobytes())
        f.write(np.ascontiguousarray(net.b1, dtype="<f8").tobytes())
        f.write(np.ascontiguousarray(net.w2, dtype="<f8").tobytes())
        f.write(np.array([net.b2], dtype="<f8").tobytes())


class NativeIsmctsPolicy:
    """Hosted play driven by the native SO-ISMCTS search
    (spz_core.ismcts_choose): pools statistics across determinizations in
    one shared tree, versus the DeterminizedPolicy K-worlds-and-VOTE
    baseline. Holds the SAME certified pair (value net + policy head @
    weight) as the det path -- this isolates the SEARCH change.

    Fail-closed exactly like DeterminizedPolicy: pre-checkpoint servers and
    any native error fall back to the shaped observation-only HostedPolicy.
    """

    def __init__(self, our_deck, iters=150, c_puct=1.5, budget=400,
                 inert=False, weight=0.15, value_path="penta_net.npz",
                 head_path="policy_head.ckpt-dagger1.npz",
                 decklists_path="builtin-decklists.json",
                 opponent="handcrafted", leaf_playout=False):
        import spz_core  # noqa: PLC0415
        from extractor import pinned_catalog  # noqa: PLC0415
        here = os.path.dirname(os.path.abspath(__file__))
        self._spz = spz_core
        self.our_deck = our_deck
        self.iters = iters
        self.c_puct = c_puct
        self.budget = budget
        self.inert = inert
        self.opponent = opponent
        self.leaf_playout = leaf_playout
        self.weight = weight
        self.decklists_path = (decklists_path if os.path.isabs(decklists_path)
                               else os.path.join(here, decklists_path))
        self.catalog_json = json.dumps(pinned_catalog())
        self.decks = load_decklists(self.decklists_path)
        # Shaped observation-only fallback (same nets, npz).
        self.fallback = HostedPolicy(value_path=value_path,
                                     head_path=head_path, weight=weight)
        # Native reads flat LE weights; export the npz pair once per worker.
        # Per-PID paths in a temp dir so parallel gate workers never race on
        # the same file (concurrent truncating writes corrupt the load).
        import tempfile  # noqa: PLC0415
        tag = f"{os.getpid()}"
        tmp = tempfile.gettempdir()
        base_v = os.path.splitext(os.path.basename(value_path))[0]
        base_h = os.path.splitext(os.path.basename(head_path))[0]
        self.value_spzw = os.path.join(tmp, f"{base_v}.{tag}.spzw")
        self.head_spzw = os.path.join(tmp, f"{base_h}.{tag}.spzw")
        _export_spzw(self.fallback.net, self.value_spzw)
        _export_spzw(self.fallback.head, self.head_spzw)

    def choose(self, obs, raw_json=None):
        actions = obs.get("legalActions") or ()
        if not actions:
            return 0
        if len(actions) == 1:
            return actions[0]["index"]
        if "checkpoint" not in obs:
            # Server predates reconstruction.checkpoint: no world to build.
            self.fallback._our_deck = self.our_deck
            return self.fallback.choose(obs)
        # Mulligan: Keep/Mulligan afterstates are featurally identical, so
        # the search has no signal and always mulligans forever. Decide by
        # the land-count rule (keep 2-5 lands), as the other policies do.
        by_type = {}
        for a in actions:
            by_type.setdefault(a.get("type"), []).append(a)
        if "KeepHand" in by_type:
            ck = self.fallback.extractor.card_kind
            lands = sum(1 for c in obs.get("hand", ())
                        if ck.get(c["definition"]) == "Land")
            if 2 <= lands <= 5 or "TakeMulligan" not in by_type:
                return by_type["KeepHand"][0]["index"]
            return by_type["TakeMulligan"][0]["index"]
        raw = raw_json if raw_json is not None else json.dumps(obs)
        opp_deck = classify_opponent(self.decks, obs) or self.our_deck
        try:
            return self._spz.ismcts_choose(
                self.catalog_json, self.value_spzw, self.head_spzw,
                self.weight, int(self.iters), float(self.c_puct),
                int(self.budget), bool(self.inert), self.decklists_path,
                raw, self.our_deck, opp_deck, self.opponent,
                bool(self.leaf_playout))
        except Exception:
            self.fallback._our_deck = self.our_deck
            return self.fallback.choose(obs)


class AacPolicy:
    """Play the trained AAC actor on the hosted server.

    The actor scores AFTERSTATES: for each legal action it wants the
    redacted observation that would result. The hosted seat has no engine
    state to fork, so it uses HostedPolicy.predict_afterstate -- a
    lightweight predicted observation touching only the fields the feature
    schema reads.

    CAVEAT, and it is the one to watch: the actor was TRAINED on true
    engine afterstates (fork, apply, observe). Here it sees predicted ones.
    Where the prediction is incomplete the afterstate is indistinguishable
    from doing nothing, and the tie ordering decides instead. So hosted
    strength is a lower bound on the local evaluation number, not the same
    number -- confirm it by measuring, never assume they match.

    Deck classification matches training: our own deck is known, the
    opponent's is inferred from revealed cards (the server does not
    disclose it).
    """

    def __init__(self, actor_path, hidden=256, our_deck="Sligh",
                 decklists_path="builtin-decklists.json"):
        import numpy as _np
        here = os.path.dirname(os.path.abspath(__file__))
        path = (actor_path if os.path.isabs(actor_path)
                else os.path.join(here, actor_path))
        d = _np.load(path)
        self.w1 = _np.asarray(d["f1.weight"], dtype=_np.float64)
        self.b1 = _np.asarray(d["f1.bias"], dtype=_np.float64)
        self.w2 = _np.asarray(d["f2.weight"], dtype=_np.float64).reshape(-1)
        self.b2 = float(_np.asarray(d["f2.bias"]).reshape(-1)[0])
        self.extractor = Extractor.for_inputs(self.w1.shape[1])
        self.our_deck = our_deck
        self.decks = load_decklists(decklists_path)
        self.fallback = HostedPolicy(our_deck=our_deck) \
            if os.path.exists(os.path.join(here, "penta_net.npz")) else None

    def _score(self, feats):
        import numpy as _np
        h = _np.tanh(_np.asarray(feats, dtype=_np.float64) @ self.w1.T
                     + self.b1)
        return h @ self.w2 + self.b2

    def choose(self, obs, raw_json=None):
        import numpy as _np
        actions = obs.get("legalActions") or ()
        if not actions:
            return 0
        if len(actions) == 1:
            return actions[0]["index"]
        try:
            if getattr(self.extractor, "belief", False):
                belief_deck_context(self.extractor, self.decks, obs,
                                    self.our_deck)
            rows = []
            for a in actions:
                after = HostedPolicy.predict_afterstate(self, obs, a)
                rows.append(self.extractor.features(after))
            scores = self._score(_np.stack(rows))
            return actions[int(_np.argmax(scores))]["index"]
        except Exception:
            # never forfeit on a scoring bug: fall back to the tie ordering
            return min(actions,
                       key=lambda a: (_TIE_ORDER.get(a.get("type"), 7),
                                      a["index"]))["index"]
