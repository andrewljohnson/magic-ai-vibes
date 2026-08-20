"""Asymmetric Actor-Critic (AAC) self-play PROTOTYPE for the Penta MTG bot.

Principled fix for our diagnosed failures (value-greedy plays passive;
imitating a perfect-info teacher gets WORSE with data). From the
literature (MADDPG / AlphaStar / sim-to-real): train a CRITIC with
PRIVILEGED info (sees BOTH seats' redacted observations = both hands) but
an ACTOR that sees only its OWN honest observation; update the actor by
policy gradient using the privileged critic's advantage. At deployment
ONLY the honest actor is used -- it never copies hidden-info moves.

ACTOR (honest): scores afterstates. At a decision, for each legal action
we clone the game, act, observe(seat) -> redacted 825-dim afterstate
features; actor_net(feats) -> scalar logit score. pi(a) = softmax(score /
temperature). SAMPLE during self-play, argmax at eval.

CRITIC (privileged): input = concat[ features(observe(seat_to_move)),
features(observe(other_seat)) ] = 1650-dim (both hands visible).
critic_net predicts V = P(seat_to_move wins). Trained (BCE) on the MC
return-to-go.

REWARD: terminal z (win 1 / loss 0 / draw 0.5) PLUS small tempo shaping
per learner step (opponent life lost + own board power developed, minus a
tiny idle-on-own-main penalty). Terminal dominates.

UPDATES (A2C / REINFORCE-with-baseline, MC returns, gamma=1):
G_t = z + sum_{k>=t} r_k ; critic regresses V(priv_t)->clip(G_t,0,1) ;
advantage A_t = G_t - V(priv_t) (standardized per batch) ; actor ascends
A_t * log pi(chosen afterstate_t).

CURRICULUM: half self-play (actor vs actor, both seats recorded), half
actor vs the engine's HANDCRAFTED bot (learner seat recorded only).

Honest eval is sacred: the gated actor uses ONLY its own redacted
observation (afterstate features from acting our OWN action on a clone,
exactly what the hosted bot does). Privileged both-hands info is ONLY for
the critic during training.

Usage:
  PENTA_ENGINE_DIR=engine-0.7.0 python3 aac_selfplay.py --games 4000 \
      --gate-every 500 --gate-games 80 --log aac_run.log
"""

import argparse
import json
import os
import random
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from extractor import import_penta, Extractor  # noqa: E402
from hosted_policy import load_decklists  # noqa: E402
from net import Net, MOMENTUM  # noqa: E402
from trainer import afterstate_rows, make_fork  # noqa: E402

FEAT = 825            # v2 redacted feature dim (one seat)
PRIV = 2 * FEAT       # privileged critic input (both seats)
MAX_DECISIONS = 600

# Tempo shaping magnitudes (small -- terminal z dominates a full game).
K_LIFE = 0.02         # per point of opponent life lost between our steps
K_POWER = 0.01        # per point of our board power developed
K_IDLE = 0.01         # penalty for passing on our own precombat main


# --------------------------------------------------------------------------
# Actor: numpy MLP (825 -> hidden -> 1 logit) with manual policy-gradient.
# We reuse net.Net purely as parameter storage; the scalar SCORE is the
# pre-sigmoid logit (fuller range than the squashed [0,1] value), and we
# accumulate REINFORCE gradients by hand and ascend them.
# --------------------------------------------------------------------------


def actor_logits(net, X):
    """X: (m, 825) -> (logits (m,), hidden h (m, H)) for backprop."""
    X = np.asarray(X, dtype=np.float64)
    h = np.tanh(X @ net.w1.T + net.b1)
    logits = h @ net.w2 + net.b2
    return logits, h


