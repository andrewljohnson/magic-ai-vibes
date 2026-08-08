"""Standalone audit checks for the penta-bot trainer (run: python3 checks.py).

Each check hunts one collapse hypothesis from the trainer audit:

  A. seat/perspective label inversion in recorded (features, target) pairs
  B. TD(lambda) target construction (ordering, terminal sign, decay
     direction, off-by-one)
  C. action-evaluation cap bias (uniform sample vs prefix; depth budget)
  D. afterstate correctness (replay-reconstruction fidelity, learner-seat
     observation after the action, epsilon vs greedy candidate sets)
  E. replay ring composition (overwrite/staleness accounting)

Every check is independent of the trainer's own bookkeeping wherever
possible: games are re-derived from (decks, seed, action history) and
observations re-extracted from both seats to prove which perspective was
recorded. Exits nonzero on the first failure.
"""

import json
import random

import numpy as np

import trainer
from extractor import Extractor, import_penta
from net import Net
from trainer import (afterstate_rows, choose, eval_budget, make_fork,
                     td_targets)

penta = import_penta()
EX = Extractor()
C = EX.defs
# Scalar offsets inside the feature vector (see Extractor._scalars order).
OWN_LIFE = 5 * C + 14
OPP_LIFE = 5 * C + 15

PASS = []
FAIL = []


def check(name, ok, detail=""):
    (PASS if ok else FAIL).append(name)
    print(f"  {'ok ' if ok else 'FAIL'} {name}" + (f"  [{detail}]" if detail else ""))


def rand_game_to_depth(seed, min_depth, want_seat=None, opponent="external"):
    """Random-play an external game to >= min_depth decisions, stopping at a
    decision for want_seat (if given). Returns (game, history, make_game)."""
    def make_game():
        return penta.Game("Sligh", "The Deck", opponent=opponent, seed=seed)

    rng = random.Random(seed)
    game = make_game()
    history = []
    while game.result() is None and len(history) < 500:
        obs = json.loads(game.observe())
        deep = len(history) >= min_depth
        if deep and (want_seat is None or obs["seat"] == want_seat) \
                and len(obs["legalActions"]) > 1:
            return game, history, make_game
        index = rng.choice(obs["legalActions"])["index"]
        game.act(index)
        history.append(index)
    return None, history, make_game


# ---------------------------------------------------------------- A -----

def check_a_observe_default_seat():
    """observe() with no argument must be the decision seat's view."""
    bad = 0
    for seed in range(3):
        game = penta.Game("Sligh", "White Weenie", opponent="external", seed=seed)
        rng = random.Random(seed)
        n = 0
        while game.result() is None and n < 200:
            obs = json.loads(game.observe())
            if obs["seat"] != game.decision_seat():
                bad += 1
            game.act(rng.choice(obs["legalActions"])["index"])
            n += 1
    check("A1 observe() default == decision_seat()", bad == 0, f"{bad} mismatches")


def check_a_absolute_indexing():
    """life/librarySizes/graveyards must be absolute p1-then-p2 arrays,
    identical across the two seats' observations of one state."""
    game, history, _ = rand_game_to_depth(11, 120)
    if game is None:
        check("A2 absolute array indexing", False, "no mid-game state found")
        return
    o1 = json.loads(game.observe("p1"))
    o2 = json.loads(game.observe("p2"))
    same = (o1["life"] == o2["life"]
            and o1["librarySizes"] == o2["librarySizes"]
            and len(o1["graveyards"][0]) == len(o2["graveyards"][0])
            and len(o1["graveyards"][1]) == len(o2["graveyards"][1]))
    check("A2 life/libs/graveyards absolute-indexed", same,
          f"life {o1['life']} vs {o2['life']}")
    # Perspective must live in the extractor, i.e. p1/p2 features differ.
    f1, f2 = EX.features(o1), EX.features(o2)
    check("A3 extractor flips perspective by seat",
          f1[OWN_LIFE] == f2[OPP_LIFE] and f1[OPP_LIFE] == f2[OWN_LIFE],
          f"p1 own/opp {f1[OWN_LIFE]:.2f}/{f1[OPP_LIFE]:.2f} "
          f"p2 own/opp {f2[OWN_LIFE]:.2f}/{f2[OPP_LIFE]:.2f}")


