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
                 shaped=True):
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

        SHAPED mode (default; the honest hosted fallback): without
        playouts the raw 1-ply blend reverts to the passivity every
        pre-search era collapsed into (measured: 2 casts from 157 cast
        offers, zero attacks, 0/12 vs handcrafted). The cure that
        worked then works here: a first_bot-shaped category order --
        land > cast > attack > the rest -- with the NETS selecting
        WITHIN each category (which land, which cast/target/X, which
        blocker). first_bot's bare ordering alone beats handcrafted
        ~73%; the nets refine its choices. shaped=False keeps the pure
        blend for comparison gates."""
        actions = obs.get("legalActions") or ()
        if not actions:
            return 0
        if len(actions) == 1:
            return actions[0]["index"]
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