class GradAccum:
    """Accumulates actor parameter gradients across a batch of decisions."""

    def __init__(self, net):
        self.gw1 = np.zeros_like(net.w1)
        self.gb1 = np.zeros_like(net.b1)
        self.gw2 = np.zeros_like(net.w2)
        self.gb2 = 0.0

    def add_decision(self, net, X, h, upstream):
        """Backprop each candidate afterstate's logit with its per-action
        upstream gradient (d objective / d logit_a) and accumulate.

        X: (m,825), h: (m,H) from actor_logits, upstream: (m,)."""
        X = np.asarray(X, dtype=np.float64)
        u = np.asarray(upstream, dtype=np.float64)          # (m,)
        self.gw2 += u @ h                                   # (H,)
        self.gb2 += float(u.sum())
        d_h = np.outer(u, net.w2) * (1.0 - h * h)           # (m, H)
        self.gw1 += d_h.T @ X                               # (H, 825)
        self.gb1 += d_h.sum(axis=0)                         # (H,)


def actor_ascend(net, grad, lr, scale=1.0, clip=2.0):
    """Momentum gradient ASCENT step (maximise the REINFORCE objective),
    with global gradient-norm clipping to stop the policy-gradient blow-up
    (the high-lr run diverged after peaking; clipping bounds each step)."""
    v = net._vel
    g_w1 = grad.gw1 * scale
    g_b1 = grad.gb1 * scale
    g_w2 = grad.gw2 * scale
    g_b2 = grad.gb2 * scale
    gnorm = np.sqrt(sum(float(np.sum(np.asarray(g) ** 2))
                        for g in (g_w1, g_b1, g_w2, g_b2)))
    if gnorm > clip:
        f = clip / (gnorm + 1e-8)
        g_w1 *= f; g_b1 *= f; g_w2 *= f; g_b2 *= f
    v[0] = MOMENTUM * v[0] + lr * g_w1
    v[1] = MOMENTUM * v[1] + lr * g_b1
    v[2] = MOMENTUM * v[2] + lr * g_w2
    v[3] = MOMENTUM * v[3] + lr * g_b2
    net.w1 += v[0]
    net.b1 += v[1]
    net.w2 += v[2]
    net.b2 += v[3]


# --------------------------------------------------------------------------
# Tempo shaping helpers.
# --------------------------------------------------------------------------


def own_board_power(obs, card_power):
    """Sum of printed power of creatures the deciding seat controls."""
    me = obs["seat"]
    total = 0
    for perm in obs.get("battlefield", ()):
        if perm.get("controller") == me:
            total += card_power.get(perm.get("definition"), 0) or 0
    return total


def opp_life(obs):
    me = 0 if obs["seat"] == "p1" else 1
    return obs["life"][1 - me]


# --------------------------------------------------------------------------
# Actor policy: score afterstates, softmax, sample (or argmax for eval).
# --------------------------------------------------------------------------


def actor_decide(game, seat, obs, actor, extractor, temperature, rng,
                 sample):
    """Returns (action_index, record) where record is a dict of
    {cand_feats, h, chosen_local, logits} for the gradient, or None when
    the move is forced (a single legal action -- nothing to learn)."""
    actions = obs["legalActions"]
    if len(actions) == 1:
        return actions[0]["index"], None
    fork = make_fork(None, None, game=game)     # game.clone
    cand_feats, _terminals = afterstate_rows(fork, seat, actions, extractor)
    X = np.asarray(cand_feats, dtype=np.float64)
    logits, h = actor_logits(actor, X)
    z = logits / max(temperature, 1e-6)
    z = z - z.max()
    pi = np.exp(z)
    pi = pi / pi.sum()
    if sample:
        c = int(rng.choices(range(len(actions)), weights=pi)[0])
    else:
        c = int(np.argmax(logits))
    record = {"X": X, "h": h, "pi": pi, "chosen": c,
              "temp": max(temperature, 1e-6)}
    return actions[c]["index"], record


# --------------------------------------------------------------------------
# One episode -> list of per-seat decision records with shaped rewards.
# A decision record holds everything needed for the A2C update.
# --------------------------------------------------------------------------


