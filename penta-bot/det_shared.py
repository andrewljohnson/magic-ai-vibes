"""Python reference mirror of the native determinized runner
(spz-core/src/det_runner.rs) using the SHARED splitmix64 PRNG, for
row-lockstep. Same world sampling, same greedy consensus (via the
reference trainer.choose, already bit-identical to policy.choose), same
row emission (redacted afterstate features, TD lambda=1.0 -> z). If
native stream_rows and this agree bit-for-bit on identical seeds, the
native runner is proven for training.
"""
import json
import random

import numpy as np

import trainer
from hosted_policy import load_decklists

MASK = (1 << 64) - 1


class SplitMix64:
    def __init__(self, seed):
        self.s = seed & MASK

    def next_u64(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & MASK
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
        return z ^ (z >> 31)

    def below(self, n):
        return self.next_u64() % n

    def next_f64(self):
        return (self.next_u64() >> 11) / 9007199254740992.0

    def shuffle(self, v):
        for i in range(len(v) - 1, 0, -1):
            j = self.below(i + 1)
            v[i], v[j] = v[j], v[i]


def _seen(obs, seat, si, include_hand):
    out = []
    if include_hand:
        out += [c["definition"] for c in obs.get("hand", ())]
    out += [p["definition"] for p in obs.get("battlefield", ())
            if p["controller"] == seat]
    for zone in ("graveyards", "exiles"):
        piles = obs.get(zone) or ((), ())
        if len(piles) > si:
            out += [c["definition"] for c in piles[si]]
    return out


def _pool(decks, name, seen):
    pool = []
    for d in sorted(decks.get(name, {})):
        pool += [d] * decks[name][d]
    for d in seen:
        if d in pool:
            pool.remove(d)
    return pool


def sample_hidden(obs, prng, decks, my_deck, opp_deck):
    me = obs["seat"]
    opp = "p2" if me == "p1" else "p1"
    mi, oi = (0, 1) if me == "p1" else (1, 0)
    my_pool = _pool(decks, my_deck, _seen(obs, me, mi, True))
    opp_pool = _pool(decks, opp_deck, _seen(obs, opp, oi, False))
    prng.shuffle(my_pool)
    prng.shuffle(opp_pool)
    need = obs.get("opponentHandSize", 0) + obs["librarySizes"][oi]
    if len(my_pool) < obs["librarySizes"][mi] or len(opp_pool) < need:
        return None
    oh = obs.get("opponentHandSize", 0)
    opp_hand = opp_pool[:oh]
    opp_lib = opp_pool[oh:oh + obs["librarySizes"][oi]]
    return {"hands": {opp: opp_hand},
            "libraries": {me: my_pool[:obs["librarySizes"][mi]],
                          opp: opp_lib},
            "outsideGame": {"p1": [], "p2": []}}


def det_choose(penta, real, seat, net, ex, decks, my_deck, opp_deck, k,
               prng, search):
    raw = real.observe(seat)
    obs = json.loads(raw)
    votes, sval = {}, {}
    for _ in range(k):
        hidden = sample_hidden(obs, prng, decks, my_deck, opp_deck)
        if hidden is None:
            continue
        rollout = prng.next_u64()
        try:
            world = penta.Game.from_observation(raw, json.dumps(hidden),
                                                rollout)
        except Exception:
            continue
        wobs = json.loads(world.observe())
        idx, _, val = trainer.choose(world.clone, wobs, net, ex,
                                     random.Random(0), 0.0, 16, depth=0,
                                     search=search)
        votes[idx] = votes.get(idx, 0) + 1
        sval[idx] = sval.get(idx, 0.0) + (val if val is not None else 0.5)
    if not votes:
        return None
    top = max(votes.values())
    tied = sorted(i for i, v in votes.items() if v == top)
    return max(tied, key=lambda i: (sval[i] / votes[i], -i))


def _explore_pick(obs, acts, ex, prior_frac, prng):
    v = prng.next_f64()
    if v < prior_frac:
        lands = [i for i, a in enumerate(acts) if a["type"] == "PlayLand"]
        if lands:
            return lands[prng.below(len(lands))]
        hand_def = {c.get("objectId", c.get("instance")): c["definition"]
                    for c in obs.get("hand", ())}
        casts = [i for i, a in enumerate(acts) if a["type"] == "CastSpell"]
        if casts:
            best, bestp = casts[0], -10**9
            for i in casts:
                d = hand_def.get(acts[i].get("card"))
                p = ex.card_power.get(d, 0) if d is not None else 0
                if p > bestp:
                    bestp, best = p, i
            return best
        atk = [i for i, a in enumerate(acts) if a["type"] == "DeclareAttacker"]
        if atk:
            return atk[prng.below(len(atk))]
    return prng.below(len(acts))


def play_game(penta, net, ex, decks, d1, d2, seed, k, handcrafted,
              learner, search, epsilon=0.0, prior_frac=0.5):
    opp_seat = "p2" if learner == "p1" else "p1"
    if handcrafted:
        game = penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=opp_seat, seed=seed)
    else:
        game = penta.Game(d1, d2, opponent="external", seed=seed)
    prng = SplitMix64(((seed * 0x9E3779B97F4A7C15) & MASK) ^ 0xD1B5)
    rows = {"p1": [], "p2": []}
    n = 0
    while game.result() is None and n < trainer.MAX_DECISIONS:
        seat = game.decision_seat()
        obs = json.loads(game.observe(seat))
        acts = obs["legalActions"]
        if len(acts) == 1:
            game.act(acts[0]["index"]); n += 1; continue
        my_deck, opp_deck = (d1, d2) if seat == "p1" else (d2, d1)
        idx = det_choose(penta, game, seat, net, ex, decks, my_deck,
                         opp_deck, k, prng, search)
        if idx is None:
            idx = acts[0]["index"]
        game.act(idx); n += 1
        if not handcrafted or seat == learner:
            after = json.loads(game.observe(seat))
            rows[seat].append(ex.features(after))
    result = game.result()
    if result is None:
        return []  # capped
    out = []
    seats = [learner] if handcrafted else ["p1", "p2"]
    for s in seats:
        z = 0.5 if result == "draw" else (1.0 if result == s else 0.0)
        for r in rows[s]:
            out.append((r, z))
    return out