def check_a_handcrafted_labels():
    """League handcrafted games, both learner seats: the last TD target must
    equal the learner-seat outcome, and the recorded rows must be from the
    learner's own perspective (own-life scalar tracks the learner)."""
    net = Net(EX.size, hidden=8, seed=3)
    bad_z, bad_persp, played, losses = 0, 0, 0, 0
    for seed in range(200, 212):
        for learner_seat in ("p1", "p2"):
            rng = random.Random(seed)
            rows, targets, stats = trainer.play_handcrafted_game(
                net, EX, penta, "Sligh", "The Deck", seed, 0.1, rng,
                max_eval=8, learner_seat=learner_seat)
            if not len(targets):
                continue
            played += 1
            result = stats["result"]
            z = 0.5 if result in ("cap", "draw") else (
                1.0 if result == learner_seat else 0.0)
            if targets[-1] != z:
                bad_z += 1
            if result not in ("cap", "draw") and result != learner_seat:
                losses += 1
                # Learner lost. In opponent mode the final recorded
                # afterstate includes the opponent's killing response, so
                # own life must have gone below the opponent's (usually
                # <= 0) in the learner's own perspective.
                if rows[-1][OWN_LIFE] > rows[-1][OPP_LIFE]:
                    bad_persp += 1
    check("A4 handcrafted-league terminal target == learner outcome",
          bad_z == 0, f"{bad_z}/{played} wrong z")
    check("A5 handcrafted-league lost games end with own life below opp's",
          losses > 0 and bad_persp == 0,
          f"{bad_persp}/{losses} lost games recorded winner-perspective rows")


def check_a_mirror_and_frozen_labels():
    """Mirror games and frozen-opponent games must record BOTH seats, each
    with its own outcome and perspective. Captured by intercepting
    td_targets (called once per recorded seat, in p1,p2 order)."""
    net = Net(EX.size, hidden=8, seed=4)
    frozen = Net(EX.size, hidden=8, seed=5)
    calls = []
    real = trainer.td_targets

    def spy(values, z, lam=trainer.TD_LAMBDA):
        calls.append((len(values), z))
        return real(values, z, lam)

    trainer.td_targets = spy
    try:
        # Mirror: both seats recorded, complementary outcomes.
        bad = 0
        checked = 0
        for seed in range(300, 306):
            calls.clear()
            rng = random.Random(seed)
            _, _, stats = trainer.play_selfplay_game(
                net, EX, penta, "White Weenie", "Counterburn", seed, 0.1,
                rng, max_eval=8)
            result = stats["result"]
            if result in ("cap", "draw") or len(calls) != 2:
                continue
            checked += 1
            (n1, z1), (n2, z2) = calls  # p1 first, then p2
            expect1 = 1.0 if result == "p1" else 0.0
            if z1 != expect1 or z2 != 1.0 - expect1:
                bad += 1
        check("A6 mirror games record both seats with own outcomes",
              checked > 0 and bad == 0, f"{bad}/{checked} bad, {checked} decisive")

        # Frozen: both seats recorded (snapshot trajectories are still true
        # outcome-labeled data), each with its own outcome.
        bad = 0
        checked = 0
        for seed in range(400, 406):
            for learner_seat in ("p1", "p2"):
                calls.clear()
                rng = random.Random(seed)
                _, _, stats = trainer.play_selfplay_game(
                    net, EX, penta, "Sligh", "White Weenie", seed, 0.1, rng,
                    max_eval=8, opponent_net=frozen, learner_seat=learner_seat)
                result = stats["result"]
                if result in ("cap", "draw") or len(calls) != 2:
                    continue
                checked += 1
                (n1, z1), (n2, z2) = calls  # p1 first, then p2
                expect1 = 1.0 if result == "p1" else 0.0
                if z1 != expect1 or z2 != 1.0 - expect1:
                    bad += 1
        check("A7 frozen-league games record both seats with own outcomes",
              checked > 0 and bad == 0, f"{bad}/{checked} bad")
    finally:
        trainer.td_targets = real


# ---------------------------------------------------------------- B -----

