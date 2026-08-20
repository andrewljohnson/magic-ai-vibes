#!/usr/bin/env python3
"""PyTorch asymmetric actor-critic (PPO) for the Penta MTG bot.

Robust rebuild of the numpy prototype (aac_selfplay.py), which learned from
random (8->26%) but was chaotic to tune. Same environment/dynamics; the ONLY
change is proper RL machinery: Adam optimizers, minibatch PPO clipped
surrogate over several epochs, normalized advantage, value loss, entropy
bonus, and gradient clipping -- the standard ingredients that turn a chaotic
bounce into a smooth climb.

ACTOR (honest): 825->H->1 afterstate scorer; pi(a)=softmax(score/temp) over
legal actions; sample in self-play, argmax at eval. CRITIC (privileged):
1650->H->1 (both seats' redacted obs concatenated = both hands). Tempo shaping
+ handcrafted-mixed curriculum, all reused from aac_selfplay. Honest gate =
actor argmax (obs only) vs the engine handcrafted bot.

Run: PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python aac_torch.py \
       --games 12000 --gate-every 1000 --log aac_torch.log
"""
import argparse
import json
import os
import random
import sys
import time

import numpy as np
import torch
import torch.nn as nn

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from extractor import import_penta, Extractor  # noqa: E402
from hosted_policy import load_decklists  # noqa: E402
from trainer import afterstate_rows, make_fork  # noqa: E402
from aac_selfplay import (  # noqa: E402  reuse env dynamics
    _priv_features, own_board_power, K_IDLE, K_LIFE, K_POWER,
    MAX_DECISIONS, FEAT, PRIV)

DEV = torch.device("cpu")   # tiny nets; engine sim dominates, CPU avoids xfer


class MLP(nn.Module):
    def __init__(self, nin, hidden):
        super().__init__()
        self.f1 = nn.Linear(nin, hidden)
        self.f2 = nn.Linear(hidden, 1)

    def forward(self, x):
        return self.f2(torch.tanh(self.f1(x))).squeeze(-1)


def actor_scores(actor, cand_np):
    """logits (m,) for a decision's candidate afterstates (numpy in)."""
    X = torch.as_tensor(cand_np, dtype=torch.float32, device=DEV)
    return actor(X)


def decide(game, seat, obs, actor, extractor, temperature, sample):
    actions = obs["legalActions"]
    if len(actions) == 1:
        return actions[0]["index"], None
    fork = make_fork(None, None, game=game)
    cand, _term = afterstate_rows(fork, seat, actions, extractor)
    cand = np.asarray(cand, dtype=np.float32)
    with torch.no_grad():
        logits = actor_scores(actor, cand)
        pi = torch.softmax(logits / max(temperature, 1e-6), dim=0)
        if sample:
            c = int(torch.multinomial(pi, 1).item())
        else:
            c = int(torch.argmax(logits).item())
        logp_old = float(torch.log(pi[c] + 1e-12))
    rec = {"cand": cand, "chosen": c, "logp_old": logp_old,
           "temp": max(temperature, 1e-6)}
    return actions[c]["index"], rec


def play_episode(actor, extractor, penta, d1, d2, seed, temperature,
                 mode, learner_seat, card_power):
    if mode == "handcrafted":
        opp = "p2" if learner_seat == "p1" else "p1"
        game = penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=opp, seed=seed)
    else:
        game = penta.Game(d1, d2, opponent="external", seed=seed)
    records = []
    prev_opp_life = {"p1": None, "p2": None}
    prev_own_power = {"p1": None, "p2": None}
    n = 0
    while game.result() is None and n < MAX_DECISIONS:
        seat = game.decision_seat()
        record_this = (mode == "selfplay") or (seat == learner_seat)
        obs = json.loads(game.observe())
        if not record_this:
            game.act(obs["legalActions"][0]["index"])
            n += 1
            continue
        priv = _priv_features(game, seat, extractor)
        idx, rec = decide(game, seat, obs, actor, extractor,
                          temperature, sample=True)
        chosen_action = next(a for a in obs["legalActions"]
                             if a["index"] == idx)
        idle = 1.0 if (chosen_action.get("type") in ("Pass", "PassPriority")
                       and obs.get("step") == "PrecombatMain"
                       and obs.get("activeSeat") == seat
                       and len(obs["legalActions"]) > 1) else 0.0
        game.act(idx)
        n += 1
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
    return records, game.result()


