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

Schema v3 (default) appends a DEPLOYMENT block after v2 -- the fix-1
post-mortem's hoarding pathology (win conditions and card-advantage
engines dying in hand). All property-derived, no card lists:
"win condition" = printed creature power, "draw engine" = an ACTIVATED
draw ability (rules text with a ':' activation before "draw" --
Jayemdae Tome, Library of Alexandria, Sage of Lat-Nam in this catalog,
future cards automatically):

    15 scalars, in the order built by _v3_scalars():
                 own-hand creature power / count / max power,
                 own-hand draw-engine count, castable creature power,
                 castable draw-engine count, own/opponent battlefield
                 draw-engine counts, active-engine signal (own engines x
                 untapped lands), own/opp battlefield artifact counts,
                 enchantment counts, total land counts

Schemas are strictly NESTED (v3 = v2 + block, v2 = v1 + block), so an
older net's weight matrix can be grown into a newer schema by
zero-padding the new input columns (the trainer's --grow-init).

Own-library-remaining counts are skipped: the protocol does not expose the
decklists of the built-in decks, so the deduction "decklist minus seen
zones" has no ground truth to start from.

Counts are scaled by 1/4 (a playset); scalars carry their own divisors,
documented inline. The vector length is `Extractor.size` (675 for v1,
825 for v2, 840 for v3 on the 128-definition Old School catalog).
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
    """Import the penta module, pinned copy first (idempotent).

    PENTA_ENGINE_DIR selects an era's engine explicitly (e.g.
    engine-0.7.0/ for the determinized era); without it the original
    0.5.0 penta.so in this directory keeps winning, so every closed-era
    artifact behaves exactly as it always did."""
    override = os.environ.get("PENTA_ENGINE_DIR")
    candidates = ((override,) if override else ()) + (LOCAL_DIR,
                                                      PENTA_PY_DIR)
    for path in candidates:
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
N_V3_SCALARS = 15  # keep in sync with _v3_scalars()


def pinned_catalog():
    """The catalog that defines the FEATURE LAYOUT, independent of any
    importable engine. Prefers the pinned-catalog.json snapshot (written
    from the vendored protocol-2 penta.so; definition IDs are
    append-only upstream, so the 128-slot layout stays valid against
    newer engines), else falls back to importing the pinned module.
    Hosted bots featurize protocol-18 observations against this exact
    layout without importing any engine at all."""
    snapshot = os.path.join(LOCAL_DIR, "pinned-catalog.json")
    if os.path.exists(snapshot):
        with open(snapshot) as f:
            return json.load(f)
    return json.loads(import_penta().catalog())


def converted_cost(cost):
    """Total printed cost from a mana-cost object, protocol-tolerant:
    protocol <=7 used a numeric whiteRedHybrid field, protocol 8+ a
    sparse `hybrid` array; X counts 0; null cost -> 0."""
    cost = cost or {}
    hybrid = sum(h.get("count", 0) for h in (cost.get("hybrid") or ()))
    return (cost.get("generic", 0) + cost.get("white", 0)
            + cost.get("blue", 0) + cost.get("black", 0)
            + cost.get("red", 0) + cost.get("green", 0)
            + cost.get("whiteRedHybrid", 0) + hybrid)


class Extractor:
    """Turns one observation dict into a fixed-length float32 vector."""

    def __init__(self, version=3, belief=False):
        if version not in (1, 2, 3):
            raise ValueError(f"unknown feature schema version {version}")
        if belief and version != 2:
            raise ValueError("the hidden-pool belief block is defined on the "
                             "v2 (825) base only; use Extractor(version=2, "
                             "belief=True)")
        self.version = version
        self.belief = belief
        catalog = pinned_catalog()
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
            self.card_cost[card["definition"]] = converted_cost(
                card.get("manaCost"))
            if (card.get("power") is not None
                    and (card.get("rulesText") or "").startswith("Flying")):
                self.flying_defs.add(card["definition"])
        # Definitions whose rules text signals effects INVISIBLE to an
        # observation snapshot (extra turns, until-end-of-turn grants,
        # damage prevention/protection, regeneration shields). The
        # dominance prune fails closed on these: a play that changes
        # nothing visible may still carry this upside. Text-derived, so
        # future cards with these effects are covered automatically.
        markers = ("extra turn", "until end of turn", "prevent",
                   "protection from", "regenerate")
        self.invisible_upside_defs = {
            card["definition"] for card in catalog["cards"]
            if any(marker in (card.get("rulesText") or "").lower()
                   for marker in markers)
        }
        # Definitions with an ACTIVATED draw ability (a ':' activation
        # before "draw" in the rules text): repeatable card-advantage
        # engines, not one-shot draw spells. Property-derived, so future
        # cards are covered automatically.
        self.draw_engine_defs = set()
        for card in catalog["cards"]:
            text = card.get("rulesText") or ""
            low = text.lower()
            if ":" in text and "draw" in low \
                    and text.index(":") < low.index("draw"):
                self.draw_engine_defs.add(card["definition"])
        self.n_scalars = len(self._scalars_of_empty())
        self.v1_size = 5 * self.defs + self.n_scalars
        self.size = self.v1_size
        if version >= 2:
            self.size += self.defs + 2 * N_COST_BUCKETS + N_EXTRA_SCALARS
        self.v2_size = self.size if version >= 2 else None
        if version >= 3:
            self.size += N_V3_SCALARS
        # The hidden-pool belief block (a per-card-type count vector, for
        # [me, opp], of each player's decklist minus every card of theirs
        # visible anywhere) is appended LAST, after the v2 castability
        # block, keeping the first 825 features byte-identical. New schema
        # size = 825 + 2*defs (1081 on the 128-def catalog). Ported from
        # the old honest C++ bot (subtract_public_zones / subtract_owned_
        # auras / append_counts in selfplay_zero.cpp).
        self.belief_base = self.size
        if belief:
            self.size += 2 * self.defs
        # Optional per-seat deck context (see set_deck_context): count
        # arrays [p1_counts, p2_counts] the belief block subtracts the seen
        # zones from. Deck GUESSING never lives here -- the caller supplies
        # already-decided count arrays; the extractor stays pure.
        self._deck_by_seat = None

    @classmethod
    def for_inputs(cls, inputs):
        """Extractor whose schema matches a saved net's input size."""
        # Non-belief schemas first (675/825/840), then the belief schema
        # (v2 + 2*defs = 1081); the sizes never collide.
        for belief in (False, True):
            for version in ((2,) if belief else (3, 2, 1)):
                ex = cls(version=version, belief=belief)
                if ex.size == inputs:
                    return ex
        raise ValueError(f"no feature schema has {inputs} inputs "
                         f"(v1={cls().v1_size})")

    # -- deck context for the belief block -------------------------------

    def deck_slot_counts(self, deck):
        """A defs-length int32 count array in this extractor's slot
        mapping. Accepts a {definition_id: count} dict, an existing
        defs-length array, or None (-> zeros)."""
        arr = np.zeros(self.defs, dtype=np.int32)
        if deck is None:
            return arr
        if isinstance(deck, dict):
            for d, c in deck.items():
                i = self.def_slot.get(int(d))
                if i is not None:
                    arr[i] += int(c)
            return arr
        a = np.asarray(deck)
        if a.shape[0] != self.defs:
            raise ValueError(f"deck count array has {a.shape[0]} slots, "
                             f"expected {self.defs}")
        arr[:] = a.astype(np.int32)
        return arr

    def set_deck_context(self, p1_deck, p2_deck):
        """Fix the two seats' decklists (by SEAT, p1 then p2) for the
        belief block, so features(obs) can compute the hidden pools
        without threading deck args through every internal call. Each
        argument is a {definition: count} dict or a defs-length array."""
        self._deck_by_seat = [self.deck_slot_counts(p1_deck),
                              self.deck_slot_counts(p2_deck)]

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
            i = self.def_slot.get(definition)
            if i is not None:
                vec[base + i] += 0.25
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
        return castable_defs

    # -- v3 deployment block ---------------------------------------------

    def _v3_scalars(self, obs, me, opp, castable_defs):
        """Deployment scalars (fix-1 post-mortem: win conditions and
        card-advantage engines dying in hand). Order is the schema."""
        hand_defs = [card["definition"] for card in obs.get("hand", ())]
        hand_creatures = [d for d in hand_defs
                          if self.card_kind.get(d) in _CREATURE_KINDS]
        hand_power = float(sum(self.card_power.get(d, 0)
                               for d in hand_creatures))
        max_hand_power = float(max(
            (self.card_power.get(d, 0) for d in hand_creatures), default=0))
        castable_power = float(sum(
            self.card_power.get(d, 0) for d in castable_defs
            if self.card_kind.get(d) in _CREATURE_KINDS))

        seat_names = ("p1", "p2")
        # engines, artifacts, enchantments, lands per side
        agg = [[0.0] * 4, [0.0] * 4]
        untapped_lands = 0.0
        for perm in obs.get("battlefield", ()):
            side = 0 if perm["controller"] == seat_names[me] else 1
            a = agg[side]
            if perm["definition"] in self.draw_engine_defs:
                a[0] += 1.0
            kind = self.card_kind.get(perm["definition"], "")
            if kind in ("Artifact", "ArtifactCreature"):
                a[1] += 1.0
            elif kind == "Enchantment":
                a[2] += 1.0
            elif kind == "Land":
                a[3] += 1.0
                if side == 0 and not perm["tapped"]:
                    untapped_lands += 1.0

        return [
            hand_power / 16.0,                     # own-hand creature power
            len(hand_creatures) / 4.0,             # own-hand creature count
            max_hand_power / 8.0,                  # biggest threat in hand
            sum(1 for d in hand_defs
                if d in self.draw_engine_defs) / 4.0,  # engines in hand
            castable_power / 16.0,                 # deployable power NOW
            sum(1 for d in castable_defs
                if d in self.draw_engine_defs) / 4.0,  # engines castable now
            agg[0][0] / 4.0,                       # own engines on battlefield
            agg[1][0] / 4.0,                       # opp engines on battlefield
            (min(agg[0][0], 2.0) / 2.0)
            * (min(untapped_lands, 4.0) / 4.0),    # active-engine signal
            agg[0][1] / 8.0,                       # own artifacts
            agg[1][1] / 8.0,                       # opp artifacts
            agg[0][2] / 4.0,                       # own enchantments
            agg[1][2] / 4.0,                       # opp enchantments
            agg[0][3] / 12.0,                      # own lands total
            agg[1][3] / 12.0,                      # opp lands total
        ]

    # -- full vector -----------------------------------------------------

    def features(self, obs, my_deck=None, opp_deck=None):
        """obs: parsed observation dict for the seat we evaluate for.

        my_deck / opp_deck (belief schema only): the deciding seat's and
        the opponent's decklists, each a {definition: count} dict or a
        defs-length count array. When omitted, the per-seat context set by
        set_deck_context is used; when neither is available the belief
        block stays zero (safe, uninformative)."""
        me = 0 if obs["seat"] == "p1" else 1
        opp = 1 - me
        seat_names = ("p1", "p2")
        C = self.defs
        vec = np.zeros(self.size, dtype=np.float32)

        # def_slot lookups are defensive: a newer engine's observation can
        # contain definitions outside the pinned layout (tokens, cards
        # added after the pin); they contribute to the scalar aggregates
        # but have no count slot.
        slot = self.def_slot.get
        for card in obs.get("hand", ()):
            i = slot(card["definition"])
            if i is not None:
                vec[i] += 0.25

        for perm in obs.get("battlefield", ()):
            base = C if perm["controller"] == seat_names[me] else 2 * C
            i = slot(perm["definition"])
            if i is not None:
                vec[base + i] += 0.25

        graveyards = obs.get("graveyards", ((), ()))
        for card in graveyards[me]:
            i = slot(card["definition"])
            if i is not None:
                vec[3 * C + i] += 0.25
        for card in graveyards[opp]:
            i = slot(card["definition"])
            if i is not None:
                vec[4 * C + i] += 0.25

        vec[5 * C:self.v1_size] = self._scalars(obs, me, opp)
        if self.version >= 2:
            castable_defs = self._extra_block(obs, me, opp, vec,
                                              self.v1_size)
        if self.version >= 3:
            vec[self.v2_size:] = self._v3_scalars(obs, me, opp,
                                                  castable_defs)
        if self.belief:
            self._belief_block(obs, me, opp, vec, my_deck, opp_deck)
        return vec

    # -- hidden-pool belief block ----------------------------------------

    def _belief_block(self, obs, me, opp, vec, my_deck, opp_deck):
        """Fill [belief_base : belief_base+2*C] in-place: for [me, opp],
        each player's decklist counts minus the counts of that player's
        cards visible ANYWHERE (subtract_public_zones + subtract_owned_
        auras from the old C++ bot), clamped >=0 and scaled by 1/4.

        "Seen" for player p = p's battlefield permanents + p's graveyard +
        p's exile + stack objects p controls, and -- only for the viewer
        (p == me) -- the viewer's hand. The opponent's hand is exactly
        what the belief block leaves the net to reason about, so it is not
        subtracted. Definitions outside the decklist floor at 0 (tokens,
        post-pin cards)."""
        C = self.defs
        slot = self.def_slot.get
        if my_deck is not None or opp_deck is not None:
            my_counts = self.deck_slot_counts(my_deck)
            opp_counts = self.deck_slot_counts(opp_deck)
        elif self._deck_by_seat is not None:
            my_counts = self._deck_by_seat[me]
            opp_counts = self._deck_by_seat[opp]
        else:
            return  # no deck context -> leave the block zero

        my_seat = ("p1", "p2")[me]
        my_seen = np.zeros(C, dtype=np.int32)
        opp_seen = np.zeros(C, dtype=np.int32)
        for card in obs.get("hand", ()):
            i = slot(card["definition"])
            if i is not None:
                my_seen[i] += 1
        for perm in obs.get("battlefield", ()):
            i = slot(perm["definition"])
            if i is None:
                continue
            if perm["controller"] == my_seat:
                my_seen[i] += 1
            else:
                opp_seen[i] += 1
        graveyards = obs.get("graveyards") or ((), ())
        for card in graveyards[me]:
            i = slot(card["definition"])
            if i is not None:
                my_seen[i] += 1
        for card in graveyards[opp]:
            i = slot(card["definition"])
            if i is not None:
                opp_seen[i] += 1
        exiles = obs.get("exiles") or ((), ())
        if len(exiles) > me:
            for card in exiles[me]:
                i = slot(card["definition"])
                if i is not None:
                    my_seen[i] += 1
        if len(exiles) > opp:
            for card in exiles[opp]:
                i = slot(card["definition"])
                if i is not None:
                    opp_seen[i] += 1
        for obj in obs.get("stack", ()):
            definition = obj.get("definition")
            if definition is None:
                continue
            i = slot(definition)
            if i is None:
                continue
            if obj.get("controller") == my_seat:
                my_seen[i] += 1
            else:
                opp_seen[i] += 1

        base = self.belief_base
        my_pool = np.maximum(my_counts - my_seen, 0).astype(np.float32) / 4.0
        opp_pool = np.maximum(opp_counts - opp_seen, 0).astype(np.float32) / 4.0
        vec[base:base + C] = my_pool
        vec[base + C:base + 2 * C] = opp_pool
