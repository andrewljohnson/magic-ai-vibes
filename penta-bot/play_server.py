"""Human-vs-bot play server for the penta engine.

Serves a local HTTP API (default port 4180) that lets a human play full
games against our CERTIFIED penta bot configuration: the value net
`penta_net.npz` plus the policy head `policy_head.ckpt-dagger1.npz`
blended at weight 0.25, chosen through trainer.choose() -- the exact
action-selection path gate.py certifies (myopic screen + dominance
prune + policy blend + rollout search, DEFAULT_SEARCH topk 4 / 1
playout / budget 120 / playout-max-eval 8, max-eval 16). Nothing is
reimplemented here; the bot seat literally calls trainer.choose() on
true game clones.

Every human choice IS a legalActions index -- mulligans, casts, combat,
response windows, mid-resolution decisions all arrive as entries in the
observation's legalActions list, and the client only ever POSTs one of
those indices back. The server adds human-readable labels derived from
the observation (hand/battlefield/stack names, targets, X values) but
never invents actions.

Concede: protocol 2 removed Concede from legalActions (upstream
"Stop offering bots the chance to resign") and the binding exposes no
concede call, so /api/concede ends the SESSION -- the game is scored
for the bot and a new game is offered. It is not an engine action.

Read-only with respect to the training stack: the net/head .npz files
are only ever np.load()ed; nothing under penta-bot/ is written. Safe to
run while a training run is active.

Usage:
    python3 play_server.py            # port 4180
    python3 play_server.py --port N

The web client is web/penta-play.html, served by the arena web server
(web/server.mjs, port 4173) or any static file server; it talks to this
server on http://localhost:4180.
"""

import argparse
import json
import os
import random
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))

# The certified play configuration (README "Policy-head prototype"):
# value net + policy head at blend weight 0.25. trainer.py picks the
# head up from these env vars at import time -- the same plumbing
# gate.py / scout.py workers use -- so they must be set BEFORE the
# trainer import. Explicit env from the caller wins (setdefault).
os.environ.setdefault(
    "PENTA_POLICY_NET", os.path.join(HERE, "policy_head.ckpt-dagger1.npz"))
os.environ.setdefault("PENTA_POLICY_WEIGHT", "0.15")

import trainer  # noqa: E402  (env vars above must precede this import)
from extractor import Extractor, import_penta  # noqa: E402
from net import Net  # noqa: E402

VALUE_NET_PATH = os.path.join(HERE, "penta_net.npz")
MAX_EVAL = 16          # gate.py default
SEARCH = trainer.DEFAULT_SEARCH
# Anti-hang cap on total decisions per game (generous: trainer caps
# training games at 600; a human game should end far earlier).
MAX_DECISIONS = 1500
# Cap on consecutive bot decisions per drive (a stuck loop guard).
MAX_BOT_BURST = 400

PENTA = import_penta()
NET = Net.load(VALUE_NET_PATH)
EXTRACTOR = Extractor.for_inputs(NET.inputs)
CATALOG = json.loads(PENTA.catalog())
DEF_INFO = {
    card["definition"]: {
        "name": card["name"],
        "kind": card["kind"],
        "manaCost": card.get("manaCost"),
        "power": card.get("power"),
        "toughness": card.get("toughness"),
        "rulesText": card.get("rulesText"),
    }
    for card in CATALOG["cards"]
    if card.get("legal", True)
}
DEF_NAME = {d: info["name"] for d, info in DEF_INFO.items()}


def bot_rng(seed, depth):
    """Deterministic per-decision RNG for the bot seat: reproducible by
    tests (rebuild the game, replay the history prefix, call
    trainer.choose with this same RNG) without sharing a stream."""
    return random.Random((seed * 1000003 + depth) % (2**31))


# -- action labels -------------------------------------------------------

GROUPS = {
    "KeepHand": "mulligan", "TakeMulligan": "mulligan",
    "BottomCards": "mulligan",
    "PlayLand": "land",
    "CastSpell": "cast",
    "DeclareAttacker": "attack", "FinishDeclaringAttackers": "attack",
    "DeclareBlocker": "block", "FinishDeclaringBlockers": "block",
    "AssignCombatDamage": "block",
    "ActivateAbility": "ability", "ActivateManaAbility": "ability",
    "PayLifeForMana": "ability",
    "ChooseDecision": "decision", "CancelDecision": "decision",
    "DiscardCards": "decision", "ChooseUntap": "decision",
    "PassPriority": "pass",
}


