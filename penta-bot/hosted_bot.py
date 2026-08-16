"""Hosted-play daemon: register on a penta server, heartbeat, play games.

Implements the docs/bots.md registry contract against a local dev
server or the public deployment:

  - POST /_bots/register {name, deck} -> {id, token}  (once; persisted
    to hosted_bot_state.json so restarts reuse the registration)
  - POST /_bots/<id>/heartbeat {token, done} -> {invites} every ~10s
    (presence window 45s); auto-reregisters on 4xx (idle registrations
    are deleted after a day)
  - per room: GET /_game/<room>/opponent + POST /_game/<room>/command
    {t: botAct, index} with the invite's x-penta-token header

The brain is hosted_policy.HostedPolicy (observation-only; no engine).
Every move races a deadline: if scoring somehow exceeds --move-budget
seconds (default 10; the room clock is 60), the instant fallback is the
first_bot-shaped tie ordering. Per-game JSONL transcripts land in
hosted-games/ for later scouting; stdout stays quiet (one line per
lifecycle event and per finished game).

Usage:
    python3 hosted_bot.py --server http://localhost:3000        # build
    python3 hosted_bot.py --server https://penta.lacker.workers.dev
"""

import argparse
import json
import os
import signal
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from hosted_policy import HostedPolicy, _TIE_ORDER  # noqa: E402

STATE_PATH = os.path.join(HERE, "hosted_bot_state.json")
LOG_DIR = os.path.join(HERE, "hosted-games")

_shutdown = False


def _request(url, payload=None, headers=None, timeout=30):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(url, data=data, headers={
        "content-type": "application/json",
        # The public deployment's WAF rejects the default Python-urllib
        # agent with 403; identify ourselves honestly instead.
        "user-agent": "SPZ-hosted-bot/1.0 (penta-bot; +lacker/penta#57)",
        **(headers or {})})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def register(server, name, deck):
    me = _request(f"{server}/_bots/register",
                  {"name": name, "deck": deck})
    state = {"server": server, "name": name, "deck": deck, **me}
    with open(STATE_PATH, "w") as f:
        json.dump(state, f)
    print(f"registered: id={me['id']} name={name} deck={deck} "
          f"server={server}", flush=True)
    return state


def load_or_register(server, name, deck):
    if os.path.exists(STATE_PATH):
        with open(STATE_PATH) as f:
            state = json.load(f)
        if state.get("server") == server:
            print(f"resuming registration id={state['id']}", flush=True)
            return state
    return register(server, name, deck)


def fallback_choice(obs):
    """Deadline fallback: pure tie-order shape, no scoring."""
    actions = obs.get("legalActions") or ()
    if not actions:
        return 0
    best = min(actions, key=lambda a: (_TIE_ORDER.get(a.get("type"), 7),
                                       a["index"]))
    return best["index"]


def play_room(server, room, token, policy, move_budget):
    headers = {"x-penta-token": token}
    log_path = os.path.join(
        LOG_DIR, f"{time.strftime('%Y%m%d-%H%M%S')}-{room}.jsonl")
    os.makedirs(LOG_DIR, exist_ok=True)
    moves = 0
    t_start = time.time()
    with open(log_path, "w") as log:
        while not _shutdown:
            try:
                view = _request(f"{server}/_game/{room}/opponent",
                                headers=headers)
            except urllib.error.URLError as error:
                log.write(json.dumps({"t": "fetch-error",
                                      "error": str(error)}) + "\n")
                time.sleep(1)
                continue
            if view.get("result"):
                log.write(json.dumps({"t": "result",
                                      "result": view["result"],
                                      "moves": moves,
                                      "seconds": round(
                                          time.time() - t_start)}) + "\n")
                print(f"game {room}: {view['result']} after {moves} moves",
                      flush=True)
                return
            if not view.get("deciding"):
                time.sleep(0.25)
                continue
            obs = view["observation"]
            t0 = time.time()
            try:
                index = policy.choose(obs)
            except Exception as error:
                index = fallback_choice(obs)
                log.write(json.dumps({"t": "choose-error",
                                      "error": str(error)[:200]}) + "\n")
            took = time.time() - t0
            if took > move_budget:
                log.write(json.dumps({"t": "slow-move",
                                      "seconds": round(took, 2)}) + "\n")
            # Chosen action detail + reconstruction survivor count so live
            # blunders are reproducible and attributable to a path: worlds=0
            # means every hypothesis reconstruction failed and the move came
            # from the UNPRUNED shaped fallback (HostedPolicy.choose), not the
            # dominance-pruned determinized search.
            acts = obs.get("legalActions") or ()
            chosen = next((a for a in acts if a.get("index") == index), None)
            worlds = getattr(policy, "worlds_used", None)
            log.write(json.dumps({
                "t": "move", "turn": obs.get("turn"),
                "step": obs.get("step"), "index": index,
                "action": ({"type": chosen.get("type"),
                            "name": chosen.get("name"),
                            "target": chosen.get("target")}
                           if chosen else None),
                "worlds": worlds[-1] if worlds else None,
                "n_actions": len(acts),
                "ms": round(1000 * took),
                "obs": obs}) + "\n")
            try:
                _request(f"{server}/_game/{room}/command",
                         {"t": "botAct", "index": index}, headers=headers)
            except urllib.error.HTTPError as error:
                log.write(json.dumps({"t": "command-error",
                                      "code": error.code}) + "\n")
                if error.code in (403, 404, 410):
                    return  # room gone or token invalid
                time.sleep(0.5)
            moves += 1