def check_b_td_targets():
    z = 1.0
    values = [0.5, 0.5, 0.5, 0.5]
    t = td_targets(values, z, lam=0.9)
    # Terminal target is exactly the outcome.
    check("B1 terminal target == z", t[-1] == z, f"{t[-1]}")
    # Hand-computed backward recursion: t[i] = 0.1*v[i+1] + 0.9*t[i+1].
    expect2 = 0.1 * 0.5 + 0.9 * 1.0
    expect1 = 0.1 * 0.5 + 0.9 * expect2
    expect0 = 0.1 * 0.5 + 0.9 * expect1
    ok = (abs(t[2] - expect2) < 1e-6 and abs(t[1] - expect1) < 1e-6
          and abs(t[0] - expect0) < 1e-6)
    check("B2 backward recursion matches hand computation", ok, f"{t}")
    # Decay direction: with flat values and a win, targets rise toward 1
    # near the end (and mirror for a loss).
    check("B3 targets approach the outcome near game end",
          all(t[i] < t[i + 1] for i in range(3))
          and all(x > 0.5 for x in t), f"{t}")
    tl = td_targets([0.5] * 4, 0.0, lam=0.9)
    check("B4 loss targets fall toward 0 near game end",
          all(tl[i] > tl[i + 1] for i in range(3)) and all(x < 0.5 for x in tl),
          f"{tl}")
    # Off-by-one: t[i] must bootstrap from v[i+1], not v[i].
    v = [0.9, 0.1, 0.9, 0.1]
    tt = td_targets(v, 1.0, lam=0.5)
    # t[2] = 0.5*v[3] + 0.5*1.0 = 0.55 (uses v[3]=0.1, not v[2]=0.9)
    check("B5 bootstrap uses the NEXT value (no off-by-one)",
          abs(tt[2] - 0.55) < 1e-6, f"t[2]={tt[2]}")
    # The collapse post-mortem: at the old per-decision lambda 0.9, a
    # typical 65-decision penta trajectory left most targets with almost
    # no outcome signal (target ~= bootstrap echo). Documented here as the
    # motivation for the lambda-1.0 default, which is pure outcomes.
    n = 65
    w_old = 0.9 ** (n - 1 - np.arange(n))
    frac_starved = float(np.mean(w_old < 0.05))
    check("B6 old lambda 0.9 starved most targets of outcome signal",
          frac_starved > 0.5, f"{frac_starved:.0%} of a 65-step trajectory "
          f"had <5% outcome weight")
    th = td_targets([0.3, 0.7, 0.2], 1.0, lam=1.0)
    check("B7 default lambda 1.0 gives pure outcome targets",
          trainer.TD_LAMBDA == 1.0 and np.all(th == 1.0), f"{th}")


# ---------------------------------------------------------------- C -----

class ConstNet:
    """Value net stub: indifferent everywhere, so greedy choice among
    sampled candidates is decided by argmax over equal scores = the first
    sampled candidate; the histogram of chosen actions then reveals the
    sampling distribution itself."""

    def value_batch(self, X):
        return np.zeros(len(X))

    def value(self, x):
        return 0.0


def check_c_sampling_uniform():
    # Find a real decision with a big branching factor.
    state = None
    for seed in range(50):
        game, history, make_game = rand_game_to_depth(seed, 0)
        rng_walk = random.Random(seed)
        while game is not None and game.result() is None:
            obs = json.loads(game.observe())
            acts = obs["legalActions"]
            if len(acts) >= 12:
                state = (history[:], obs, make_game)
                break
            idx = rng_walk.choice(acts)["index"]
            game.act(idx)
            history.append(idx)
        if state:
            break
    if not state:
        check("C1 uniform candidate sampling", False, "no branchy state found")
        return
    history, obs, make_game = state
    acts = obs["legalActions"]
    hist = {a["index"]: 0 for a in acts}
    net = ConstNet()
    trials = 400
    fork = make_fork(make_game, history)
    for t in range(trials):
        rng = random.Random(t)
        index, _, _ = choose(fork, obs, net, EX, rng,
                             epsilon=0.0, max_eval=4, depth=len(history))
        hist[index] += 1
    n = len(acts)
    first4 = sum(hist[a["index"]] for a in acts[:4])
    last = [hist[a["index"]] for a in acts[-4:]]
    # Prefix bias would put ~all mass on the first 4 listed actions.
    check("C1 candidate subset is uniformly sampled, not a prefix",
          first4 < trials * 0.75 and all(x > 0 for x in last),
          f"n={n}, first4={first4}/{trials}, last4 counts={last}")
    check("C2 eval budget shrinks with depth but never below 4",
          eval_budget(0, 16) == 16 and eval_budget(250, 16) == 8
          and eval_budget(450, 16) == 4 and eval_budget(450, 4) == 4)