def _target_label(target, obs, viewer_seat):
    kind = target.get("type")
    if kind == "player":
        return "you" if target.get("seat") == viewer_seat else "opponent"
    if kind == "permanent":
        inst = target.get("instance", target.get("objectId"))
        for perm in obs.get("battlefield") or ():
            if perm["instance"] == inst:
                owner = ("your" if perm["controller"] == viewer_seat
                         else "their")
                return f"{owner} {perm['name']}"
        return f"permanent #{inst}"
    if kind == "spell":
        sid = target.get("stackId", target.get("objectId"))
        for item in obs.get("stack") or ():
            if item.get("stackId") == sid:
                return f"{item['name']} (on stack)"
        return f"spell #{sid}"
    return json.dumps(target)


def _card_names(ids, obs):
    """Resolve hand/battlefield instance ids to names."""
    names = {}
    for card in obs.get("hand") or ():
        names[card["instance"]] = card["name"]
    for perm in obs.get("battlefield") or ():
        names[perm["instance"]] = perm["name"]
    return [names.get(i, f"card #{i}") for i in ids or ()]


def action_label(action, obs, public_safe=False):
    """A human-readable label for one legalActions entry, derived only
    from the action and the acting seat's observation. public_safe
    suppresses hidden information (bottomed cards, library-zone decision
    picks) for the opponent-visible log."""
    t = action["type"]
    seat = obs["seat"]
    if t == "KeepHand":
        return "Keep hand"
    if t == "TakeMulligan":
        return "Mulligan"
    if t == "BottomCards":
        cards = action.get("cards") or ()
        if public_safe:
            n = len(cards)
            return f"Put {n} card{'s' if n != 1 else ''} on the bottom"
        return "Bottom " + ", ".join(_card_names(cards, obs))
    if t == "PassPriority":
        return "Pass"
    if t == "PlayLand":
        names = _card_names([action.get("card")], obs)
        return f"Play {names[0]}"
    if t == "CastSpell":
        names = _card_names([action.get("card")], obs)
        label = f"Cast {names[0]}"
        x = action.get("x") or 0
        if x:
            label += f" (X={x})"
        targets = action.get("targets") or ()
        if targets:
            label += " → " + ", ".join(
                _target_label(tg, obs, seat) for tg in targets)
        sacs = action.get("sacrifices") or ()
        if sacs:
            label += ", sacrificing " + ", ".join(_card_names(sacs, obs))
        return label
    if t == "ActivateAbility":
        names = _card_names([action.get("source")], obs)
        label = f"Activate {names[0]}"
        target = action.get("target")
        if target:
            label += " → " + _target_label(target, obs, seat)
        sac = action.get("sacrifice")
        if sac:
            label += ", sacrificing " + _card_names([sac], obs)[0]
        return label
    if t == "ActivateManaAbility":
        names = _card_names([action.get("source")], obs)
        color = action.get("color")
        return f"Tap {names[0]} for {color}" if color \
            else f"Tap {names[0]} for mana"
    if t == "PayLifeForMana":
        names = _card_names([action.get("source")], obs) \
            if action.get("source") is not None else ["a permanent"]
        return f"Pay life for mana ({names[0]})"
    if t == "DeclareAttacker":
        names = _card_names([action.get("attacker")], obs)
        return f"Attack with {names[0]}"
    if t == "FinishDeclaringAttackers":
        return "Finish declaring attackers"
    if t == "DeclareBlocker":
        blocker = _card_names([action.get("blocker")], obs)[0]
        attacker = _card_names([action.get("attacker")], obs)[0]
        return f"Block {attacker} with {blocker}"
    if t == "FinishDeclaringBlockers":
        return "Finish declaring blockers"
    if t == "AssignCombatDamage":
        return "Assign combat damage"
    if t == "DiscardCards":
        return "Discard " + ", ".join(
            _card_names(action.get("cards"), obs))
    if t == "ChooseUntap":
        return "Choose untap"
    if t == "CancelDecision":
        return "Cancel"
    if t == "ChooseDecision":
        decision = obs.get("decision") or {}
        options = {o["id"]: o for o in decision.get("options") or ()}
        picked = action.get("options") or ()
        if not picked:
            return "Choose nothing"
        hidden = any(options.get(i, {}).get("zone") in ("Library", "Hand")
                     for i in picked)
        if public_safe and hidden:
            n = len(picked)
            return f"Chose {n} card{'s' if n != 1 else ''}"
        labels = [options[i].get("label", f"option {i}")
                  if i in options else f"option {i}" for i in picked]
        return "Choose " + ", ".join(labels)
    return t  # honest fallback: the raw action type


def annotate_actions(obs):
    return [
        {
            "index": a["index"],
            "type": a["type"],
            "label": action_label(a, obs),
            "group": GROUPS.get(a["type"], "other"),
        }
        for a in obs.get("legalActions") or ()
    ]


# -- game session --------------------------------------------------------

