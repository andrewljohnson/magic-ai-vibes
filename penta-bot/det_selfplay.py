"""Determinized self-play: the training loop with NO true-state clones.

The determinized era's core (engine 0.7.0 / protocol 22, selected via
PENTA_ENGINE_DIR): every seat's search runs on K hypothesis worlds
built with `Game.from_observation` from that seat's REDACTED
observation -- opponent hand/library sampled from the unseen pool of
their (known, both built-in) deck, own library from the own list minus
seen zones -- exactly the machinery the hosted bot plays with. The
information leak the old trainer carried (true-state clones showing
both libraries and the opponent's hand to the playouts) is gone at the
source: the real game object is only ever used to act the chosen index
and to read redacted observations.

Per greedy decision: sample K worlds, run the full certified search
(trainer.choose: policy-head blend, dominance prunes, playouts) inside
each, act on the MAJORITY VOTE (ties -> higher mean world score; no
world survives -> the aggression-prior draw). Recorded features are
the redacted afterstate observation of the real game after acting --
the same view the old trainer recorded, never containing hidden
information. Targets, capping, rings, league mechanics, and TD
handling are trainer.py's, unchanged.
"""

import json
import random

import numpy as np

from hosted_policy import load_decklists, sample_hidden
import trainer
from trainer import (MAX_DECISIONS, aggression_prior, td_targets)

DET_FALLBACK_PRIOR = 0.5  # prior share when no world survives


def det_choose(penta, decks, net, extractor, obs, raw, my_deck, opp_deck,
               k, rng, search, max_eval=16):
    """Consensus index over K hypothesis worlds; None when no world
    survived (caller falls back to an exploration-style draw)."""
    votes = {}
    scores = {}
    for _ in range(k):
        hidden = sample_hidden(obs, rng, decks, my_deck, opp_deck)
        if hidden is None:
            continue
        try:
            world = penta.Game.from_observation(
                raw, json.dumps(hidden), rng.randrange(2**31))
        except Exception:
            continue  # fail closed per world
        try:
            index, _, value = trainer.choose(
                world.clone, obs, net, extractor, rng, epsilon=0.0,
                max_eval=max_eval, depth=0, search=search)
        except ValueError:
            continue  # engine fault / screen panic inside this world
        votes[index] = votes.get(index, 0) + 1
        scores.setdefault(index, []).append(
            value if value is not None else 0.5)
    if not votes:
        return None, None
    top = max(votes.values())
    tied = [i for i, v in votes.items() if v == top]
    best = max(tied, key=lambda i: sum(scores[i]) / len(scores[i]))
    return best, sum(scores[best]) / len(scores[best])


def _record_afterstate(game, seat, extractor):
    """The redacted afterstate view -- what the old trainer recorded,
    reproduced without any fork."""
    return extractor.features(json.loads(game.observe(seat)))


def _epsilon_draw(obs, extractor, rng, prior_frac):
    if rng.random() < prior_frac:
        return aggression_prior(obs["legalActions"], extractor, rng,
                                obs=obs)
    return rng.choice(obs["legalActions"])


def _decide(penta, decks, net, extractor, obs, raw, my_deck, opp_deck,
            k, rng, search, epsilon, prior_frac):
    """One seat's determinized decision. Returns (index, record_flag,
    value_hint); value_hint None means 'evaluate the afterstate'."""
    actions = obs["legalActions"]
    if len(actions) == 1:
        return actions[0]["index"], False, None
    if epsilon > 0.0 and rng.random() < epsilon:
        return _epsilon_draw(obs, extractor, rng, prior_frac)["index"], \
            True, None
    index, value = det_choose(penta, decks, net, extractor, obs, raw,
                              my_deck, opp_deck, k, rng, search)
    if index is None:
        return _epsilon_draw(obs, extractor, rng,
                             DET_FALLBACK_PRIOR)["index"], True, None
    return index, True, value