def finalize_returns(records, result):
    """MC return-to-go per record (terminal z + future shaped, gamma=1)."""
    if result is None:
        return []
    for seat in ("p1", "p2"):
        recs = [r for r in records if r["seat"] == seat]
        if not recs:
            continue
        z = 0.5 if result == "draw" else (1.0 if result == seat else 0.0)
        future = 0.0
        for rec in reversed(recs):
            future += rec["r"]
            rec["G"] = z + future
    return [r for r in records if "G" in r]


def ppo_update(batch, actor, critic, aopt, copt, ppo_eps, ppo_epochs,
               ent_beta, clip, minibatch):
    if not batch:
        return None
    priv = torch.as_tensor(np.stack([r["priv"] for r in batch]),
                           dtype=torch.float32, device=DEV)
    G = torch.as_tensor(np.array([r["G"] for r in batch], dtype=np.float32),
                        device=DEV)
    with torch.no_grad():
        V = critic(priv)
        adv = G - V
        adv = (adv - adv.mean()) / (adv.std() + 1e-6)
    logp_old = torch.as_tensor(
        np.array([r["logp_old"] for r in batch], dtype=np.float32), device=DEV)

    idx = np.arange(len(batch))
    closs = ploss = ent_val = 0.0
    for _ in range(ppo_epochs):
        np.random.shuffle(idx)
        for s in range(0, len(idx), minibatch):
            mb = idx[s:s + minibatch]
            # actor: per-decision softmax (variable action counts) -> loop
            logp_new = []
            ent = []
            for j in mb:
                logits = actor_scores(actor, batch[j]["cand"])
                pi = torch.softmax(logits / batch[j]["temp"], dim=0)
                lp = torch.log(pi[batch[j]["chosen"]] + 1e-12)
                logp_new.append(lp)
                ent.append(-(pi * torch.log(pi + 1e-12)).sum())
            logp_new = torch.stack(logp_new)
            ent = torch.stack(ent).mean()
            a_mb = adv[mb]
            ratio = torch.exp(logp_new - logp_old[mb])
            surr1 = ratio * a_mb
            surr2 = torch.clamp(ratio, 1 - ppo_eps, 1 + ppo_eps) * a_mb
            pol_loss = -torch.min(surr1, surr2).mean() - ent_beta * ent
            aopt.zero_grad()
            pol_loss.backward()
            nn.utils.clip_grad_norm_(actor.parameters(), clip)
            aopt.step()
            # critic: value regression to G
            v = critic(priv[mb])
            val_loss = ((v - G[mb]) ** 2).mean()
            copt.zero_grad()
            val_loss.backward()
            nn.utils.clip_grad_norm_(critic.parameters(), clip)
            copt.step()
            closs = float(val_loss)
            ploss = float(pol_loss)
            ent_val = float(ent)
    return {"critic_loss": closs, "pol_loss": ploss, "entropy": ent_val,
            "V_mean": float(V.mean()), "G_mean": float(G.mean())}


def gate(actor, extractor, penta, decks, learner_deck, games, seed_base):
    opps = [d for d in decks if d != learner_deck]
    wins = 0.0
    for g in range(games):
        my_seat = "p1" if g % 2 == 0 else "p2"
        opp_deck = opps[g % len(opps)]
        d1, d2 = (learner_deck, opp_deck) if my_seat == "p1" \
            else (opp_deck, learner_deck)
        opp = "p2" if my_seat == "p1" else "p1"
        game = penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=opp, seed=seed_base + g)
        n = 0
        while game.result() is None and n < MAX_DECISIONS:
            obs = json.loads(game.observe())
            idx, _ = decide(game, game.decision_seat(), obs, actor,
                            extractor, 1.0, sample=False)
            game.act(idx)
            n += 1
        res = game.result()
        wins += 1.0 if res == my_seat else (0.5 if res in (None, "draw")
                                            else 0.0)
    return wins / games


