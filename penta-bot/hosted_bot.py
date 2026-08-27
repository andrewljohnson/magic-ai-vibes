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
        # agent with 403; identify ourselves accurately instead.
        "user-agent": "SPZ-hosted-bot/1.0 (penta-bot; +lacker/penta#57)",
        **(headers or {})})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def compatibility(engine=None):
    """The manifest the server matches us against.

    Protocol 22 made `protocolVersion` the breaking bot-wire EPOCH, and
    hosted bots must declare `{protocolVersion, capabilities,
    requiredCapabilities}` at registration and heartbeat. We were sending
    neither, so the server defaulted us to protocol 21 and refused:

        409 {"code":"incompatible_bot",
             "server":{"protocolVersion":29,...},
             "bot":{"protocolVersion":21,...}}

    Capabilities stay EMPTY on purpose. The docs are explicit that a bot
    which only reads `legalActions` must not echo the server's advertised
    capabilities without implementing them -- claiming
    `reconstruction.checkpoint.v8` we do not support would fail later and
    less legibly.
    """
    version = 22
    if engine is not None:
        try:
            version = int(engine.protocol_version())
        except Exception:
            pass
    return {"protocolVersion": version,
            "capabilities": [],
            "requiredCapabilities": []}


def register(server, name, deck, engine=None):
    me = _request(f"{server}/_bots/register",
                  {"name": name, "deck": deck,
                   "compatibility": compatibility(engine)})
    state = {"server": server, "name": name, "deck": deck, **me}
    with open(STATE_PATH, "w") as f:
        json.dump(state, f)
    print(f"registered: id={me['id']} name={name} deck={deck} "
          f"server={server}", flush=True)
    return state


def load_or_register(server, name, deck, engine=None):
    if os.path.exists(STATE_PATH):
        with open(STATE_PATH) as f:
            state = json.load(f)
        if state.get("server") == server:
            print(f"resuming registration id={state['id']}", flush=True)
            return state
    return register(server, name, deck, engine)


def fallback_choice(obs):
    """Deadline fallback: pure tie-order shape, no scoring."""
    actions = obs.get("legalActions") or ()
    if not actions:
        return 0
    best = min(actions, key=lambda a: (_TIE_ORDER.get(a.get("type"), 7),
                                       a["index"]))
    return best["index"]


