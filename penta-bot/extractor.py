"""Observation JSON -> numpy float32 feature vector for penta.

Feature philosophy mirrors our v1/colors SPZ schema: per-card-DEFINITION
counts for the public zones, plus a small block of global scalars. Every
feature is deterministic, bounded (roughly [0, ~4] after scaling), and
derived only from one seat's redacted observation -- no hidden information.

Layout (C = number of card definitions in penta.catalog(), fetched once):

    [0*C : 1*C)  own hand, count per definition
    [1*C : 2*C)  own battlefield, count per definition
    [2*C : 3*C)  opponent battlefield, count per definition
    [3*C : 4*C)  own graveyard, count per definition
    [4*C : 5*C)  opponent graveyard, count per definition
    [5*C : ...)  global scalars, in the order built by _scalars() below

Own-library-remaining counts are skipped: the protocol does not expose the
decklists of the built-in decks, so the deduction "decklist minus seen
zones" has no ground truth to start from.

Counts are scaled by 1/4 (a playset); scalars carry their own divisors,
documented inline. The vector length is `Extractor.size`.
"""

import json
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


class Extractor:
    """Turns one observation dict into a fixed-length float32 vector."""

    def __init__(self):
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
        self.n_scalars = len(self._scalars_of_empty())
        self.size = 5 * self.defs + self.n_scalars

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

        vec[5 * C:] = self._scalars(obs, me, opp)
        return vec