class Session:
    def __init__(self, human_deck, bot_deck, human_seat, seed):
        self.human_seat = human_seat
        self.bot_seat = "p2" if human_seat == "p1" else "p1"
        self.human_deck = human_deck
        self.bot_deck = bot_deck
        self.seed = seed
        d1, d2 = ((human_deck, bot_deck) if human_seat == "p1"
                  else (bot_deck, human_deck))
        self.d1, self.d2 = d1, d2
        self.game = PENTA.Game(d1, d2, opponent="external", seed=seed)
        self.history = []       # every action index, both seats
        self.bot_decisions = []  # [{depth, index}] for reproducibility
        self.log = []           # public log entries, both seats
        self.conceded = False
        self.error = None
        self.last_obs = None    # last human observation (terminal fallback)

    def make_game(self):
        return PENTA.Game(self.d1, self.d2, opponent="external",
                          seed=self.seed)

    # -- bot seat --------------------------------------------------------

    def drive_bot(self):
        """Run trainer.choose() for the bot seat until the human holds
        the decision, the game ends, or a cap trips."""
        burst = 0
        while (self.game.result() is None and not self.conceded
               and self.error is None):
            if self.game.decision_seat() != self.bot_seat:
                break
            if len(self.history) >= MAX_DECISIONS or burst >= MAX_BOT_BURST:
                self.error = "decision cap reached (stalled game)"
                break
            obs = json.loads(self.game.observe())
            rng = bot_rng(self.seed, len(self.history))
            try:
                index, _, value = trainer.choose(
                    trainer.make_fork(self.make_game, self.history,
                                      self.game),
                    obs, NET, EXTRACTOR, rng, epsilon=0.0,
                    max_eval=MAX_EVAL, depth=len(self.history),
                    search=SEARCH)
            except ValueError as err:  # upstream engine fault
                self.error = f"engine fault in bot search: {err}"
                break
            action = next(a for a in obs["legalActions"]
                          if a["index"] == index)
            self.log.append({
                "seat": self.bot_seat, "bot": True,
                "turn": obs.get("turn"), "step": obs.get("step"),
                "pregame": bool(obs.get("pregame")),
                "type": action["type"],
                "label": action_label(action, obs, public_safe=True),
                "value": None if value is None else round(float(value), 3),
            })
            self.bot_decisions.append(
                {"depth": len(self.history), "index": index})
            self.game.act(index)
            self.history.append(index)
            burst += 1

    # -- human seat ------------------------------------------------------

    def human_actions(self):
        if self.game.result() is not None or self.conceded or self.error:
            return None, []
        if self.game.decision_seat() != self.human_seat:
            return None, []
        obs = json.loads(self.game.observe())
        return obs, obs.get("legalActions") or []

    def act_human(self, index):
        obs, actions = self.human_actions()
        if obs is None:
            return False, "no decision pending for the human seat"
        legal = {a["index"] for a in actions}
        if index not in legal:
            return False, f"index {index} is not in legalActions"
        action = next(a for a in actions if a["index"] == index)
        self.log.append({
            "seat": self.human_seat, "bot": False,
            "turn": obs.get("turn"), "step": obs.get("step"),
            "pregame": bool(obs.get("pregame")),
            "type": action["type"],
            "label": action_label(action, obs, public_safe=True),
            "value": None,
        })
        self.game.act(index)
        self.history.append(index)
        return True, None

    # -- state payload ---------------------------------------------------

    def result_payload(self):
        if self.conceded:
            return {"winner": self.bot_seat, "reason": "concede",
                    "humanWon": False}
        if self.error:
            return {"winner": None, "reason": self.error, "humanWon": None}
        result = self.game.result()
        if result is None:
            return None
        return {
            "winner": result,
            "reason": "draw" if result == "draw" else "win",
            "humanWon": (None if result == "draw"
                         else result == self.human_seat),
        }

    def state(self):
        result = self.result_payload()
        obs = None
        try:
            obs = json.loads(self.game.observe(self.human_seat))
            self.last_obs = obs
        except Exception:
            obs = self.last_obs  # terminal states may refuse observe()
        payload = {
            "active": True,
            "version": len(self.history),
            "humanSeat": self.human_seat,
            "botSeat": self.bot_seat,
            "humanDeck": self.human_deck,
            "botDeck": self.bot_deck,
            "seed": self.seed,
            "result": result,
            "log": self.log[-80:],
            "history": self.history,
            "botDecisions": self.bot_decisions,
            "botThinking": False,
        }
        if obs is not None:
            payload["obs"] = {
                key: obs.get(key) for key in (
                    "turn", "step", "pregame", "activeSeat", "prioritySeat",
                    "life", "manaPools", "librarySizes", "opponentHandSize",
                    "hand", "battlefield", "stack", "graveyards", "exiles",
                    "decision")
            }
            if (result is None
                    and self.game.decision_seat() == self.human_seat):
                payload["actions"] = annotate_actions(obs)
            else:
                payload["actions"] = []
        else:
            payload["obs"] = None
            payload["actions"] = []
        return payload