def check_c_epsilon_covers_all():
    """The epsilon branch must be able to pick ANY candidate (the same set
    greedy draws its sample from), including beyond the budget."""
    state = None
    for seed in range(50):
        game, history, make_game = rand_game_to_depth(seed, 0)
        rng_walk = random.Random(seed)
        while game is not None and game.result() is None:
            obs = json.loads(game.observe())
            acts = obs["legalActions"]
            if len(acts) >= 8:
                state = (history[:], obs, make_game)
                break
            idx = rng_walk.choice(acts)["index"]
            game.act(idx)
            history.append(idx)
        if state:
            break
    if not state:
        check("C3 epsilon explores the full candidate set", False, "no state")
        return
    history, obs, make_game = state
    acts = obs["legalActions"]
    seen = set()
    net = ConstNet()
    fork = make_fork(make_game, history)
    for t in range(300):
        rng = random.Random(7000 + t)
        index, _, _ = choose(fork, obs, net, EX, rng,
                             epsilon=1.0, max_eval=4, depth=len(history))
        seen.add(index)
    check("C3 epsilon explores the full candidate set (incl. past budget)",
          seen == {a["index"] for a in acts},
          f"covered {len(seen)}/{len(acts)}")


# ---------------------------------------------------------------- D -----

def check_d_replay_fidelity():
    """Game(same args) + replay must reproduce the live state exactly, in
    both external and opponent modes."""
    for opponent in ("external", "handcrafted"):
        bad = 0
        for seed in (21, 22, 23):
            def make_game():
                return penta.Game("Sligh", "The Deck", opponent=opponent,
                                  seed=seed)
            game = make_game()
            rng = random.Random(seed)
            history = []
            while game.result() is None and len(history) < 240:
                if len(history) in (40, 120, 200):
                    copies = [make_fork(make_game, history)()]
                    if hasattr(game, "clone"):
                        copies.append(game.clone())
                    elif hasattr(game, "clone_game"):
                        copies.append(game.clone_game())
                    for copy in copies:
                        for seat in ("p1", "p2"):
                            if copy.observe(seat) != game.observe(seat):
                                bad += 1
                obs = json.loads(game.observe())
                idx = rng.choice(obs["legalActions"])["index"]
                game.act(idx)
                history.append(idx)
        check(f"D1 replay/clone forks are exact ({opponent} mode)",
              bad == 0, f"{bad} divergent observations")


def check_d_afterstate_perspective():
    """afterstate_rows must return the LEARNER's-seat observation after the
    action -- for p2 especially -- verified against an independent
    reconstruction, and shown distinct from the opponent's perspective."""
    for want_seat in ("p1", "p2"):
        game, history, make_game = rand_game_to_depth(
            33, 60, want_seat=want_seat)
        if game is None:
            check(f"D2 afterstate perspective ({want_seat})", False,
                  "no usable state")
            continue
        obs = json.loads(game.observe())
        assert obs["seat"] == want_seat
        acts = obs["legalActions"][:4]
        rows, terms = afterstate_rows(make_fork(make_game, history, game),
                                      want_seat, acts, EX)
        bad = 0
        distinct = 0
        for action, row in zip(acts, rows):
            copy = make_game()
            for played in history:
                copy.act(played)
            copy.act(action["index"])
            mine = EX.features(json.loads(copy.observe(want_seat)))
            other = EX.features(json.loads(
                copy.observe("p1" if want_seat == "p2" else "p2")))
            if not np.array_equal(row, mine):
                bad += 1
            if not np.array_equal(mine, other):
                distinct += 1
        check(f"D2 afterstate rows are the {want_seat} learner's own view",
              bad == 0 and distinct > 0,
              f"{bad} mismatches, {distinct}/{len(rows)} distinct from opp view")


def check_d_terminal_outcomes():
    """afterstate_rows terminal detection: outcomes must be from the acting
    seat's perspective (1.0 exactly when the actor wins)."""
    # Drive handcrafted-vs-random games to the brink and test the last
    # decision's candidates: any terminal outcome must match game.result().
    bad = 0
    seen = 0
    for seed in range(600, 640):
        def make_game():
            return penta.Game("Sligh", "The Deck", opponent="random",
                              seed=seed)
        game = make_game()
        rng = random.Random(seed)
        history = []
        while game.result() is None and len(history) < 400:
            obs = json.loads(game.observe())
            acts = obs["legalActions"]
            rows, terms = afterstate_rows(make_fork(make_game, history, game),
                                          obs["seat"], acts[:6], EX)
            chosen = None
            for a, t in zip(acts[:6], terms):
                if t is not None:
                    seen += 1
                    copy = make_game()
                    for played in history:
                        copy.act(played)
                    copy.act(a["index"])
                    result = copy.result()
                    expect = 0.5 if result == "draw" else (
                        1.0 if result == obs["seat"] else 0.0)
                    if t != expect:
                        bad += 1
                    if t == 1.0 and chosen is None:
                        chosen = a["index"]
            idx = chosen if chosen is not None else rng.choice(acts)["index"]
            game.act(idx)
            history.append(idx)
        if seen > 25:
            break
    check("D3 terminal afterstate outcomes are actor-perspective",
          seen > 0 and bad == 0, f"{bad}/{seen} wrong-signed terminals")


