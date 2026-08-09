"""Observation JSON -> numpy float32 feature vector for penta.

Feature philosophy mirrors our v1/colors SPZ schema: per-card-DEFINITION
counts for the public zones, plus a small block of global scalars. Every
feature is deterministic, bounded (roughly [0, ~4] after scaling), and
derived only from one seat's redacted observation -- no hidden information.

The schema is VERSIONED; saved nets carry their input size, so
`Extractor.for_inputs(net.inputs)` dispatches old nets onto the layout
they were trained on.

Schema v1 layout (C = number of card definitions legal in
penta.catalog()'s own format, fetched once):

    [0*C : 1*C)  own hand, count per definition
    [1*C : 2*C)  own battlefield, count per definition
    [2*C : 3*C)  opponent battlefield, count per definition
    [3*C : 4*C)  own graveyard, count per definition
    [4*C : 5*C)  opponent graveyard, count per definition
    [5*C : ...)  global scalars, in the order built by _scalars() below

Schema v2 (default) appends a castability + race-math block after the v1
scalars:

    [v1 : v1+C)  own hand, CASTABLE-NOW count per definition -- exact,
                 from the observation's enumerated CastSpell legalActions
                 (distinct hand instances; zero when this seat holds no
                 decision, e.g. most afterstates where priority passed)
    then 7       hand count per converted-cost bucket (0,1,2,3,4,5,6+)
    then 7       castable-now count per converted-cost bucket
    then 8       extra scalars, in the order built by _extra_scalars():
                 castable-spell count, max castable cost, life delta,
                 own/opponent flying power, turns-to-kill-opponent and
                 turns-to-be-killed at current total power (clipped),
                 declare-attacker options available right now

Own-library-remaining counts are skipped: the protocol does not expose the
decklists of the built-in decks, so the deduction "decklist minus seen
zones" has no ground truth to start from.

Counts are scaled by 1/4 (a playset); scalars carry their own divisors,
documented inline. The vector length is `Extractor.size` (675 for v1,
825 for v2 on the 128-definition Old School catalog).
"""

import json
import math
import os
import sys

import numpy as np

# The pinned engine binding: the penta.so copied into this directory is
# preferred, so training is immune to upstream rebuilds of the clone.
# Fallback is the build location in the penta clone; PENTA_PY_DIR overrides
# that fallback.
LOCAL_DIR = os.path.dirname(os.path.abspath(__file__))
PENTA_PY_DIR = os.environ.get(
    "PENTA_PY_DIR", os.path.expanduser("~/proj/penta/bindings/penta-py")
)


def import_penta():
    """Import the penta module, pinned copy first (idempotent)."""
    for path in (LOCAL_DIR, PENTA_PY_DIR):
        if os.path.exists(os.path.join(path, "penta.so")):
            if path not in sys.path:
                sys.path.insert(0, path)
            import penta  # noqa: PLC0415

            return penta
    raise ImportError(
        f"no penta.so in {LOCAL_DIR} or {PENTA_PY_DIR}; build the binding "
        "(cargo build --release in bindings/penta-py) and copy it here")


STEPS = (
    "Upkeep",
    "Draw",
    "PrecombatMain",
    "BeginningOfCombat",
    "DeclareAttackers",
    "DeclareBlockers",
    "CombatDamage",
    "EndOfCombat",
    "PostcombatMain",
    "End",
    "Cleanup",
)
STEP_INDEX = {name: i for i, name in enumerate(STEPS)}

_CREATURE_KINDS = frozenset({"Creature", "ArtifactCreature"})


N_COST_BUCKETS = 7  # converted cost 0..5, then 6+
N_EXTRA_SCALARS = 8  # keep in sync with _extra_scalars()