def _priv_features(game, seat, extractor):
    """Privileged critic input: [features(seat_to_move), features(other)].
    Both are REDACTED per-seat observations, but concatenating BOTH gives
    the critic both hands -- the privilege the honest actor never sees."""
    other = "p2" if seat == "p1" else "p1"
    f_me = extractor.features(json.loads(game.observe(seat)))
    f_op = extractor.features(json.loads(game.observe(other)))
    return np.concatenate([f_me, f_op]).astype(np.float64)


def play_episode(actor, critic, extractor, penta, d1, d2, seed, temperature,
                 rng, mode, learner_seat, card_power):
    """mode: 'selfplay' (actor vs actor, record both seats) or
    'handcrafted' (actor vs engine handcrafted, record learner seat).

    Returns (records, result) where records is a list of dicts, each:
      seat, priv (1650,), X (cand afterstate feats), h, pi, chosen,
      temp, r (shaped reward at this step).
    Terminal z is filled in by the caller per seat."""
    if mode == "handcrafted":
        opp_seat = "p2" if learner_seat == "p1" else "p1"
        game = penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=opp_seat, seed=seed)
    else:
        game = penta.Game(d1, d2, opponent="external", seed=seed)

    records = []
    # Per-seat tempo trackers (previous afterstate opp-life / own-power).
    prev_opp_life = {"p1": None, "p2": None}
    prev_own_power = {"p1": None, "p2": None}
    n = 0
    while game.result() is None and n < MAX_DECISIONS:
        seat = game.decision_seat()
        record_this = (mode == "selfplay") or (seat == learner_seat)
        raw = game.observe()
        obs = json.loads(raw)
        if not record_this:
            # Shouldn't happen (engine drives handcrafted seat), but guard.
            game.act(obs["legalActions"][0]["index"])
            n += 1
            continue

        priv = _priv_features(game, seat, extractor)
        idx, rec = actor_decide(game, seat, obs, actor, extractor,
                                temperature, rng, sample=True)
        # Idle penalty: chose to pass priority on our own precombat main
        # while holding castable/playable options.
        chosen_action = next(a for a in obs["legalActions"]
                             if a["index"] == idx)
        idle = 0.0
        if (chosen_action.get("type") in ("Pass", "PassPriority")
                and obs.get("step") == "PrecombatMain"
                and obs.get("activeSeat") == seat
                and len(obs["legalActions"]) > 1):
            idle = 1.0

        game.act(idx)
        n += 1

        # Measure tempo on OUR redacted afterstate.
        after = json.loads(game.observe(seat))
        ol = after["life"][1 - (0 if seat == "p1" else 1)]
        op = own_board_power(after, card_power)
        r = -K_IDLE * idle
        if prev_opp_life[seat] is not None:
            r += K_LIFE * max(0, prev_opp_life[seat] - ol)
            r += K_POWER * max(0, op - prev_own_power[seat])
        prev_opp_life[seat] = ol
        prev_own_power[seat] = op

        if rec is not None:
            rec["seat"] = seat
            rec["priv"] = priv
            rec["r"] = r
            records.append(rec)

    result = game.result()
    return records, result


# --------------------------------------------------------------------------
# Assemble MC returns & advantages, then A2C update.
# --------------------------------------------------------------------------


def finalize_returns(records, result):
    """Fill each record with its return-to-go G (terminal z + future
    shaped rewards, gamma=1). Mutates records; returns them."""
    if result is None:
        return []
    for seat in ("p1", "p2"):
        seat_recs = [r for r in records if r["seat"] == seat]
        if not seat_recs:
            continue
        z = 0.5 if result == "draw" else (1.0 if result == seat else 0.0)
        # future shaped reward, walking backwards
        acc = 0.0
        for rec in reversed(seat_recs):
            acc += rec["r"]
            rec["G"] = z + acc
    return records


