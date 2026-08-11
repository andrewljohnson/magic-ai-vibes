"""End-to-end test for play_server.py.

Launches the server on a test port, drives full games through the HTTP
API with a seeded random policy on the human seat, and asserts:

  A. every human move the server accepts was in the served legalActions
     list, illegal indices are rejected (400), stale versions 409;
  B. games reach a real engine result (p1/p2/draw) with no crashes;
  C. the bot seat's recorded choices REPRODUCE trainer.choose() exactly:
     the game is rebuilt offline (same decks/seed), the history replayed,
     and choose() re-run with the server's per-decision RNG derivation
     and the same certified net/head/search config -- indices must match.

Read-only over all training artifacts. Run while training is active is
fine (single extra process, one game at a time).

Usage:
    python3 test_play_server.py            # 3 games
    python3 test_play_server.py --games N --port 4181
"""

import argparse
import json
import os
import random
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))

# Must match play_server.py's certified-config env (set before the
# trainer import so the policy head loads identically).
os.environ.setdefault(
    "PENTA_POLICY_NET", os.path.join(HERE, "policy_head.ckpt-dagger1.npz"))
os.environ.setdefault("PENTA_POLICY_WEIGHT", "0.25")

MATCHUPS = [  # (human deck, bot deck, human seat)
    ("White Weenie", "Sligh", "p1"),
    ("Sligh", "The Deck", "p2"),
    ("Goblins", "Counterburn", "p1"),
    ("Erhnamgeddon", "Mono Black", "p2"),
    ("Jeskai Aggro", "White Weenie", "p1"),
]
MAX_HUMAN_ACTS = 700
SPOT_CHECKS_PER_GAME = 6


def api(port, path, body=None, expect_error=False):
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}",
        data=None if body is None else json.dumps(body).encode(),
        headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=300) as resp:
            return resp.status, json.loads(resp.read())
    except urllib.error.HTTPError as err:
        payload = json.loads(err.read() or b"{}")
        if not expect_error:
            raise AssertionError(
                f"{path} -> HTTP {err.code}: {payload}") from err
        return err.code, payload


def wait_ready(port, proc, timeout=60):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if proc.poll() is not None:
            raise AssertionError(
                f"server exited early with code {proc.returncode}")
        try:
            status, meta = api(port, "/api/meta")
            if status == 200:
                return meta
        except (urllib.error.URLError, ConnectionError, OSError):
            time.sleep(0.5)
    raise AssertionError("server did not become ready")


def play_one_game(port, human_deck, bot_deck, human_seat, seed, rng):
    status, state = api(port, "/api/new", {
        "humanDeck": human_deck, "botDeck": bot_deck,
        "humanSeat": human_seat, "seed": seed})
    assert status == 200, state
    assert state["humanSeat"] == human_seat
    assert state["seed"] == seed
    acts_taken = 0
    while state["result"] is None:
        assert acts_taken < MAX_HUMAN_ACTS, "game did not terminate"
        actions = state["actions"]
        assert actions, (
            f"no result and no legal actions at version {state['version']}")
        for a in actions:  # every action carries index/type/label/group
            assert isinstance(a["index"], int) and a["label"] and a["group"]
        picked = rng.choice(actions)["index"]
        status, state = api(port, "/api/act",
                            {"index": picked, "version": state["version"]})
        assert status == 200, state
        acts_taken += 1
    result = state["result"]
    assert result["winner"] in ("p1", "p2", "draw"), result
    return state


def check_illegal_and_stale(port, state):
    """A fresh game: illegal index -> 400, stale version -> 409."""
    legal = {a["index"] for a in state["actions"]}
    bad = max(legal) + 999
    status, payload = api(port, "/api/act",
                          {"index": bad, "version": state["version"]},
                          expect_error=True)
    assert status == 400, (status, payload)
    status, payload = api(port, "/api/act",
                          {"index": min(legal),
                           "version": state["version"] + 5},
                          expect_error=True)
    assert status == 409, (status, payload)