def play_room(server, room, token, policy, move_budget, max_room_secs=900):
    headers = {"x-penta-token": token}
    log_path = os.path.join(
        LOG_DIR, f"{time.strftime('%Y%m%d-%H%M%S')}-{room}.jsonl")
    os.makedirs(LOG_DIR, exist_ok=True)
    moves = 0
    t_start = time.time()
    with open(log_path, "w") as log:
        while not _shutdown:
            # A room that never resolves used to hang the daemon forever:
            # the opponent walks away, the server declares nothing, and this
            # loop polls until the process dies. Give up and let the caller
            # mark it done so presence is not held hostage by one game.
            if time.time() - t_start > max_room_secs:
                log.write(json.dumps({"t": "abandoned",
                                      "seconds": round(time.time() - t_start),
                                      "moves": moves}) + "\n")
                print(f"game {room}: abandoned after "
                      f"{round(time.time() - t_start)}s with no result",
                      flush=True)
                return
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
                try:
                    index = policy.choose(obs, raw_json=json.dumps(obs))
                except TypeError:
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
    parser.add_argument("--actor", default=None,
                        help="AAC actor .npz to play with (the trained bot). "
                             "Without it, the legacy determinized nets are "
                             "used.")
    parser.add_argument("--actor-hidden", type=int, default=256)
    parser.add_argument("--az", default=None,
                        help="AlphaZero save-prefix to play with, e.g. "
                             "'az_deep' (reads <prefix>_value.spzw and "
                             "<prefix>_policy.azp). Plays WITH SEARCH, "
                             "which is worth ~11 points over the raw "
                             "policy: 35.8%% at 1 sim, 41.7%% at 32, "
                             "46.7%% at 128. Takes precedence over --actor.")
    parser.add_argument("--az-iters", type=int, default=128,
                        help="search simulations per move. More is "
                             "stronger and costs clock; 128 measured "
                             "+11 points over none.")
    parser.add_argument("--az-best", action="store_true",
                        help="use the <prefix>_{policy_best,value_best} "
                             "checkpoint -- the best-GATED net rather than "
                             "whatever the live run last exported.")
    parser.add_argument("--move-budget", type=float, default=20.0)
    parser.add_argument("--heartbeat", type=float, default=10.0)
    args = parser.parse_args()

    def on_signal(signum, frame):
        global _shutdown
        _shutdown = True
        print("shutting down (stop heartbeating = go offline)", flush=True)

    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)

    # The engine is loaded whenever a dir is given, because the AAC actor
    # NEEDS it: without reconstruction it scores approximated afterstates
    # and drops from 56% to 3.8%. --actor therefore takes precedence over
    # the legacy determinized brain, rather than the other way round.
    engine = None
    if args.engine_dir:
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "penta", os.path.join(args.engine_dir, "penta.so"))
        engine = importlib.util.module_from_spec(spec)
        sys.modules["penta"] = engine
        spec.loader.exec_module(engine)

    if args.az:
        from hosted_policy import AzSearchPolicy
        # The best-gated checkpoint is the one to deploy: a live run's
        # current export can be mid-regression, and gates measured drops of
        # 5-11 points between promotions.
        pol = f"{args.az}_policy_best.azp" if args.az_best else \
            f"{args.az}_policy.azp"
        val = f"{args.az}_value_best.spzw" if args.az_best else \
            f"{args.az}_value.spzw"
        policy = AzSearchPolicy(val, pol, our_deck=args.deck,
                                iters=args.az_iters)
        print(f"AlphaZero + SEARCH: {pol} / {val} deck={args.deck} "
              f"iters={args.az_iters}", flush=True)
    elif args.actor:
        from hosted_policy import AacPolicy
        policy = AacPolicy(args.actor, hidden=args.actor_hidden,
                           our_deck=args.deck, engine=engine)
        kind = "reconstructed afterstates" if engine else \
            "APPROXIMATED afterstates -- pass --engine-dir, this scores 3.8%"
        print(f"AAC actor: {args.actor} deck={args.deck} ({kind})",
              flush=True)
    elif args.engine_dir:
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
    state = load_or_register(args.server, args.name, args.deck, engine)

    # Heartbeat on its OWN thread. It used to share the main loop with
    # play_room, so while a game was in progress nothing was sent -- and
    # the presence window is 45s. One game that never resolved took the bot
    # offline and left it there, still running, invisible in the registry.
    # Presence must not depend on what a game is doing.
    import queue
    import threading
    invites = queue.Queue()
    hb = {"state": state, "done": [], "beats": 0}
    lock = threading.Lock()

    def heartbeat_loop():
        """Heartbeat forever. This thread MUST NOT die.

        It did: one `TimeoutError` from an SSL read escaped both handlers
        below -- it is an OSError, not a urllib URLError -- and killed the
        thread. The process stayed up (the main thread blocks on the invite
        queue), so every liveness check said "daemon running" while the
        server had long since dropped us from its presence list. The bot was
        offline for twelve hours and nothing reported it.

        So: catch EVERYTHING, and make silence itself an alarm.
        """
        last_ok = time.time()
        warned = False
        while not _shutdown:
            try:
                with lock:
                    sid, tok = hb["state"]["id"], hb["state"]["token"]
                    done, hb["done"] = hb["done"], []
                reply = _request(f"{args.server}/_bots/{sid}/heartbeat",
                                 {"token": tok, "done": done,
                                  "compatibility": compatibility(engine)})
                hb["beats"] += 1
                last_ok, warned = time.time(), False
                if hb["beats"] % 360 == 1:      # roughly hourly
                    print(f"heartbeat ok ({hb['beats']} total)", flush=True)
                for inv in reply.get("invites", ()):
                    invites.put(inv)
            except urllib.error.HTTPError as error:
                if 400 <= error.code < 500:
                    print(f"heartbeat {error.code}: re-registering",
                          flush=True)
                    with lock:
                        # `engine` matters: without it the manifest claims
                        # protocol 22 and the server answers 409 forever.
                        hb["state"] = register(args.server, args.name,
                                               args.deck, engine)
                else:
                    print(f"heartbeat {error.code}; retrying", flush=True)
            except Exception as error:      # noqa: BLE001 -- see docstring
                print(f"heartbeat failed ({type(error).__name__}: {error}); "
                      f"retrying", flush=True)

            stale = time.time() - last_ok
            if stale > 45 and not warned:
                # The server's presence window is 45s, so past this we are
                # NOT listed and nobody can invite us.
                print(f"OFFLINE: no successful heartbeat in {stale:.0f}s -- "
                      f"the server has dropped us from its bot list",
                      flush=True)
                warned = True
            if stale > 300:
                print("re-registering after 5 minutes offline", flush=True)
                try:
                    with lock:
                        hb["state"] = register(args.server, args.name,
                                               args.deck, engine)
                    last_ok, warned = time.time(), False
                except Exception as error:      # noqa: BLE001
                    print(f"re-register failed ({error})", flush=True)
            time.sleep(args.heartbeat)

    threading.Thread(target=heartbeat_loop, daemon=True).start()

    seen = set()
    while not _shutdown:
        try:
            invite = invites.get(timeout=args.heartbeat)
        except queue.Empty:
            continue
        room = invite["room"]
        if room in seen:            # the same invite can arrive twice
            continue
        seen.add(room)
        print(f"invite: room {room}", flush=True)
        try:
            play_room(args.server, room, invite["token"], policy,
                      args.move_budget)
        finally:
            with lock:
                hb["done"].append(room)


if __name__ == "__main__":
    main()