def a2c_update(batch, actor, critic, actor_lr, critic_lr, critic_epochs,
               entropy_beta, ppo_epochs=4, ppo_eps=0.2):
    """One PPO update over a batch of finalized decision records.

    Critic: BCE-regress critic(priv) -> clip(G,0,1).
    Actor: PPO clipped surrogate with standardized advantage A = G - V(priv),
    ppo_epochs passes over the batch. The clip bounds how far the policy can
    move per update -> stops the 27->7 win% swings of vanilla REINFORCE."""
    if not batch:
        return None
    priv = np.stack([r["priv"] for r in batch])              # (K,1650)
    G = np.array([r["G"] for r in batch], dtype=np.float64)   # (K,)

    # Advantage uses the critic BEFORE its update (standard A2C baseline).
    V = critic.value_batch(priv)                             # (K,)
    adv = G - V
    adv_std = adv.std()
    adv_n = adv / (adv_std + 1e-6) if adv_std > 1e-8 else adv * 0.0

    # --- critic regression (BCE to clipped return) ---
    Gt = np.clip(G, 0.0, 1.0)
    closs = 0.0
    for _ in range(critic_epochs):
        closs = critic.train_batch(priv, Gt, critic_lr)

    # old chosen-action prob (frozen policy at collection time) for the ratio
    for rec in batch:
        rec["pi_old_c"] = float(rec["pi"][rec["chosen"]])

    # --- PPO clipped actor update (ppo_epochs passes) ---
    ent_last = 0.0
    for _ in range(max(1, ppo_epochs)):
        grad = GradAccum(actor)
        ent_sum = 0.0
        for rec, a in zip(batch, adv_n):
            # recompute pi_new from the stored candidate afterstate features
            logits, h = actor_logits(actor, rec["X"])
            z = logits / rec["temp"]
            z = z - z.max()
            pi_new = np.exp(z)
            pi_new = pi_new / pi_new.sum()
            c = rec["chosen"]
            m = len(pi_new)
            ratio = pi_new[c] / (rec["pi_old_c"] + 1e-8)
            clipped = min(max(ratio, 1.0 - ppo_eps), 1.0 + ppo_eps)
            # PPO: L = min(ratio*A, clipped*A); gradient flows through the
            # ACTIVE (min) branch only. coef multiplies dlog pi(chosen).
            if ratio * a <= clipped * a:
                coef = ratio * a               # ratio branch active
            else:
                coef = 0.0                     # clipped branch -> no gradient
            onehot = np.zeros(m)
            onehot[c] = 1.0
            up_pg = coef * (onehot - pi_new) / rec["temp"]
            H = -np.sum(pi_new * np.log(pi_new + 1e-12))
            ent_sum += H
            up_ent = (entropy_beta * -pi_new * (np.log(pi_new + 1e-12) + H)
                      / rec["temp"])
            grad.add_decision(actor, rec["X"], h, up_pg + up_ent)
        actor_ascend(actor, grad, actor_lr, scale=1.0 / len(batch))
        ent_last = ent_sum / len(batch)

    return {"critic_loss": closs, "V_mean": float(V.mean()),
            "G_mean": float(G.mean()), "adv_std": float(adv_std),
            "entropy": float(ent_last)}


# --------------------------------------------------------------------------
# Honest gate: argmax afterstate actor (observation only) vs handcrafted.
# --------------------------------------------------------------------------


def gate(actor, extractor, penta, decks, learner_deck, games, seed_base,
         opp_decks):
    """Plays `games` honest games (Sligh learner, alternating seats) vs the
    engine handcrafted bot. Returns win rate (draws count 0.5)."""
    rng = random.Random(999)
    score = 0.0
    for g in range(games):
        my_seat = "p1" if g % 2 == 0 else "p2"
        opp_deck = opp_decks[g % len(opp_decks)]
        d1, d2 = (learner_deck, opp_deck) if my_seat == "p1" \
            else (opp_deck, learner_deck)
        opp_seat = "p2" if my_seat == "p1" else "p1"
        game = penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=opp_seat, seed=seed_base + g)
        n = 0
        while game.result() is None and n < MAX_DECISIONS:
            obs = json.loads(game.observe())
            idx, _ = actor_decide(game, my_seat, obs, actor, extractor,
                                  temperature=1.0, rng=rng, sample=False)
            game.act(idx)
            n += 1
        result = game.result()
        if result == my_seat:
            score += 1.0
        elif result == "draw" or result is None:
            score += 0.5
    return score / games