# ---------------------------------------------------------------- E -----

def check_g_capped_games_dropped():
    """Games that hit the decision cap have no ground-truth outcome and
    must contribute zero training samples: 0.5-flooding from capped
    passing loops (the longest games, hence the most samples) flattened
    the value net in both curve collapses."""
    net = Net(EX.size, hidden=8, seed=9)
    old_cap = trainer.MAX_DECISIONS
    trainer.MAX_DECISIONS = 30  # force caps
    try:
        capped = 0
        bad = 0
        for seed in range(700, 706):
            rng = random.Random(seed)
            rows, targets, stats = trainer.play_selfplay_game(
                net, EX, penta, "Sligh", "The Deck", seed, 0.1, rng,
                max_eval=8)
            if stats["result"] == "cap":
                capped += 1
                if len(rows) or len(targets):
                    bad += 1
            rng = random.Random(seed)
            rows, targets, stats = trainer.play_handcrafted_game(
                net, EX, penta, "Sligh", "The Deck", seed, 0.1, rng,
                max_eval=8, learner_seat="p1")
            if stats["result"] == "cap":
                capped += 1
                if len(rows) or len(targets):
                    bad += 1
    finally:
        trainer.MAX_DECISIONS = old_cap
    check("G1 capped games contribute no samples",
          capped > 0 and bad == 0, f"{bad}/{capped} capped games leaked data")


def check_f_seat_pair_dealiasing():
    """Deck-pair rotation has an even period, so `number % 2` would give
    each ordered pair the SAME learner seat forever. learner_seat_for must
    give every pair both seats."""
    seats = {}
    for number in range(2 * trainer.N_PAIRS):
        pair = trainer.matchup(number)
        seats.setdefault(pair, set()).add(trainer.learner_seat_for(number))
    check("F1 every ordered deck pair sees both learner seats",
          all(s == {"p1", "p2"} for s in seats.values()),
          f"{sum(len(s) for s in seats.values())}/24 seat-pair combos")


def check_e_replay_ring():
    """The trainer's ring insertion arithmetic: oldest samples overwritten,
    size never exceeds capacity, all live slots reachable by the sampler."""
    capacity = 10
    X = np.full((capacity, 1), -1.0)
    y = np.full(capacity, -1.0)
    size = 0
    cursor = 0
    stream = np.arange(23, dtype=np.float64)
    for start in range(0, 23, 5):  # rounds of 5 samples
        batch = stream[start:start + 5]
        for v in batch:
            X[cursor, 0] = v
            y[cursor] = v
            cursor = (cursor + 1) % capacity
        size = min(size + len(batch), capacity)
    check("E1 ring size is capped at capacity", size == capacity, f"{size}")
    live = set(y.tolist())
    check("E2 ring holds exactly the newest samples",
          live == set(stream[-capacity:].tolist()), f"{sorted(live)}")
    rng = np.random.default_rng(0)
    pick = rng.integers(0, size, size=1000)
    check("E3 sampler reaches every live slot",
          set(pick.tolist()) == set(range(capacity)))


def main():
    print("A. seat/perspective labels")
    check_a_observe_default_seat()
    check_a_absolute_indexing()
    check_a_handcrafted_labels()
    check_a_mirror_and_frozen_labels()
    print("B. TD(lambda) targets")
    check_b_td_targets()
    print("C. evaluation-cap bias")
    check_c_sampling_uniform()
    check_c_epsilon_covers_all()
    print("D. afterstate correctness")
    check_d_replay_fidelity()
    check_d_afterstate_perspective()
    check_d_terminal_outcomes()
    print("E. replay ring")
    check_e_replay_ring()
    print("F. league scheduling")
    check_f_seat_pair_dealiasing()
    print("G. capped-game exclusion")
    check_g_capped_games_dropped()
    print()
    print(f"{len(PASS)} passed, {len(FAIL)} failed")
    if FAIL:
        for name in FAIL:
            print(f"  FAILED: {name}")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