def verify_bot_choices(final_state, rng):
    """Rebuild the game offline and assert sampled bot decisions match
    trainer.choose() under the server's per-decision RNG derivation."""
    import trainer
    from extractor import Extractor, import_penta
    from net import Net
    from play_server import MAX_EVAL, VALUE_NET_PATH, bot_rng

    penta = import_penta()
    net = Net.load(VALUE_NET_PATH)
    extractor = Extractor.for_inputs(net.inputs)
    seed = final_state["seed"]
    human_seat = final_state["humanSeat"]
    d1 = (final_state["humanDeck"] if human_seat == "p1"
          else final_state["botDeck"])
    d2 = (final_state["botDeck"] if human_seat == "p1"
          else final_state["humanDeck"])

    def make_game():
        return penta.Game(d1, d2, opponent="external", seed=seed)

    history = final_state["history"]
    bot_decisions = final_state["botDecisions"]
    assert bot_decisions, "bot made no decisions?"
    sample = sorted(rng.sample(
        range(len(bot_decisions)),
        min(SPOT_CHECKS_PER_GAME, len(bot_decisions))))

    game = make_game()
    replayed = 0
    checked = 0
    for pos in sample:
        rec = bot_decisions[pos]
        depth = rec["depth"]
        while replayed < depth:
            game.act(history[replayed])
            replayed += 1
        assert game.result() is None
        obs = json.loads(game.observe())
        index, _, _ = trainer.choose(
            trainer.make_fork(make_game, history[:depth], game), obs,
            net, extractor, bot_rng(seed, depth), epsilon=0.0,
            max_eval=MAX_EVAL, depth=depth, search=trainer.DEFAULT_SEARCH)
        assert index == rec["index"], (
            f"bot divergence at depth {depth}: server played "
            f"{rec['index']}, offline choose() played {index}")
        checked += 1
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--games", type=int, default=3)
    parser.add_argument("--port", type=int, default=4181)
    parser.add_argument("--seed-base", type=int, default=777000)
    args = parser.parse_args()

    proc = subprocess.Popen(
        [sys.executable, os.path.join(HERE, "play_server.py"),
         "--port", str(args.port)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        meta = wait_ready(args.port, proc)
        print(f"server ready: engine {meta['engineVersion']} protocol "
              f"{meta['protocolVersion']}, net {meta['valueNet']} + "
              f"{meta['policyHead']} @ w={meta['policyWeight']}, "
              f"{len(meta['decks'])} decks")
        assert meta["policyWeight"] == 0.25
        assert set(meta["trainedDecks"]) <= set(meta["decks"])

        rng = random.Random(20260811)
        finals = []
        for g in range(args.games):
            human_deck, bot_deck, human_seat = MATCHUPS[g % len(MATCHUPS)]
            seed = args.seed_base + g
            t0 = time.time()
            # Start the game, probe rejection paths once, then play out.
            status, state = api(args.port, "/api/new", {
                "humanDeck": human_deck, "botDeck": bot_deck,
                "humanSeat": human_seat, "seed": seed})
            assert status == 200
            if g == 0:
                check_illegal_and_stale(args.port, state)
                print("  illegal-index 400 and stale-version 409: ok")
            final = play_one_game(args.port, human_deck, bot_deck,
                                  human_seat, seed, rng)
            finals.append(final)
            r = final["result"]
            outcome = ("draw" if r["winner"] == "draw"
                       else "human win" if r["humanWon"] else "bot win")
            print(f"  game {g + 1}: {human_deck} (you, {human_seat}) vs "
                  f"{bot_deck} seed {seed} -> {outcome} "
                  f"[{final['version']} actions, "
                  f"{len(final['botDecisions'])} bot decisions, "
                  f"{time.time() - t0:.0f}s]")

        # Concede path on a fresh game.
        status, state = api(args.port, "/api/new", {
            "humanDeck": "Sligh", "botDeck": "White Weenie",
            "humanSeat": "p1", "seed": 424243})
        assert status == 200
        status, state = api(args.port, "/api/concede", {})
        assert status == 200
        assert state["result"]["reason"] == "concede"
        assert state["result"]["winner"] == state["botSeat"]
        print("  concede (session-level): ok")
    finally:
        proc.terminate()
        proc.wait(timeout=10)

    print("verifying bot choices against trainer.choose() offline…")
    rng = random.Random(4242)
    total = 0
    for final in finals:
        total += verify_bot_choices(final, rng)
    print(f"  {total} sampled bot decisions across {len(finals)} games "
          f"all match trainer.choose()")
    print("ALL CHECKS PASSED")


if __name__ == "__main__":
    main()