def wilson_lcb(wins, games, z=1.96):
    import math
    if games == 0:
        return 0.0
    p = wins / games
    denom = 1 + z * z / games
    centre = p + z * z / (2 * games)
    margin = z * math.sqrt(p * (1 - p) / games + z * z / (4 * games * games))
    return (centre - margin) / denom


# --------------------------------------------------------------------------
# Main training loop.
# --------------------------------------------------------------------------


def log(msg, path):
    line = f"[{time.strftime('%H:%M:%S')}] {msg}"
    print(line, flush=True)
    if path:
        with open(path, "a") as f:
            f.write(line + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--games", type=int, default=4000)
    ap.add_argument("--batch", type=int, default=8,
                    help="episodes per A2C update")
    ap.add_argument("--learner-deck", default="Sligh")
    ap.add_argument("--temperature", type=float, default=1.0)
    ap.add_argument("--actor-lr", type=float, default=0.05)
    ap.add_argument("--critic-lr", type=float, default=0.05)
    ap.add_argument("--critic-epochs", type=int, default=3)
    ap.add_argument("--entropy-beta", type=float, default=0.01)
    ap.add_argument("--ppo-epochs", type=int, default=1,
                    help="1 = vanilla A2C (the config that reached 26%%); "
                         ">1 = PPO clipped surrogate")
    ap.add_argument("--ppo-eps", type=float, default=0.2)
    ap.add_argument("--hidden", type=int, default=64)
    ap.add_argument("--selfplay-frac", type=float, default=0.5)
    ap.add_argument("--gate-every", type=int, default=500)
    ap.add_argument("--gate-games", type=int, default=80)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--log", default="")
    ap.add_argument("--save-prefix", default="aac")
    ap.add_argument("--smoke", action="store_true",
                    help="phase-1 plumbing check: one episode, print shapes")
    args = ap.parse_args()

    penta = import_penta()
    extractor = Extractor(version=2)
    assert extractor.size == FEAT, extractor.size
    decks = load_decklists()
    all_decks = list(decks.keys())
    opp_decks = [d for d in ("Goblins", "White Weenie", "The Deck",
                             "Counterburn", "Mono Black", "Jeskai Aggro",
                             "Erhnamgeddon") if d in decks]
    card_power = extractor.card_power

    actor = Net(FEAT, hidden=args.hidden, seed=1234 + args.seed)
    critic = Net(PRIV, hidden=args.hidden, seed=5678 + args.seed)
    rng = random.Random(args.seed)

    if args.smoke:
        recs, result = play_episode(
            actor, critic, extractor, penta, args.learner_deck, "Goblins",
            seed=42, temperature=args.temperature, rng=rng, mode="selfplay",
            learner_seat="p1", card_power=card_power)
        finalize_returns(recs, result)
        print(f"SMOKE: result={result} decisions_recorded={len(recs)}")
        if recs:
            r0 = recs[0]
            print(f"  priv shape={r0['priv'].shape} (expect ({PRIV},))")
            print(f"  cand afterstate X shape={r0['X'].shape} "
                  f"(m x {FEAT})")
            print(f"  chosen={r0['chosen']} pi={np.round(r0['pi'],3)}")
            nonzero_r = [round(r['r'], 4) for r in recs if abs(r['r']) > 1e-9]
            print(f"  nonzero shaped rewards ({len(nonzero_r)}): "
                  f"{nonzero_r[:12]}")
            print(f"  G range=[{min(r['G'] for r in recs):.3f}, "
                  f"{max(r['G'] for r in recs):.3f}]")
        stats = a2c_update(recs, actor, critic, args.actor_lr,
                           args.critic_lr, args.critic_epochs,
                           args.entropy_beta, args.ppo_epochs, args.ppo_eps)
        print(f"  a2c_update stats={stats}")
        print("  no NaN in actor:",
              not np.isnan(actor.w1).any(),
              "critic:", not np.isnan(critic.w1).any())
        return

    log(f"AAC start: games={args.games} batch={args.batch} "
        f"learner={args.learner_deck} actor_lr={args.actor_lr} "
        f"critic_lr={args.critic_lr} temp={args.temperature} "
        f"hidden={args.hidden} selfplay_frac={args.selfplay_frac}", args.log)

    # Gate at random init first (the baseline of the curve).
    wr0 = gate(actor, extractor, penta, decks, args.learner_deck,
               args.gate_games, 7_000_000, opp_decks)
    log(f"GATE @0 games: honest actor vs handcrafted = {100*wr0:.1f}% "
        f"(LCB {100*wilson_lcb(wr0*args.gate_games, args.gate_games):.1f}%)",
        args.log)

    batch = []
    episodes_in_batch = 0
    played = 0
    last_stats = None
    t0 = time.time()
    while played < args.games:
        # Curriculum: selfplay vs handcrafted mix.
        mode = "selfplay" if rng.random() < args.selfplay_frac \
            else "handcrafted"
        learner_seat = "p1" if rng.random() < 0.5 else "p2"
        opp_deck = rng.choice(opp_decks)
        if mode == "selfplay":
            # both seats are the learner deck? No -- keep decks asymmetric
            # but both driven by the actor. Use learner vs a random opp deck.
            d1, d2 = args.learner_deck, opp_deck
        else:
            if learner_seat == "p1":
                d1, d2 = args.learner_deck, opp_deck
            else:
                d1, d2 = opp_deck, args.learner_deck
        seed = 1_000_000 + played
        try:
            recs, result = play_episode(
                actor, critic, extractor, penta, d1, d2, seed,
                args.temperature, rng, mode, learner_seat, card_power)
        except Exception as e:
            log(f"  episode error (skipped): {str(e)[:120]}", args.log)
            played += 1
            continue
        recs = finalize_returns(recs, result)  # [] if game hit the cap
        batch.extend(recs)
        episodes_in_batch += 1
        played += 1

        if episodes_in_batch >= args.batch:
            last_stats = a2c_update(batch, actor, critic, args.actor_lr,
                                    args.critic_lr, args.critic_epochs,
                                    args.entropy_beta, args.ppo_epochs, args.ppo_eps)
            batch = []
            episodes_in_batch = 0

        if played % args.gate_every == 0:
            gr = args.gate_games
            wr = gate(actor, extractor, penta, decks, args.learner_deck,
                      gr, 7_000_000, opp_decks)
            rate = played / (time.time() - t0)
            s = last_stats or {}
            log(f"GATE @{played} games: {100*wr:.1f}% "
                f"(LCB {100*wilson_lcb(wr*gr, gr):.1f}%) | "
                f"critic_loss={s.get('critic_loss', float('nan')):.4f} "
                f"V_mean={s.get('V_mean', float('nan')):.3f} "
                f"G_mean={s.get('G_mean', float('nan')):.3f} "
                f"entropy={s.get('entropy', float('nan')):.3f} "
                f"adv_std={s.get('adv_std', float('nan')):.3f} "
                f"[{rate:.2f} g/s]", args.log)
            actor.save(f"{args.save_prefix}_actor.npz")
            critic.save(f"{args.save_prefix}_critic.npz")

    log(f"DONE {played} games in {time.time()-t0:.0f}s", args.log)
    actor.save(f"{args.save_prefix}_actor.npz")
    critic.save(f"{args.save_prefix}_critic.npz")


if __name__ == "__main__":
    main()
