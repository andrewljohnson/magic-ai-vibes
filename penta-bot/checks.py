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
from trainer import (DEFAULT_SEARCH, afterstate_rows, aggression_prior,
                     choose, eval_budget, make_fork, playout_boundary,
                     playout_value, td_targets)

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
                # afterstate usually includes the opponent's killing
                # response, so own life should be below the opponent's in
                # the learner's own perspective. NOT an invariant -- a
                # burn kill from above the opponent's life total (seen:
                # The Deck Fireballing a 9-life learner past its last
                # contested decision, engine 0.5.0 seed 209) is a real
                # loss with own > opp -- but a wrong-perspective bug
                # would flip essentially ALL lost games, so a small
                # minority of exceptions passes.
                if rows[-1][OWN_LIFE] > rows[-1][OPP_LIFE]:
                    bad_persp += 1
    check("A4 handcrafted-league terminal target == learner outcome",
          bad_z == 0, f"{bad_z}/{played} wrong z")
    check("A5 handcrafted-league lost games mostly end with own life below "
          "opp's",
          losses > 0 and bad_persp <= max(1, losses // 5),
          f"{bad_persp}/{losses} lost games recorded winner-perspective rows")


def check_a_mirror_and_frozen_labels():
    """Mirror games and frozen-opponent games must record BOTH seats, each
    with its own outcome and perspective. Captured by intercepting
    td_targets (called once per recorded seat, in p1,p2 order)."""
    net = Net(EX.size, hidden=8, seed=4)
    frozen = Net(EX.size, hidden=8, seed=5)
    calls = []
    real = trainer.td_targets

    def spy(values, z, lam=trainer.TD_LAMBDA, **kwargs):
        calls.append((len(values), z))
        return real(values, z, lam, **kwargs)

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
    # Per-TURN lambda: decay applies only at turn boundaries; decisions
    # within one turn share the decay step (identical targets).
    v = [0.5, 0.6, 0.4, 0.3, 0.7]
    turns = [1, 1, 2, 2, 3]
    tp = td_targets(v, 1.0, turns=turns, turn_lam=0.9)
    # backward: boundary 2->3: 0.1*0.7+0.9*1.0 = 0.97 (both turn-2 rows);
    # boundary 1->2: 0.1*0.4+0.9*0.97 = 0.913 (both turn-1 rows).
    expect = [0.913, 0.913, 0.97, 0.97, 1.0]
    check("B8 per-turn lambda decays at turn boundaries only",
          np.allclose(tp, expect, atol=1e-6), f"{tp}")
    # With one decision per turn it reduces exactly to per-decision lambda.
    v2 = [0.9, 0.1, 0.9, 0.1]
    same = td_targets(v2, 1.0, turns=[1, 2, 3, 4], turn_lam=0.5)
    per_dec = td_targets(v2, 1.0, lam=0.5)
    check("B9 per-turn == per-decision when every turn has one decision",
          np.allclose(same, per_dec, atol=1e-6), f"{same} vs {per_dec}")
    # Outcome weight survives per-turn decay on realistic trajectories: a
    # 65-decision game spans ~10 turns, so the deepest target keeps
    # 0.9^9 ~ 39% outcome weight vs 0.9^64 ~ 0.1% per-decision.
    turns65 = [1 + i // 7 for i in range(65)]  # ~7 decisions/turn, 10 turns
    tp65 = td_targets([0.0] * 65, 1.0, turns=turns65, turn_lam=0.9)
    check("B10 per-turn decay keeps real outcome weight in early targets",
          tp65[0] > 0.3, f"first target {tp65[0]:.3f}")


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
        # dominance=False: this check probes the SAMPLER's distribution;
        # the prune legitimately empties some slots (section I covers it).
        index, _, _ = choose(fork, obs, net, EX, rng,
                             epsilon=0.0, max_eval=4, depth=len(history),
                             dominance=False)
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


def check_f_seat_oversampling():
    """--seat-oversample: the weighted TRAINING schedule gives The Deck
    ~1.8x and Counterburn/Jeskai ~1.4x the plain decks' seat share,
    covers every ordered pair on both learner seats, and never touches
    the uniform gate schedule."""
    n = len(trainer.TRAIN_PAIRS)
    share = {}
    seats = {}
    for number in range(2 * n):
        a, b = trainer.matchup_train(number)
        share[a] = share.get(a, 0) + 1
        share[b] = share.get(b, 0) + 1
        seats.setdefault((a, b), set()).add(
            trainer.learner_seat_for(number, period=n))
    plain = share["Sligh"]
    ratio_deck = share["The Deck"] / plain
    ratio_cb = share["Counterburn"] / plain
    check("F2 oversampled seat shares hit ~1.8x/~1.4x",
          1.6 <= ratio_deck <= 2.0 and 1.25 <= ratio_cb <= 1.55,
          f"The Deck {ratio_deck:.2f}x, Counterburn {ratio_cb:.2f}x, "
          f"schedule {n} pairs")
    check("F3 oversampled schedule covers every pair on both seats",
          len(seats) == trainer.N_PAIRS
          and all(s == {"p1", "p2"} for s in seats.values()),
          f"{len(seats)}/{trainer.N_PAIRS} pairs")
    gate_counts = {}
    for i in range(4 * trainer.N_PAIRS):
        a, _ = trainer.matchup(i)
        gate_counts[a] = gate_counts.get(a, 0) + 1
    check("F4 gate schedule stays uniform",
          len(set(gate_counts.values())) == 1,
          f"{sorted(set(gate_counts.values()))} per-deck gate counts")


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


# ---------------------------------------------------------------- H -----

def check_h_aggression_prior():
    """Exploration prior ordering: land > biggest castable > attack-declare
    > uniform rest (first_bot-shaped)."""
    defs_by_power = sorted(EX.card_power.items(), key=lambda kv: kv[1])
    small_def, small_p = defs_by_power[0]
    big_def, big_p = defs_by_power[-1]
    rng = random.Random(0)
    land = {"index": 0, "type": "PlayLand"}
    cast_small = {"index": 1, "type": "CastSpell", "card": small_def}
    cast_big = {"index": 2, "type": "CastSpell", "card": big_def}
    attack = {"index": 3, "type": "DeclareAttacker"}
    finish = {"index": 4, "type": "FinishDeclaringAttackers"}
    quiet = {"index": 5, "type": "PassPriority"}
    check("H1 prior prefers land over everything",
          all(aggression_prior([quiet, cast_big, attack, land], EX,
                               rng)["index"] == 0 for _ in range(20)))
    check("H2 prior casts the biggest castable spell",
          big_p > small_p and all(
              aggression_prior([quiet, cast_small, cast_big, finish], EX,
                               rng)["index"] == 2 for _ in range(20)),
          f"powers {small_p} vs {big_p}")
    # Protocol v2: CastSpell "card" is a hand INSTANCE id; the prior must
    # resolve it through the observation's hand to find the power.
    obs_v2 = {"hand": [{"definition": small_def, "instance": 501},
                       {"definition": big_def, "instance": 502}]}
    cast_i_small = {"index": 1, "type": "CastSpell", "card": 501}
    cast_i_big = {"index": 2, "type": "CastSpell", "card": 502}
    check("H2b prior resolves v2 instance ids through the hand",
          all(aggression_prior([quiet, cast_i_small, cast_i_big, finish],
                               EX, rng, obs=obs_v2)["index"] == 2
              for _ in range(20)))
    check("H3 prior declares attackers over quiet options",
          all(aggression_prior([finish, quiet, attack], EX, rng)["index"] == 3
              for _ in range(20)))
    seen = {aggression_prior([finish, quiet], EX, rng)["index"]
            for _ in range(200)}
    check("H4 prior falls back to a uniform draw", seen == {4, 5}, f"{seen}")


def check_h_playout_boundary():
    """Boundary predicate: the deciding seat's next turn start."""
    def obs(turn, active, pregame=False):
        return {"turn": turn, "activeSeat": active, "pregame": pregame}
    ok = (not playout_boundary(obs(3, "p1"), "p1", 3, False)  # same turn
          and not playout_boundary(obs(4, "p2"), "p1", 3, False)  # opp turn
          and playout_boundary(obs(5, "p1"), "p1", 3, False)  # next turn
          and not playout_boundary(obs(1, "p1", True), "p1", 1, True)  # mull
          and playout_boundary(obs(1, "p1"), "p1", 1, True)  # first turn
          and not playout_boundary(obs(2, "p2"), "p2", 3, False))  # earlier
    check("H5 playout boundary is the seat's next turn start", ok)


def check_h_playout_and_search():
    """playout_value scores terminals exactly, is deterministic for a
    fixed rng, and stays in [0, 1]; searched choose takes an immediate
    winning terminal candidate when one exists."""
    net = Net(EX.size, hidden=8, seed=11)
    game, history, make_game = rand_game_to_depth(33, 60)
    if game is None:
        check("H6 playout determinism", False, "no usable state")
        return
    obs = json.loads(game.observe())
    seat = obs["seat"]
    action = obs["legalActions"][0]
    vals = []
    for _ in range(2):
        after = game.clone()
        after.act(action["index"])
        vals.append(playout_value(
            after, seat, net, EX, random.Random(5), obs.get("turn", 0),
            False, DEFAULT_SEARCH))
    check("H6 playout value is deterministic and bounded",
          vals[0] == vals[1] and 0.0 <= vals[0] <= 1.0, f"{vals}")

    # A state with a winning terminal candidate: search must take it.
    found = 0
    taken = 0
    for seed in range(600, 660):
        def make_game():
            return penta.Game("Sligh", "The Deck", opponent="random",
                              seed=seed)
        game = make_game()
        rng = random.Random(seed)
        history = []
        while game.result() is None and len(history) < 400:
            obs = json.loads(game.observe())
            acts = obs["legalActions"]
            winning = None
            if len(acts) > 1:
                rows, terms = afterstate_rows(
                    make_fork(make_game, history, game), obs["seat"],
                    acts[:8], EX)
                for a, t in zip(acts[:8], terms):
                    if t == 1.0:
                        winning = a["index"]
                        break
            if winning is not None:
                found += 1
                index, _, value = choose(
                    make_fork(make_game, history, game), obs, net, EX,
                    random.Random(1), epsilon=0.0, max_eval=8,
                    depth=len(history), search=DEFAULT_SEARCH)
                copy = make_fork(make_game, history, game)()
                copy.act(index)
                taken += copy.result() == obs["seat"] and value == 1.0
                break
            idx = rng.choice(acts)["index"]
            game.act(idx)
            history.append(idx)
        if found:
            break
    check("H7 search takes an immediate winning candidate",
          found > 0 and taken == found, f"{taken}/{found}")


# ---------------------------------------------------------------- I -----
#
# No-upside-versus-pass dominance prune. I1-I3 replay the three scouted
# blunder decisions (scout.py corpus, net = the era's deliverable
# ckpt-search-7000) with the prune DISABLED to reach the exact historical
# state, then assert the prune predicate kills the blunder action while
# the legitimate targeted plays in the SAME state survive. I4 plays live
# games with the prune active and audits its invariants.

SCOUT_NET = "penta_net.ckpt-search-7000.npz"


def _cast_targets(action):
    return [t for sel in (action.get("choices") or {}).get(
                "targetSelections", ())
            for t in sel.get("targets", ())]


# The scouted trajectories were recorded before the 2026-08-12 search
# promotion; replaying them requires the era-accurate config (pme8).
SCOUT_ERA_SEARCH = trainer.SearchConfig(
    top_k=4, playouts=1, budget=120, playout_max_eval=8,
    playout_epsilon=0.10)


def _replay_to(net, ex, our_deck, their_deck, my_seat, seed, pred):
    """Re-drive a scout game (greedy, scout-era search, prune off) to
    the first decision where pred(obs) is truthy; returns (obs, fork)."""
    opponent_seat = "p2" if my_seat == "p1" else "p1"
    d1, d2 = ((our_deck, their_deck) if my_seat == "p1"
              else (their_deck, our_deck))

    def make_game():
        return penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=opponent_seat, seed=seed)

    game = make_game()
    rng = random.Random(seed)
    history = []
    while game.result() is None and len(history) < 3000:
        obs = json.loads(game.observe())
        fork = make_fork(make_game, history, game)
        if pred(obs):
            return obs, fork
        index, _, _ = choose(fork, obs, net, ex, rng, epsilon=0.0,
                             max_eval=16, depth=len(history),
                             search=SCOUT_ERA_SEARCH, dominance=False)
        game.act(index)
        history.append(index)
    return None, None


def _prune_case(name, obs, fork, blunder, survivors, ex):
    """Run dominance_prune on the state's full action list and assert the
    blunder is dropped while every legitimate survivor action remains."""
    if obs is None:
        check(name, False, "replay never reached the scouted decision")
        return
    kept = trainer.dominance_prune(fork, obs, obs["legalActions"], ex)
    kept_idx = {a["index"] for a in kept}
    ok = blunder["index"] not in kept_idx
    missing = [a["index"] for a in survivors if a["index"] not in kept_idx]
    check(name, ok and not missing,
          f"pruned {len(obs['legalActions']) - len(kept)}/"
          f"{len(obs['legalActions'])}; blunder "
          f"{'pruned' if ok else 'KEPT'}; "
          f"{len(survivors) - len(missing)}/{len(survivors)} legit kept")


def _drive_simple(make_game, pred, limit=1500):
    """Drive a scripted-opponent game with a fixed, net-free policy (play a
    land, else cast a spell, else declare an attacker, else pass) to the
    first decision where pred(obs) holds. Deterministic and card-agnostic,
    so a blunder decision is REACHED on the current engine without leaning
    on a replay corpus captured against an older era. Returns (obs, fork)
    -- fork clones the game AT the decision -- or (None, None)."""
    game = make_game()
    while game.result() is None and limit > 0:
        limit -= 1
        obs = json.loads(game.observe())
        if pred(obs):
            return obs, make_fork(make_game, [], game)
        by_type = {}
        for a in obs["legalActions"]:
            by_type.setdefault(a["type"], []).append(a)
        for kind in ("PlayLand", "CastSpell", "DeclareAttacker"):
            if by_type.get(kind):
                game.act(by_type[kind][0]["index"])
                break
        else:
            passes = by_type.get("PassPriority") or obs["legalActions"]
            game.act(passes[0]["index"])
    return None, None


def _prune_case_multi(name, obs, fork, blunders, survivors, ex):
    """Like _prune_case but for a decision with several blunder actions
    (e.g. every self-targeting Strip Mine crack): all blunders must be
    dropped, every legitimate survivor kept."""
    if obs is None:
        check(name, False, "driver never reached the decision")
        return
    kept = trainer.dominance_prune(fork, obs, obs["legalActions"], ex)
    kept_idx = {a["index"] for a in kept}
    blunder_kept = [a["index"] for a in blunders if a["index"] in kept_idx]
    missing = [a["index"] for a in survivors if a["index"] not in kept_idx]
    check(name, not blunder_kept and not missing,
          f"pruned {len(obs['legalActions']) - len(kept)}/"
          f"{len(obs['legalActions'])}; "
          f"{len(blunders) - len(blunder_kept)}/{len(blunders)} blunders "
          f"pruned; {len(survivors) - len(missing)}/{len(survivors)} "
          f"legit kept")


def check_i_scouted_blunders():
    import os

    # I1 and I3 are reached by a net-free deterministic driver on the
    # CURRENT engine (0.7.0 / protocol 22): the 0.5.0-captured scout seeds
    # no longer replay to their decisions, and the prune's own guards --
    # not any net -- are what these regressions exercise, so EX (any
    # schema) suffices. I2 still replays its scout seed, which reaches on
    # 0.7.0, so it keeps the scout-net choose-driver.
    _MAIN = ("PrecombatMain", "PostcombatMain")

    def hand_name(obs, action):
        names = {c["instance"]: c["name"] for c in obs.get("hand", ())}
        return names.get(action.get("card"))

    # I1: Ancestral Recall cast AT THE OPPONENT (they draw three) is pure
    # self-sabotage -- strictly dominated by passing. Aiming it at
    # ourselves is the legitimate line and must survive. The Deck (seat
    # p1) vs Sligh, seed 7, reaches turn 3 with both casts enumerated.
    def ancestral_casts(obs):
        out = {"blunder": [], "legit": []}
        for a in obs["legalActions"]:
            if a["type"] != "CastSpell" or \
                    hand_name(obs, a) != "Ancestral Recall":
                continue
            for t in a.get("targets") or ():
                if t.get("type") != "player":
                    continue
                if t.get("seat") == obs["seat"]:
                    out["legit"].append(a)
                else:
                    out["blunder"].append(a)
        return out

    def pred1(obs):
        if obs.get("stack") or obs.get("pregame") \
                or obs.get("activeSeat") != obs["seat"]:
            return False
        casts = ancestral_casts(obs)
        return bool(casts["blunder"]) and bool(casts["legit"])

    obs, fork = _drive_simple(
        lambda: penta.Game("The Deck", "Sligh", opponent="handcrafted",
                           opponent_seat="p2", seed=7), pred1)
    found = ancestral_casts(obs) if obs else {"blunder": [], "legit": []}
    _prune_case_multi("I1 Ancestral Recall at the opponent is pruned "
                      "(cast at self survives)", obs, fork,
                      found["blunder"], found["legit"], EX)

    # I3: Strip Mine cracked on ANY of our OWN permanents (its own land, or
    # our Mountain -- pure card/mana loss) is pruned, while cracking an
    # OPPOSING land survives. Sligh (seat p1) vs The Deck, seed 52, reaches
    # turn 3 with a self-target crack, an opposing-land crack, AND (unlike
    # the old scout seed) a scripted-opponent reaction on the settle -- the
    # exact protocol-22 shape that defeated a static-observation baseline.
    def stripmine_acts(obs):
        perms = {p["instance"]: p for p in obs.get("battlefield", ())}
        out = {"blunder": [], "legit": []}
        for a in obs["legalActions"]:
            if a["type"] != "ActivateAbility":
                continue
            source = perms.get(a.get("source"))
            if source is None or source["name"] != "Strip Mine":
                continue
            target = perms.get((a.get("target") or {}).get("instance"))
            if target is None:
                continue
            if target["controller"] == obs["seat"]:
                out["blunder"].append(a)  # any of OUR permanents
            else:
                out["legit"].append(a)    # an opposing land
        return out

    def pred3(obs):
        if obs.get("stack") or obs.get("pregame") \
                or obs.get("activeSeat") != obs["seat"] \
                or obs.get("step") not in _MAIN:
            return False
        acts = stripmine_acts(obs)
        return bool(acts["blunder"]) and bool(acts["legit"])

    obs3, fork3 = _drive_simple(
        lambda: penta.Game("Sligh", "The Deck", opponent="handcrafted",
                           opponent_seat="p2", seed=52), pred3)
    found3 = stripmine_acts(obs3) if obs3 else {"blunder": [], "legit": []}
    _prune_case_multi("I3 Strip Mine on our own permanent is pruned "
                      "(opposing-land targets survive)", obs3, fork3,
                      found3["blunder"], found3["legit"], EX)

    # I2 still replays its scout seed (reaches on 0.7.0).
    if not os.path.exists(SCOUT_NET):
        check("I2 Fireball at our own creature is pruned "
              "(land/creature plays survive)", False,
              f"{SCOUT_NET} missing")
        return
    net = Net.load(SCOUT_NET)
    ex = Extractor.for_inputs(net.inputs)

    # I2: Fireball aimed at our own Goblin Balloon Brigade (Sligh vs
    # White Weenie, seat p2, seed 9100103, turn 20 PrecombatMain). The
    # engine enumerates these casts at x=0 -- pure card waste either
    # way. Plays with real upside in the SAME state (land drops,
    # creature casts) must survive.
    def fireball_casts(obs):
        mine = {p["instance"]: p["name"] for p in obs.get("battlefield", ())
                if p["controller"] == "p2"}
        hand_defs = {c["instance"]: c["definition"]
                     for c in obs.get("hand", ())}
        out = {"blunder": None, "legit": []}
        for a in obs["legalActions"]:
            if a["type"] == "PlayLand":
                out["legit"].append(a)
            if a["type"] != "CastSpell":
                continue
            if hand_name(obs, a) == "Fireball":
                for t in _cast_targets(a):
                    if mine.get(t.get("instance")) == \
                            "Goblin Balloon Brigade":
                        out["blunder"] = a
            elif EX.card_kind.get(hand_defs.get(a.get("card"))) in (
                    "Creature", "ArtifactCreature"):
                out["legit"].append(a)
        return out

    def pred2(obs):
        if obs.get("turn") != 20 or obs.get("step") != "PrecombatMain" \
                or obs.get("stack"):
            return False
        return fireball_casts(obs)["blunder"] is not None

    obs, fork = _replay_to(net, ex, "Sligh", "White Weenie", "p2",
                           9100103, pred2)
    found = fireball_casts(obs) if obs else {"blunder": None, "legit": []}
    _prune_case("I2 Fireball at our own creature is pruned (land/creature "
                "plays survive)", obs, fork,
                found["blunder"] or {"index": -1}, found["legit"], ex)


SCOUT48_NET = "penta_net.ckpt-v3-48000.npz"


def check_i_land_skip():
    """I5: the land-drop rule on real scout data. Seed 9201713 (The Deck
    seat p2 vs White Weenie, the 48k-net 8-deck sweep) lost with 9
    skipped land drops, dying with three City of Brass in hand. At the
    first main-phase decision of that game offering both a land drop and
    Pass, the prune must remove Pass and keep every PlayLand."""
    import os
    if not os.path.exists(SCOUT48_NET):
        check("I5 pass is pruned when a free land drop is available",
              False, f"{SCOUT48_NET} missing")
        return
    net = Net.load(SCOUT48_NET)
    ex = Extractor.for_inputs(net.inputs)

    def pred(obs):
        if obs.get("stack") or obs.get("pregame"):
            return False
        if obs.get("activeSeat") != "p2" or obs.get("step") not in (
                "PrecombatMain", "PostcombatMain"):
            return False
        types = {a["type"] for a in obs["legalActions"]}
        return "PlayLand" in types and "PassPriority" in types

    obs, fork = _replay_to(net, ex, "The Deck", "White Weenie", "p2",
                           9201713, pred)
    if obs is None:
        check("I5 pass is pruned when a free land drop is available",
              False, "replay found no land+pass main-phase decision")
        return
    kept = trainer.dominance_prune(fork, obs, obs["legalActions"], ex)
    kept_types = [a["type"] for a in kept]
    lands_in = sum(1 for a in obs["legalActions"]
                   if a["type"] == "PlayLand")
    check("I5 pass is pruned when a free land drop is available",
          "PassPriority" not in kept_types
          and kept_types.count("PlayLand") == lands_in,
          f"turn {obs.get('turn')} {obs.get('step')}: "
          f"{len(obs['legalActions'])} -> {len(kept)} actions, "
          f"lands {kept_types.count('PlayLand')}/{lands_in} kept, "
          f"pass {'kept' if 'PassPriority' in kept_types else 'pruned'}")


def check_i_land_rule_units():
    """I6: _land_dominates_pass unit cases, including the fail-closed
    side-effect guards (Ankh-style life hit, opponent change)."""
    def state(**over):
        base = {"turn": 5, "step": "PrecombatMain", "my_hand": 5,
                "my_life": 20, "my_lib": 40, "my_bf": {(9, None, None): 3},
                "my_mana": 0, "opp_hand": 6, "opp_life": 20,
                "opp_bf": {(4, 2, 2): 1}, "opp_gy": {}, "opp_exile": {}}
        base.update(over)
        return base

    base = state()
    grown = {(9, None, None): 4}
    check("I6a pure development dominates pass",
          trainer._land_dominates_pass(
              state(my_hand=4, my_bf=grown), base))
    check("I6b a life hit on the drop fails closed (Ankh guard)",
          not trainer._land_dominates_pass(
              state(my_hand=4, my_bf=grown, my_life=18), base))
    check("I6c an opponent-side change fails closed",
          not trainer._land_dominates_pass(
              state(my_hand=4, my_bf=grown, opp_life=22), base))
    check("I6d no battlefield growth fails closed",
          not trainer._land_dominates_pass(state(my_hand=4), base))
    check("I6e losing another permanent fails closed",
          not trainer._land_dominates_pass(
              state(my_hand=4, my_bf={(9, None, None): 2,
                                      (11, None, None): 1}), base))


def check_j_schema_v3():
    """Schema v3 (deployment block): sizes, prefix nesting, the
    property-derived draw-engine set, and grow-init equivalence (a v2
    net grown with zero columns must value every v3 observation exactly
    as it valued the v2 features)."""
    ex2 = Extractor(version=2)
    ex3 = Extractor(version=3)
    check("J1 schema sizes are nested (675/825/840)",
          ex3.v1_size == 675 and ex3.v2_size == 825 and ex3.size == 840,
          f"{ex3.v1_size}/{ex3.v2_size}/{ex3.size}")
    names = {}
    catalog = json.loads(penta.catalog())
    for card in catalog["cards"]:
        names[card["name"]] = card["definition"]
    engines = {names["Jayemdae Tome"], names["Library of Alexandria"],
               names["Sage of Lat-Nam"]}
    one_shots = {names["Ancestral Recall"], names["Wheel of Fortune"],
                 names["Timetwister"], names["Braingeyser"]}
    check("J2 draw engines are activated abilities, not one-shot draw",
          engines <= ex3.draw_engine_defs
          and not (one_shots & ex3.draw_engine_defs),
          f"{len(ex3.draw_engine_defs)} engine defs")
    # Prefix nesting + grow-init equivalence over live observations.
    net2 = Net(ex2.size, hidden=8, seed=21)
    grown = Net(ex2.size, hidden=8, seed=21)
    pad = np.zeros((grown.w1.shape[0], ex3.size - ex2.size))
    grown.w1 = np.hstack([grown.w1, pad])
    game, history, make_game = rand_game_to_depth(19, 40)
    if game is None:
        check("J3 v3 prefix + grow-init equivalence", False, "no state")
        return
    obs = json.loads(game.observe())
    v2 = ex2.features(obs)
    v3 = ex3.features(obs)
    prefix_ok = np.array_equal(v2, v3[:ex2.size])
    val_ok = abs(net2.value(v2) - grown.value(v3)) < 1e-12
    block = v3[ex2.size:]
    check("J3 v3 prefix + grow-init equivalence",
          prefix_ok and val_ok and len(block) == 15,
          f"prefix {prefix_ok}, value delta "
          f"{abs(net2.value(v2) - grown.value(v3)):.2e}, "
          f"block nonzero {int((block != 0).sum())}/15")


def check_i_prune_invariants():
    """Live games with the prune active: the filtered list is always a
    non-empty subset keeping at least one non-pass action, Pass is only
    ever removed by the land-drop rule (a PlayLand stays available),
    and games still complete."""
    net = Net(EX.size, hidden=8, seed=13)
    stats_log = []
    violations = []
    real = trainer.dominance_prune

    def spy(fork, obs, actions, ex):
        kept = real(fork, obs, actions, ex)
        stats_log.append((len(actions), len(kept)))
        kept_idx = {a["index"] for a in kept}
        all_idx = {a["index"] for a in actions}
        if not kept or not kept_idx <= all_idx:
            violations.append("not a subset / empty")
        had_pass = any(a["type"] == "PassPriority" for a in actions)
        has_pass = any(a["type"] == "PassPriority" for a in kept)
        if had_pass and not has_pass and not any(
                a["type"] == "PlayLand" for a in kept):
            violations.append("pass removed without a land drop kept")
        if len(kept) < len(actions) and all(
                a["type"] == "PassPriority" for a in kept):
            violations.append("all non-pass actions pruned")
        return kept

    trainer.dominance_prune = spy
    try:
        completed = 0
        for seed in (700, 701, 702, 703):
            rng = random.Random(seed)
            _, _, stats = trainer.play_selfplay_game(
                net, EX, penta, "Sligh", "The Deck", seed, 0.1, rng,
                max_eval=8)
            completed += stats["result"] in ("p1", "p2", "draw", "cap")
    finally:
        trainer.dominance_prune = real
    pruned_calls = sum(1 for n_in, n_out in stats_log if n_out < n_in)
    check("I4 prune invariants hold in live games",
          completed == 4 and stats_log and not violations,
          f"{completed}/4 games, {len(stats_log)} prune calls, "
          f"{pruned_calls} pruned decisions, violations={violations[:3]}")


def check_k_deploy_aux():
    """Counterfactual deploy-axis collection: aux samples appear only
    when a cast was declined at a greedy decision, carry the deciding
    seat's outcome, and vanish when disabled or capped."""
    net = Net(EX.size, hidden=8, seed=17)
    got_aux = 0
    bad = 0
    decisive = 0
    for seed in (810, 811, 812):
        rng = random.Random(seed)
        _, _, stats = trainer.play_selfplay_game(
            net, EX, penta, "White Weenie", "Mono Black", seed, 0.05, rng,
            max_eval=8, collect_aux=True)
        if stats["result"] == "cap":
            if "aux_X" in stats:
                bad += 1  # capped games must drop aux with everything
            continue
        decisive += 1
        if "aux_X" in stats:
            got_aux += len(stats["aux_y"])
            if stats["aux_X"].shape[1] != EX.size or not all(
                    y in (0.0, 0.5, 1.0) for y in stats["aux_y"]):
                bad += 1
        rng2 = random.Random(seed)
        _, _, stats_off = trainer.play_selfplay_game(
            net, EX, penta, "White Weenie", "Mono Black", seed, 0.05, rng2,
            max_eval=8, collect_aux=False)
        if "aux_X" in stats_off:
            bad += 1
    check("K1 deploy-aux samples are outcome-labeled declined casts",
          (decisive == 0 or got_aux > 0) and bad == 0,
          f"{got_aux} aux samples across {decisive} decisive games, "
          f"{bad} violations")


def check_l_panic_armor():
    """Engine-panic armor: a pyo3 PanicException from a CLONE act inside
    a playout truncates the playout (net-scored, no raise); the settle
    path fails closed; a screen-clone panic surfaces as the existing
    dropped-game ValueError; and NON-panic BaseExceptions are never
    swallowed. Detection is by class identity (name + claimed module)
    because pyo3 never registers a real pyo3_runtime module, so the
    stand-in class replicates exactly that shape."""
    panic_cls = type("PanicException", (BaseException,),
                     {"__module__": "pyo3_runtime"})
    game, history, make_game = rand_game_to_depth(41, 30)
    if game is None:
        check("L1 panic armor truncates playouts", False, "no state")
        return
    obs_json = game.observe()
    obs = json.loads(obs_json)

    class PanickyClone:
        """Serves the real observation; panics on every act."""
        def __init__(self, exc):
            self.exc = exc

        def result(self):
            return None

        def observe(self, seat=None):
            return obs_json

        def act(self, index):
            raise self.exc("test panic: mana activation plan")

        def clone(self):
            return self

    net = Net(EX.size, hidden=8, seed=23)
    rng = random.Random(3)
    value = playout_value(PanickyClone(panic_cls), obs["seat"], net, EX,
                          rng, obs.get("turn", 0), False, DEFAULT_SEARCH)
    check("L1 panic armor truncates playouts with a net score",
          isinstance(value, float) and 0.0 <= value <= 1.0,
          f"value {value:.3f}, no raise")
    settle = trainer._settle_all_pass(PanickyClone(panic_cls))
    expect = not json.loads(obs_json).get("stack")
    check("L2 panicked settle clone fails closed",
          settle is expect,
          f"settle {settle} (stack "
          f"{'empty' if expect else 'pending'}), no raise")

    try:
        choose(lambda: PanickyClone(panic_cls), obs, net, EX, rng,
               epsilon=0.0, max_eval=8, depth=len(history))
        screen_ok = False
        detail = "no exception raised"
    except ValueError as error:
        screen_ok = "engine panic" in str(error)
        detail = str(error)[:60]
    except BaseException as error:
        screen_ok = False
        detail = f"leaked {type(error).__name__}"
    check("L3 screen-clone panic surfaces as the dropped-game "
          "ValueError", screen_ok, detail)

    # Anything that is NOT the engine panic must pass straight through.
    other = type("KeyboardInterrupt", (BaseException,), {})
    try:
        playout_value(PanickyClone(other), obs["seat"], net, EX,
                      rng, obs.get("turn", 0), False, DEFAULT_SEARCH)
        passed_through = False
    except BaseException as error:
        passed_through = type(error) is other
    check("L4 non-panic BaseExceptions are never swallowed",
          passed_through)


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
    check_f_seat_oversampling()
    print("G. capped-game exclusion")
    check_g_capped_games_dropped()
    print("H. rollout search + aggression prior")
    check_h_aggression_prior()
    check_h_playout_boundary()
    check_h_playout_and_search()
    print("I. dominance prune (no upside versus pass)")
    check_i_scouted_blunders()
    check_i_land_skip()
    check_i_land_rule_units()
    check_i_prune_invariants()
    print("J. schema v3 (deployment block)")
    check_j_schema_v3()
    print("K. deploy-axis aux loss")
    check_k_deploy_aux()
    print("L. engine-panic armor")
    check_l_panic_armor()
    print()
    print(f"{len(PASS)} passed, {len(FAIL)} failed")
    if FAIL:
        for name in FAIL:
            print(f"  FAILED: {name}")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