# -- HTTP plumbing -------------------------------------------------------

LOCK = threading.Lock()
SESSION = None


def meta_payload():
    return {
        "decks": list(PENTA.deck_names()),
        "trainedDecks": list(trainer.DECKS),
        "engineVersion": PENTA.engine_version(),
        "protocolVersion": PENTA.protocol_version(),
        "valueNet": os.path.basename(VALUE_NET_PATH),
        "policyHead": os.path.basename(os.environ["PENTA_POLICY_NET"]),
        "policyWeight": trainer.POLICY_WEIGHT,
        "search": SEARCH._asdict(),
        "maxEval": MAX_EVAL,
        "netInputs": NET.inputs,
        "catalog": {str(d): info for d, info in DEF_INFO.items()},
        "concedeIsSessionLevel": True,
    }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):  # quiet
        pass

    def _send(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods",
                         "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def _body(self):
        length = int(self.headers.get("Content-Length") or 0)
        if not length:
            return {}
        try:
            return json.loads(self.rfile.read(length))
        except json.JSONDecodeError:
            return None

    def do_GET(self):
        global SESSION
        path = self.path.split("?")[0]
        if path == "/api/meta":
            return self._send(200, meta_payload())
        if path == "/api/state":
            with LOCK:
                if SESSION is None:
                    return self._send(200, {"active": False})
                return self._send(200, SESSION.state())
        static = {"/": ("penta-play.html", "text/html; charset=utf-8"),
                  "/penta-play.html": ("penta-play.html",
                                       "text/html; charset=utf-8"),
                  "/site-nav.js": ("site-nav.js",
                                   "text/javascript; charset=utf-8")}
        if path in static:
            name, ctype = static[path]
            page = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "web", name)
            try:
                with open(page, "rb") as fh:
                    body = fh.read()
            except OSError:
                return self._send(404, {"error": f"{name} not found"})
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return None
        return self._send(404, {"error": f"no route {path}"})

    def do_POST(self):
        global SESSION
        path = self.path.split("?")[0]
        body = self._body()
        if body is None:
            return self._send(400, {"error": "invalid JSON body"})

        if path == "/api/new":
            decks = set(PENTA.deck_names())
            human_deck = body.get("humanDeck")
            bot_deck = body.get("botDeck")
            if human_deck not in decks or bot_deck not in decks:
                return self._send(400, {
                    "error": "unknown deck",
                    "decks": sorted(decks)})
            seat = body.get("humanSeat", "random")
            if seat == "random":
                seat = random.choice(["p1", "p2"])
            if seat not in ("p1", "p2"):
                return self._send(400, {"error": "humanSeat must be "
                                                 "p1, p2, or random"})
            seed = body.get("seed")
            if seed is None:
                seed = random.randrange(2**31)
            if not isinstance(seed, int) or seed < 0:
                return self._send(400, {"error": "seed must be a "
                                                 "non-negative integer"})
            with LOCK:
                SESSION = Session(human_deck, bot_deck, seat, seed)
                SESSION.drive_bot()
                return self._send(200, SESSION.state())

        if path == "/api/act":
            with LOCK:
                if SESSION is None:
                    return self._send(409, {"error": "no active game"})
                version = body.get("version")
                if version is not None \
                        and version != len(SESSION.history):
                    return self._send(409, {
                        "error": "stale state; refetch /api/state",
                        "version": len(SESSION.history)})
                index = body.get("index")
                if not isinstance(index, int):
                    return self._send(400, {"error": "index must be an "
                                                     "integer"})
                ok, err = SESSION.act_human(index)
                if not ok:
                    return self._send(400, {"error": err})
                SESSION.drive_bot()
                return self._send(200, SESSION.state())

        if path == "/api/concede":
            with LOCK:
                if SESSION is None:
                    return self._send(409, {"error": "no active game"})
                if SESSION.game.result() is None:
                    SESSION.conceded = True
                return self._send(200, SESSION.state())

        return self._send(404, {"error": f"no route {path}"})


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--port", type=int, default=4180)
    parser.add_argument("--host", default="127.0.0.1")
    args = parser.parse_args()
    print(f"penta play server: engine {PENTA.engine_version()} protocol "
          f"{PENTA.protocol_version()}; value net "
          f"{os.path.basename(VALUE_NET_PATH)} ({NET.inputs} inputs), "
          f"policy head {os.path.basename(os.environ['PENTA_POLICY_NET'])} "
          f"@ w={trainer.POLICY_WEIGHT}, search {SEARCH}")
    print(f"listening on http://{args.host}:{args.port} "
          f"(client: web/penta-play.html)")
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