def main():
    global _shutdown
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--server", default="http://localhost:3000")
    parser.add_argument("--name", default="SPZ")
    parser.add_argument("--deck", default="Sligh")
    parser.add_argument("--weight", type=float, default=0.15)
    parser.add_argument("--engine-dir", default="",
                        help="penta-py build dir for Game.from_observation "
                             "determinized search (lacker/penta#57); empty "
                             "= shaped observation-only policy")
    parser.add_argument("--k-worlds", type=int, default=4)
    parser.add_argument("--value-net", default="penta_net.npz",
                        help="value net (.npz); the determinized-era "
                             "deliverable when promoted")
    parser.add_argument("--move-budget", type=float, default=20.0)
    parser.add_argument("--heartbeat", type=float, default=10.0)
    args = parser.parse_args()

    def on_signal(signum, frame):
        global _shutdown
        _shutdown = True
        print("shutting down (stop heartbeating = go offline)", flush=True)

    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)

    if args.engine_dir:
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "penta", os.path.join(args.engine_dir, "penta.so"))
        engine = importlib.util.module_from_spec(spec)
        sys.modules["penta"] = engine
        spec.loader.exec_module(engine)
        from hosted_policy import DeterminizedPolicy
        policy = DeterminizedPolicy(
            engine, our_deck=args.deck, k_worlds=args.k_worlds,
            weight=args.weight, value_path=args.value_net,
            fail_log=os.path.join(HERE, "recon-failures.jsonl"),
            time_budget=args.move_budget)
        print(f"determinized brain: engine "
              f"{engine.engine_version()}/p{engine.protocol_version()}, "
              f"K={args.k_worlds}", flush=True)
    else:
        policy = HostedPolicy(weight=args.weight,
                              value_path=args.value_net)
    state = load_or_register(args.server, args.name, args.deck)
    done = []
    beats = 0
    while not _shutdown:
        try:
            reply = _request(
                f"{args.server}/_bots/{state['id']}/heartbeat",
                {"token": state["token"], "done": done})
            done = []
            beats += 1
            if beats % 360 == 1:  # roughly hourly
                print(f"heartbeat ok ({beats} total)", flush=True)
        except urllib.error.HTTPError as error:
            if 400 <= error.code < 500:
                print(f"heartbeat {error.code}: re-registering", flush=True)
                state = register(args.server, args.name, args.deck)
                continue
            time.sleep(args.heartbeat)
            continue
        except urllib.error.URLError as error:
            print(f"server unreachable: {error}", flush=True)
            time.sleep(args.heartbeat)
            continue
        for invite in reply.get("invites", ()):
            room = invite["room"]
            print(f"invite: room {room}", flush=True)
            try:
                play_room(args.server, room, invite["token"], policy,
                          args.move_budget)
            finally:
                done.append(room)
        if not reply.get("invites"):
            time.sleep(args.heartbeat)


if __name__ == "__main__":
    main()