def log(msg, path):
    line = f"[{time.strftime('%H:%M:%S')}] {msg}"
    print(line, flush=True)
    if path:
        with open(path, "a") as f:
            f.write(line + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=12000)
    ap.add_argument("--batch", type=int, default=16, help="episodes / update")
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--actor-lr", type=float, default=3e-4)
    ap.add_argument("--critic-lr", type=float, default=1e-3)
    ap.add_argument("--ppo-epochs", type=int, default=4)
    ap.add_argument("--ppo-eps", type=float, default=0.2)
    ap.add_argument("--minibatch", type=int, default=256)
    ap.add_argument("--entropy-beta", type=float, default=0.01)
    ap.add_argument("--clip", type=float, default=1.0)
    ap.add_argument("--temperature", type=float, default=1.0)
    ap.add_argument("--learner-deck", default="Sligh")
    ap.add_argument("--selfplay-frac", type=float, default=0.5)
    ap.add_argument("--gate-every", type=int, default=1000)
    ap.add_argument("--gate-games", type=int, default=100)
    ap.add_argument("--seed", type=int, default=12345)
    ap.add_argument("--log", default="aac_torch.log")
    ap.add_argument("--save-prefix", default="aac_torch")
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    rng = random.Random(args.seed)
    penta = import_penta()
    extractor = Extractor(version=2)   # 825-dim (matches FEAT/PRIV)
    decks = list(load_decklists())
    card_power = extractor.card_power if hasattr(extractor, "card_power") \
        else {}

    actor = MLP(FEAT, args.hidden).to(DEV)
    critic = MLP(PRIV, args.hidden).to(DEV)
    aopt = torch.optim.Adam(actor.parameters(), lr=args.actor_lr)
    copt = torch.optim.Adam(critic.parameters(), lr=args.critic_lr)

    log(f"TORCH-AAC start: games={args.games} batch={args.batch} "
        f"hidden={args.hidden} a_lr={args.actor_lr} c_lr={args.critic_lr} "
        f"ppo_epochs={args.ppo_epochs} eps={args.ppo_eps} "
        f"ent={args.entropy_beta} clip={args.clip}", args.log)
    wr0 = gate(actor, extractor, penta, decks, args.learner_deck,
               max(24, args.gate_games // 4), 900000)
    log(f"GATE @0 games: honest actor vs handcrafted = {100*wr0:.1f}%",
        args.log)

    opps = [d for d in decks if d != args.learner_deck]
    played = 0
    t0 = time.time()
    ep_seed = args.seed * 7919 + 1
    while played < args.games:
        batch = []
        for _ in range(args.batch):
            mode = "selfplay" if rng.random() < args.selfplay_frac \
                else "handcrafted"
            learner_seat = "p1" if rng.random() < 0.5 else "p2"
            opp_deck = opps[rng.randrange(len(opps))]
            if learner_seat == "p1":
                d1, d2 = args.learner_deck, opp_deck
            else:
                d1, d2 = opp_deck, args.learner_deck
            recs, res = play_episode(actor, extractor, penta, d1, d2,
                                     ep_seed, args.temperature, mode,
                                     learner_seat, card_power)
            ep_seed += 1
            batch.extend(finalize_returns(recs, res))
            played += 1
        stats = ppo_update(batch, actor, critic, aopt, copt, args.ppo_eps,
                           args.ppo_epochs, args.entropy_beta, args.clip,
                           args.minibatch)
        if played % args.gate_every < args.batch:
            wr = gate(actor, extractor, penta, decks, args.learner_deck,
                      args.gate_games, 900000)
            gps = played / (time.time() - t0)
            s = stats or {}
            log(f"GATE @{played} games: {100*wr:.1f}% | "
                f"critic_loss={s.get('critic_loss',0):.3f} "
                f"entropy={s.get('entropy',0):.3f} "
                f"V_mean={s.get('V_mean',0):.3f} G_mean={s.get('G_mean',0):.3f} "
                f"[{gps:.2f} g/s]", args.log)
            np.savez(f"{args.save_prefix}_actor.npz",
                     **{k: v.detach().numpy() for k, v in
                        actor.state_dict().items()})
    log(f"TORCH-AAC complete: {played} games", args.log)


if __name__ == "__main__":
    main()