def play_det_selfplay_game(net, extractor, penta, decks, d1, d2, seed,
                           epsilon, rng, k, opponent_net=None,
                           learner_seat=None, lam=trainer.TD_LAMBDA,
                           search=None, prior_frac=trainer.PRIOR_FRACTION,
                           turn_lam=0.0):
    """Mirror / frozen-league determinized game; both seats decide on
    hypothesis worlds. Returns (rows, targets, stats) like trainer's."""
    game = penta.Game(d1, d2, opponent="external", seed=seed)
    traj = {"p1": ([], [], []), "p2": ([], [], [])}
    n = 0
    while game.result() is None and n < MAX_DECISIONS:
        seat = game.decision_seat()
        raw = game.observe()
        obs = json.loads(raw)
        my_deck, opp_deck = (d1, d2) if seat == "p1" else (d2, d1)
        seat_net = net
        seat_eps = epsilon
        if opponent_net is not None and seat != learner_seat:
            seat_net = opponent_net
            seat_eps = 0.0
        index, record, value = _decide(
            penta, decks, seat_net, extractor, obs, raw, my_deck,
            opp_deck, k, rng, search, seat_eps, prior_frac)
        game.act(index)
        n += 1
        if record:
            feats = _record_afterstate(game, seat, extractor)
            traj[seat][0].append(feats)
            traj[seat][1].append(value if value is not None
                                 and seat_net is net
                                 else net.value(feats))
            traj[seat][2].append(
                0 if obs.get("pregame") else obs.get("turn", 0))

    result = game.result()
    stats = {"decisions": n, "result": result or "cap"}
    if result is None:
        return (np.empty((0, extractor.size), dtype=np.float32),
                np.empty(0, dtype=np.float32), stats)
    rows, targets = [], []
    for seat in ("p1", "p2"):
        feats, values, turns = traj[seat]
        if not feats:
            continue
        z = 0.5 if result == "draw" else (1.0 if result == seat else 0.0)
        rows.append(np.stack(feats))
        targets.append(td_targets(values, z, lam, turns=turns,
                                  turn_lam=turn_lam))
    if rows:
        return np.concatenate(rows), np.concatenate(targets), stats
    return (np.empty((0, extractor.size), dtype=np.float32),
            np.empty(0, dtype=np.float32), stats)


def play_det_handcrafted_game(net, extractor, penta, decks, d1, d2, seed,
                              epsilon, rng, k, learner_seat,
                              lam=trainer.TD_LAMBDA, search=None,
                              prior_frac=trainer.PRIOR_FRACTION,
                              turn_lam=0.0):
    """Determinized league game vs the built-in handcrafted bot; only
    the learner's seat is observed and recorded."""
    opponent_seat = "p2" if learner_seat == "p1" else "p1"
    game = penta.Game(d1, d2, opponent="handcrafted",
                      opponent_seat=opponent_seat, seed=seed)
    my_deck, opp_deck = ((d1, d2) if learner_seat == "p1" else (d2, d1))
    feats_list, values, turns = [], [], []
    n = 0
    while game.result() is None and n < MAX_DECISIONS:
        raw = game.observe()
        obs = json.loads(raw)
        index, record, value = _decide(
            penta, decks, net, extractor, obs, raw, my_deck, opp_deck,
            k, rng, search, epsilon, prior_frac)
        game.act(index)
        n += 1
        if record:
            feats = _record_afterstate(game, learner_seat, extractor)
            feats_list.append(feats)
            values.append(value if value is not None
                          else net.value(feats))
            turns.append(0 if obs.get("pregame") else obs.get("turn", 0))

    result = game.result()
    stats = {"decisions": n, "result": result or "cap"}
    if result is None or not feats_list:
        return (np.empty((0, extractor.size), dtype=np.float32),
                np.empty(0, dtype=np.float32), stats)
    z = 0.5 if result == "draw" else (1.0 if result == learner_seat
                                      else 0.0)
    return (np.stack(feats_list),
            td_targets(values, z, lam, turns=turns, turn_lam=turn_lam),
            stats)