class Extractor:
    """Turns one observation dict into a fixed-length float32 vector."""

    def __init__(self, version=2):
        if version not in (1, 2):
            raise ValueError(f"unknown feature schema version {version}")
        self.version = version
        penta = import_penta()
        catalog = json.loads(penta.catalog())
        self.engine_version = catalog["engineVersion"]
        self.protocol_version = catalog["protocolVersion"]
        # Deterministic definition -> slot mapping, sorted by definition id.
        # Protocol v2 catalogs list every format's cards with per-format
        # legality flags; only cards legal in the catalog's own format
        # (default: Old School 93/94) get feature slots. Protocol 0/1
        # catalogs carry no flag and keep every card, as before.
        defs = sorted(card["definition"] for card in catalog["cards"]
                      if card.get("legal", True))
        self.def_slot = {d: i for i, d in enumerate(defs)}
        self.defs = len(defs)
        self.card_kind = {
            card["definition"]: card["kind"] for card in catalog["cards"]
        }
        # Printed power per definition (0 for non-creatures); used by the
        # trainer's first_bot-shaped exploration prior, not by features.
        self.card_power = {
            card["definition"]: card.get("power") or 0
            for card in catalog["cards"]
        }
        # Converted mana cost per definition (generic + colored pips +
        # hybrid; X counts 0). Protocol 0/1 catalogs lack manaCost -> 0.
        self.card_cost = {}
        # Definitions whose printed rules text LEADS with the Flying
        # keyword ("Flying." / "Flying, vigilance." ...); conditional
        # granters ("R: Gains flying...") are correctly excluded.
        self.flying_defs = set()
        for card in catalog["cards"]:
            cost = card.get("manaCost") or {}
            self.card_cost[card["definition"]] = (
                cost.get("generic", 0) + cost.get("white", 0)
                + cost.get("blue", 0) + cost.get("black", 0)
                + cost.get("red", 0) + cost.get("green", 0)
                + cost.get("whiteRedHybrid", 0))
            if (card.get("power") is not None
                    and (card.get("rulesText") or "").startswith("Flying")):
                self.flying_defs.add(card["definition"])
        self.n_scalars = len(self._scalars_of_empty())
        self.v1_size = 5 * self.defs + self.n_scalars
        self.size = self.v1_size
        if version >= 2:
            self.size += self.defs + 2 * N_COST_BUCKETS + N_EXTRA_SCALARS

    @classmethod
    def for_inputs(cls, inputs):
        """Extractor whose schema matches a saved net's input size."""
        for version in (2, 1):
            ex = cls(version=version)
            if ex.size == inputs:
                return ex
        raise ValueError(f"no feature schema has {inputs} inputs "
                         f"(v1={ex.v1_size})")

    # -- scalars ---------------------------------------------------------

    def _scalars_of_empty(self):
        """Dry run to fix the scalar count; keep in sync with _scalars."""
        empty = {
            "turn": 1, "step": "Upkeep", "pregame": True,
            "seat": "p1", "activeSeat": "p1",
            "life": [20, 20], "librarySizes": [53, 53],
            "hand": [], "opponentHandSize": 7,
            "manaPools": [dict.fromkeys(
                ("white", "blue", "black", "red", "green", "colorless"), 0
            )] * 2,
            "battlefield": [], "stack": [],
        }
        return self._scalars(empty, 0, 1)

    def _scalars(self, obs, me, opp):
        """Global scalar block. Order is the schema; do not reorder."""
        life = obs["life"]
        libs = obs["librarySizes"]
        pools = obs["manaPools"]
        my_pool = sum(pools[me].values())
        opp_pool = sum(pools[opp].values())

        # Battlefield aggregates per side.
        agg = [
            # creatures, power, toughness, untapped creatures,
            # untapped lands, attackers
            [0.0] * 6,
            [0.0] * 6,
        ]
        seat_names = ("p1", "p2")
        for perm in obs["battlefield"]:
            side = me if perm["controller"] == seat_names[me] else opp
            a = agg[0] if side == me else agg[1]
            kind = self.card_kind.get(perm["definition"], "")
            is_creature = perm["power"] is not None or kind in _CREATURE_KINDS
            if is_creature:
                a[0] += 1.0
                a[1] += float(perm["power"] or 0)
                a[2] += float(perm["toughness"] or 0)
                if not perm["tapped"]:
                    a[3] += 1.0
                if perm["attacking"]:
                    a[5] += 1.0
            elif kind == "Land" and not perm["tapped"]:
                a[4] += 1.0

        step_onehot = [0.0] * len(STEPS)
        idx = STEP_INDEX.get(obs.get("step"))
        if idx is not None:
            step_onehot[idx] = 1.0

        scalars = [
            min(obs.get("turn", 0), 40) / 20.0,          # game turn
            1.0 if obs.get("pregame") else 0.0,           # mulligan phase
            1.0 if obs.get("activeSeat") == obs["seat"] else 0.0,
            *step_onehot,                                 # 11 entries
            life[me] / 20.0,
            life[opp] / 20.0,
            len(obs.get("hand", ())) / 7.0,
            obs.get("opponentHandSize", 0) / 7.0,
            libs[me] / 60.0,
            libs[opp] / 60.0,
            min(my_pool, 10) / 5.0,
            min(opp_pool, 10) / 5.0,
            len(obs.get("stack", ())) / 4.0,
            agg[0][0] / 8.0,   # own creature count
            agg[0][1] / 16.0,  # own total power
            agg[0][2] / 16.0,  # own total toughness
            agg[0][3] / 8.0,   # own untapped creatures
            agg[0][4] / 8.0,   # own untapped lands
            agg[0][5] / 8.0,   # own attackers pending
            agg[1][0] / 8.0,   # opponent creature count
            agg[1][1] / 16.0,
            agg[1][2] / 16.0,
            agg[1][3] / 8.0,
            agg[1][4] / 8.0,
            agg[1][5] / 8.0,
        ]
        return scalars

    # -- v2 castability + race-math block --------------------------------

    def _cost_bucket(self, definition):
        return min(self.card_cost.get(definition, 0), N_COST_BUCKETS - 1)

    def _extra_block(self, obs, me, opp, vec, base):
        """Fill the v2 block in-place starting at `base`; see module doc.

        Castability is EXACT and free: the observation enumerates every
        CastSpell legal right now (only when this seat holds the
        decision; otherwise the castable slots stay zero, which is
        itself informative -- "no spell window here").
        """
        C = self.defs
        hand = obs.get("hand", ())
        instance_def = {}
        for card in hand:
            vec[base + C + self._cost_bucket(card["definition"])] += 0.25
            inst = card.get("instance", card.get("objectId"))
            instance_def[inst] = card["definition"]

        castable_defs = []
        attack_options = 0
        seen_instances = set()
        for action in obs.get("legalActions", ()):
            kind = action["type"]
            if kind == "DeclareAttacker":
                attack_options += 1
                continue
            if kind != "CastSpell":
                continue
            inst = action.get("card")
            definition = instance_def.get(inst)
            # Count each castable hand instance once (a card can carry
            # several play options); non-hand casts (none in this pool)
            # are skipped.
            if definition is None or inst in seen_instances:
                continue
            seen_instances.add(inst)
            castable_defs.append(definition)
            vec[base + self.def_slot[definition]] += 0.25
            vec[base + C + N_COST_BUCKETS
                + self._cost_bucket(definition)] += 0.25

        # Battlefield race math (perm power reflects current stats).
        life = obs["life"]
        seat_names = ("p1", "p2")
        power = [0.0, 0.0]   # me, opp total power
        flying = [0.0, 0.0]  # me, opp flying power
        for perm in obs.get("battlefield", ()):
            side = 0 if perm["controller"] == seat_names[me] else 1
            p = perm["power"]
            if p is None:
                continue
            power[side] += float(p)
            if perm["definition"] in self.flying_defs:
                flying[side] += float(p)

        def turns_to_kill(attacker_power, defender_life):
            if attacker_power <= 0.0:
                return 10.0
            return min(10.0, math.ceil(max(defender_life, 0) / attacker_power))

        max_cost = max((self.card_cost.get(d, 0) for d in castable_defs),
                       default=0)
        s = base + C + 2 * N_COST_BUCKETS
        vec[s:s + N_EXTRA_SCALARS] = (
            len(castable_defs) / 4.0,            # castable spells right now
            min(max_cost, 6) / 6.0,              # max castable cost
            (life[me] - life[opp]) / 20.0,       # life delta
            flying[0] / 8.0,                     # own flying power
            flying[1] / 8.0,                     # opponent flying power
            turns_to_kill(power[0], life[opp]) / 10.0,  # turns to kill opp
            turns_to_kill(power[1], life[me]) / 10.0,   # turns to be killed
            attack_options / 8.0,                # attackers available now
        )

    # -- full vector -----------------------------------------------------

    def features(self, obs):
        """obs: parsed observation dict for the seat we evaluate for."""
        me = 0 if obs["seat"] == "p1" else 1
        opp = 1 - me
        seat_names = ("p1", "p2")
        C = self.defs
        vec = np.zeros(self.size, dtype=np.float32)

        for card in obs.get("hand", ()):
            vec[self.def_slot[card["definition"]]] += 0.25

        for perm in obs.get("battlefield", ()):
            base = C if perm["controller"] == seat_names[me] else 2 * C
            vec[base + self.def_slot[perm["definition"]]] += 0.25

        graveyards = obs.get("graveyards", ((), ()))
        for card in graveyards[me]:
            vec[3 * C + self.def_slot[card["definition"]]] += 0.25
        for card in graveyards[opp]:
            vec[4 * C + self.def_slot[card["definition"]]] += 0.25

        vec[5 * C:self.v1_size] = self._scalars(obs, me, opp)
        if self.version >= 2:
            self._extra_block(obs, me, opp, vec, self.v1_size)
        return vec
