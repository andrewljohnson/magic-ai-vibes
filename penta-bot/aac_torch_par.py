#!/usr/bin/env python3
"""Parallel PyTorch AAC (PPO+GAE) for the Penta MTG bot.

Same recipe as aac_torch.py (proven to beat the 31.6% baseline: PPO clipped
surrogate + GAE + privileged critic + tempo shaping), but self-play episode
COLLECTION runs across N worker processes -- the ~8x throughput multiplier that
makes "bigger net + far more games" runnable. Workers score afterstates with a
NUMPY forward of the current actor (no torch in workers); the main process holds
the torch actor+critic, broadcasts actor weights each round, and does GAE +
minibatch PPO. Evaluation = actor argmax vs handcrafted.

Run: PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python aac_torch_par.py \
       --games 60000 --workers 8 --hidden 256 --gate-every 4000 --log par.log
"""
import argparse
import json
import os
# Workers use only numpy+engine (never torch); fork lets torch load ONCE in the
# parent and be inherited copy-on-write instead of re-imported per worker (spawn
# default = 8x torch memory). Env var avoids macOS objc fork-safety aborts.
os.environ.setdefault("OBJC_DISABLE_INITIALIZE_FORK_SAFETY", "YES")
# Pin BLAS/OpenMP to 1 thread PER PROCESS: with N worker processes, letting each
# spawn a full BLAS thread pool oversubscribes the cores (measured ~9 threads x
# 6 workers on 10 cores -> 920% CPU but only 0.7 g/s from contention). One thread
# per worker lets the N workers cleanly use N cores. Set before numpy import.
for _v in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
           "VECLIB_MAXIMUM_THREADS", "NUMEXPR_NUM_THREADS"):
    os.environ.setdefault(_v, "1")
import random
import sys
import time
import multiprocessing as mp
from multiprocessing import Pool

import numpy as np
import torch
import torch.nn as nn

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from extractor import import_penta, Extractor  # noqa: E402
from hosted_policy import load_decklists, belief_deck_context  # noqa: E402
from trainer import afterstate_rows, make_fork  # noqa: E402
from aac_selfplay import (  # noqa: E402
    _priv_features, own_board_power, K_IDLE, K_LIFE, K_POWER,
    MAX_DECISIONS)
import aac_torch as AT  # noqa: E402  MLP, compute_gae, ppo_update, gate
import aac_native as AN  # noqa: E402  native episode generation + gate

DEV = torch.device("cpu")

# Set by main() before the (forked) Pool is created, so workers inherit them.
BELIEF = False
DECKS = None

# ---- worker globals (set once per process) --------------------------------
_W = {}


def _winit():
    _W["penta"] = import_penta()
    _W["ex"] = Extractor(version=2, belief=BELIEF)
    _W["card_power"] = _W["ex"].card_power
    _W["decks"] = DECKS if DECKS is not None else load_decklists()


def _np_logits(cand, w1, b1, w2, b2):
    h = np.tanh(cand.astype(np.float64) @ w1.T + b1)
    return h @ w2 + b2


def _worker_episode(task):
    """task: (weights, temperature, mode, learner_seat, d1, d2, seed).
    Plays one episode with a NUMPY forward of the actor; returns
    (records, result). records carry cand/chosen/logp_old/priv/r/seat."""
    (w1, b1, w2, b2), temp, mode, learner_seat, d1, d2, seed = task
    penta = _W["penta"]
    ex = _W["ex"]
    card_power = _W["card_power"]
    rng = random.Random(seed ^ 0x9E37)
    if mode == "handcrafted":
        opp = "p2" if learner_seat == "p1" else "p1"
        game = penta.Game(d1, d2, opponent="handcrafted",
                          opponent_seat=opp, seed=seed)
    else:
        game = penta.Game(d1, d2, opponent="external", seed=seed)
    records = []
    prev_ol = {"p1": None, "p2": None}
    prev_op = {"p1": None, "p2": None}
    n = 0
    while game.result() is None and n < MAX_DECISIONS:
        seat = game.decision_seat()
        record_this = (mode == "selfplay") or (seat == learner_seat)
        obs = json.loads(game.observe())
        acts = obs["legalActions"]
        if not record_this:
            game.act(acts[0]["index"])
            n += 1
            continue
        if len(acts) == 1:
            game.act(acts[0]["index"])
            n += 1
            continue
        if ex.belief:
            # our_deck is known (we pilot this seat); opp deck is classified
            # from its revealed cards -- never from its hidden hand. Context
            # is per-seat so both priv seats + all afterstates use it.
            belief_deck_context(ex, _W["decks"], obs,
                                d1 if seat == "p1" else d2)
        priv = _priv_features(game, seat, ex)
        fork = make_fork(None, None, game=game)
        cand, _t = afterstate_rows(fork, seat, acts, ex)
        cand = np.asarray(cand, dtype=np.float32)
        logits = _np_logits(cand, w1, b1, w2, b2)
        z = logits / max(temp, 1e-6)
        z = z - z.max()
        pi = np.exp(z)
        pi = pi / pi.sum()
        c = rng.choices(range(len(acts)), weights=pi)[0]
        idx = acts[c]["index"]
        logp_old = float(np.log(pi[c] + 1e-12))
        chosen_action = next(a for a in acts if a["index"] == idx)
        idle = 1.0 if (chosen_action.get("type") in ("Pass", "PassPriority")
                       and obs.get("step") == "PrecombatMain"
                       and obs.get("activeSeat") == seat) else 0.0
        game.act(idx)
        n += 1
        after = json.loads(game.observe(seat))
        ol = after["life"][1 - (0 if seat == "p1" else 1)]
        op = own_board_power(after, card_power)
        r = -K_IDLE * idle
        if prev_ol[seat] is not None:
            r += K_LIFE * max(0, prev_ol[seat] - ol)
            r += K_POWER * max(0, op - prev_op[seat])
        prev_ol[seat] = ol
        prev_op[seat] = op
        records.append({"cand": cand, "chosen": c, "logp_old": logp_old,
                        "temp": max(temp, 1e-6), "seat": seat, "priv": priv,
                        "r": r})
    return records, game.result()


def ppo_update_fast(batch, actor, critic, aopt, copt, ppo_eps, ppo_epochs,
                    ent_beta, clip, vhead=None, vopt=None, feat=None):
    """Vectorized PPO: one concatenated actor forward + segmented softmax over
    all decisions, instead of a per-decision Python loop. Removes the serial
    learner barrier that idles the workers -- and makes bigger nets ~free.
    Full-batch per epoch (minibatching was only needed for the Python loop)."""
    if not batch:
        return None
    N = len(batch)
    cand_list = [np.asarray(r["cand"], dtype=np.float32) for r in batch]
    counts = np.array([c.shape[0] for c in cand_list])
    offsets = np.zeros(N, dtype=np.int64)
    offsets[1:] = np.cumsum(counts)[:-1]
    cat = torch.as_tensor(np.concatenate(cand_list, 0), device=DEV)   # (R,FEAT)
    seg = torch.as_tensor(np.repeat(np.arange(N), counts), device=DEV)  # (R,)
    temp_seg = torch.as_tensor(np.array([r["temp"] for r in batch],
                                        dtype=np.float32), device=DEV)   # (N,)
    chosen_g = torch.as_tensor(
        offsets + np.array([r["chosen"] for r in batch], dtype=np.int64),
        device=DEV)                                                      # (N,)
    priv = torch.as_tensor(np.stack([r["priv"] for r in batch]),
                           dtype=torch.float32, device=DEV)
    ret = torch.as_tensor(np.array([r["ret"] for r in batch],
                                   dtype=np.float32), device=DEV)
    adv_raw = torch.as_tensor(np.array([r["adv"] for r in batch],
                                       dtype=np.float32), device=DEV)
    adv = (adv_raw - adv_raw.mean()) / (adv_raw.std() + 1e-6)
    # outcome targets for the value head (see compute_gae): P(win), not the
    # shaped return the critic regresses
    zt = torch.as_tensor(np.array([r.get("z", 0.5) for r in batch],
                                  dtype=np.float32), device=DEV).clamp(0, 1)
    logp_old = torch.as_tensor(np.array([r["logp_old"] for r in batch],
                                        dtype=np.float32), device=DEV)
    val_loss = pol_loss = ent = None
    for _ in range(ppo_epochs):
        logits = actor(cat) / temp_seg[seg]                 # (R,)
        seg_max = torch.full((N,), -1e30, device=DEV).scatter_reduce(
            0, seg, logits, reduce="amax", include_self=True).detach()
        shifted = logits - seg_max[seg]
        exp = shifted.exp()
        seg_sum = torch.zeros(N, device=DEV).scatter_add(0, seg, exp)
        logpi = shifted - torch.log(seg_sum[seg] + 1e-12)   # log-softmax/row
        logp_new = logpi[chosen_g]                          # (N,)
        pi = exp / seg_sum[seg]
        ent_per = torch.zeros(N, device=DEV).scatter_add(0, seg, -(pi * logpi))
        ent = ent_per.mean()
        ratio = torch.exp(logp_new - logp_old)
        surr1 = ratio * adv
        surr2 = torch.clamp(ratio, 1 - ppo_eps, 1 + ppo_eps) * adv
        pol_loss = -torch.min(surr1, surr2).mean() - ent_beta * ent
        aopt.zero_grad()
        pol_loss.backward()
        nn.utils.clip_grad_norm_(actor.parameters(), clip)
        aopt.step()
        v = critic(priv)
        val_loss = ((v - ret) ** 2).mean()
        copt.zero_grad()
        val_loss.backward()
        nn.utils.clip_grad_norm_(critic.parameters(), clip)
        copt.step()
        if vhead is not None:
            # Same targets, but only the acting seat's own half of the row.
            #
            # BCE-ON-LOGITS, not MSE, and that choice is load-bearing: the
            # native reader (net.rs Mlp::value) applies a sigmoid. Train
            # with MSE and the raw output is already a probability in
            # [0,1], which that sigmoid then squashes into [0.5, 0.73] --
            # monotone, but most of the resolution gone. Training on
            # logits makes the reader's sigmoid exactly right, so the head
            # exports to .spzw with no Rust change and no calibration step.
            # Targets are soft (a draw is 0.5), which BCE accepts.
            vh = vhead(priv[:, :feat])
            vh_loss = torch.nn.functional.binary_cross_entropy_with_logits(
                vh, zt)
            vopt.zero_grad()
            vh_loss.backward()
            nn.utils.clip_grad_norm_(vhead.parameters(), clip)
            vopt.step()
    with torch.no_grad():
        V = critic(priv)
        # report the MSE of the SQUASHED output, so the number stays
        # comparable to the target variance below it
        vh_mse = float(((torch.sigmoid(vhead(priv[:, :feat])) - zt) ** 2)
                       .mean()) if vhead is not None else 0.0
        # variance of the OUTCOME targets: MSE below this means the head is
        # genuinely predicting, not just emitting the base rate
        base_mse = float(((zt - zt.mean()) ** 2).mean())
    return {"vhead_mse": vh_mse, "base_mse": base_mse,
            "critic_loss": float(val_loss.detach()),
            "pol_loss": float(pol_loss.detach()),
            "entropy": float(ent.detach()), "V_mean": float(V.mean()),
            "G_mean": float(ret.mean())}


def actor_weights(actor):
    sd = actor.state_dict()
    return (sd["f1.weight"].numpy().copy(), sd["f1.bias"].numpy().copy(),
            sd["f2.weight"].numpy().squeeze(0).copy(),
            float(sd["f2.bias"].numpy()[0]))


def load_actor(path, feat, hidden):
    """Load an actor npz into MLP(feat, hidden). If the saved net is NARROWER
    (e.g. the 825-feature 40% actor grown into the 1081 belief schema), copy
    the learned input columns and ZERO the new ones -- the actor starts
    behaviourally identical, then learns to use the belief block."""
    d = np.load(path)
    m = AT.MLP(feat, hidden)
    sd = m.state_dict()
    old_w = np.asarray(d["f1.weight"])           # (hidden, old_feat)
    of = old_w.shape[1]
    if of > feat:
        raise ValueError(f"saved actor has {of} cols > target {feat}")
    new_w = sd["f1.weight"].numpy().copy()
    new_w[:, :of] = old_w
    new_w[:, of:] = 0.0
    sd["f1.weight"] = torch.as_tensor(new_w)
    sd["f1.bias"] = torch.as_tensor(np.asarray(d["f1.bias"]))
    sd["f2.weight"] = torch.as_tensor(np.asarray(d["f2.weight"]))
    sd["f2.bias"] = torch.as_tensor(np.asarray(d["f2.bias"]))
    m.load_state_dict(sd)
    return m, of


def gate_belief(actor, extractor, penta, decks, learner_deck, games,
                seed_base):
    """AT.gate, but sets the hidden-pool belief context per decision: our_deck
    is known (we pilot it), opp deck is classified from its revealed cards.
    Identical to AT.gate when extractor.belief is False."""
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
            seat = game.decision_seat()
            if extractor.belief:
                belief_deck_context(extractor, decks, obs,
                                    learner_deck if seat == my_seat
                                    else opp_deck)
            idx, _ = AT.decide(game, seat, obs, actor, extractor, 1.0,
                               sample=False)
            game.act(idx)
            n += 1
        res = game.result()
        wins += 1.0 if res == my_seat else (0.5 if res in (None, "draw")
                                            else 0.0)
    return wins / games


def native_or_python_gate(args, spz, actor, extractor, penta, decks, games):
    """The evaluation, through whichever runner this run is using.

    Both play actor argmax vs the handcrafted bot over the same seats,
    decks and seeds; the native one just runs the games on threads instead
    of in one Python loop. --python-gate forces the Python path (useful
    for spot-checking the two against each other mid-run)."""
    if args.native and not args.python_gate:
        return AN.gate(spz, actor, args.hidden, args.belief,
                       args.learner_deck, games, 900000,
                       threads=args.native_threads,
                       max_actions=args.native_max_actions,
                       open_decklist=not args.classify_decklist)
    return gate_belief(actor, extractor, penta, decks, args.learner_deck,
                       games, 900000)


def log(msg, path):
    line = f"[{time.strftime('%H:%M:%S')}] {msg}"
    print(line, flush=True)
    if path:
        with open(path, "a") as f:
            f.write(line + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=60000)
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--round-episodes", type=int, default=64,
                    help="episodes collected (in parallel) per PPO update")
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--actor-lr", type=float, default=1e-3)
    ap.add_argument("--critic-lr", type=float, default=2e-3)
    ap.add_argument("--ppo-epochs", type=int, default=4)
    ap.add_argument("--ppo-eps", type=float, default=0.2)
    ap.add_argument("--minibatch", type=int, default=512)
    ap.add_argument("--entropy-beta", type=float, default=0.01)
    ap.add_argument("--clip", type=float, default=1.0)
    ap.add_argument("--gae-lambda", type=float, default=0.95)
    ap.add_argument("--temperature", type=float, default=1.0)
    ap.add_argument("--learner-deck", default="Sligh")
    ap.add_argument("--selfplay-frac", type=float, default=0.5)
    ap.add_argument("--gate-every", type=int, default=4000)
    ap.add_argument("--gate-games", type=int, default=120)
    ap.add_argument("--seed", type=int, default=12345)
    ap.add_argument("--per-result-timeout", type=float, default=120.0,
                    help="secs to wait for the next episode before assuming a "
                         "worker hung in native code; recreate pool on timeout")
    ap.add_argument("--init-actor", default=None,
                    help="npz of actor state_dict to warm-start from; a "
                         "narrower net is grown (belief columns zero-padded)")
    ap.add_argument("--belief", action="store_true",
                    help="use the hidden-pool belief feature schema "
                         "(v2+2*defs=1081); actor sees unseen-pool counts for "
                         "[me, opp] -- own deck known, opp deck classified")
    ap.add_argument("--native", action="store_true",
                    help="generate episodes in spz-core's native AAC runner "
                         "instead of the fork() worker pool: same rows "
                         "(aac_lockstep.py holds them to bit-equality), no "
                         "JSON in the hot loop, OS threads instead of "
                         "processes, and no hang watchdog to pay for")
    ap.add_argument("--native-threads", type=int, default=0,
                    help="native worker threads (0 = all cores)")
    ap.add_argument("--native-max-actions", type=int,
                    default=AN.DEFAULT_MAX_ACTIONS,
                    help="cap the afterstate expansion at this many legal "
                         "actions (0 = expand everything). Above the cap a "
                         "decision is played greedily from a prefix and "
                         "emits no training row")
    # OPEN DECKLISTS ARE THE ARCHITECTURE (decided 2026-08-22). The belief
    # block gets the opponent's REAL decklist; only their HIDDEN HAND stays
    # hidden. Classifying the deck Classifying the deck from
    # revealed cards was the old AAC default and it is measurably lossy:
    # 76.6% accurate overall, but 0% on turn 1, 53% turn 2, 64% turn 3 --
    # a quarter of decisions ran the unseen-pool maths against the wrong 60
    # cards, worst exactly where planning matters. It also made our numbers
    # incomparable to the 57.7% C++ reference, which came from the
    # determinized lineage where decklists were always open.
    ap.add_argument("--classify-decklist", action="store_true",
                    help="opt back OUT to the old behaviour: infer the "
                         "opponent's deck from its revealed cards instead "
                         "of being given it (76.6%% accurate; 0%% on turn 1)")
    ap.add_argument("--python-gate", action="store_true",
                    help="with --native, still gate through the Python loop "
                         "(slower; the native gate is the default there)")
    ap.add_argument("--log", default="aac_par.log")
    ap.add_argument("--save-prefix", default="aac_par")
    args = ap.parse_args()

    global BELIEF, DECKS
    spz = AN.load_spz_core() if args.native else None
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    rng = random.Random(args.seed)
    penta = import_penta()
    extractor = Extractor(version=2, belief=args.belief)
    decks = load_decklists()                     # {name: {def: count}}
    opps = [d for d in decks if d != args.learner_deck]
    feat = extractor.size                        # 825 or 1081 (belief)
    priv_dim = 2 * feat
    BELIEF = args.belief                          # inherited by forked workers
    DECKS = decks

    critic = AT.MLP(priv_dim, args.hidden).to(DEV)
    # OBSERVATION-ONLY VALUE HEAD (ROADMAP #1). V(s) ~ P(win) from ONE
    # seat's redacted view -- the piece that blocks search.
    #
    # The actor cannot serve as V: it is trained only on a softmax over one
    # decision's siblings, so its logit is a relative ranking with no
    # absolute scale, and Platt calibration measured it at ~0.003 nats over
    # base rate. The critic HAS real value targets but is privileged (both
    # seats) and may not deploy.
    #
    # This head is free to train: the privileged row is already
    # [features(me), features(other)], so its first half IS the
    # observation-only input, and it regresses the same returns the critic
    # does. One extra small MLP per round, no new game generation.
    vhead = AT.MLP(feat, args.hidden).to(DEV)
    vopt = torch.optim.Adam(vhead.parameters(), lr=args.critic_lr)
    if args.init_actor:
        actor, of = load_actor(args.init_actor, feat, args.hidden)
        actor = actor.to(DEV)
        log(f"warm-started actor from {args.init_actor} "
            f"(grew {of} -> {feat} cols, new cols zeroed)", args.log)
    else:
        actor = AT.MLP(feat, args.hidden).to(DEV)
    aopt = torch.optim.Adam(actor.parameters(), lr=args.actor_lr)
    copt = torch.optim.Adam(critic.parameters(), lr=args.critic_lr)

    runner = (f"NATIVE(threads={args.native_threads or 'all'},"
              f"max_actions={args.native_max_actions},"
              f"decklist={'classified' if args.classify_decklist else 'OPEN'})"
              if args.native else f"pool(workers={args.workers})")
    log(f"PAR-AAC start: games={args.games} runner={runner} "
        f"round_ep={args.round_episodes} hidden={args.hidden} "
        f"belief={args.belief} feat={feat} "
        f"a_lr={args.actor_lr} ppo_epochs={args.ppo_epochs} "
        f"ent={args.entropy_beta} gae_lam={args.gae_lambda}", args.log)
    wr0 = native_or_python_gate(args, spz, actor, extractor, penta, decks,
                                40)
    log(f"GATE @0 games: actor vs handcrafted = {100*wr0:.1f}%",
        args.log)

    played = 0
    ep_seed = args.seed * 7919 + 1
    t0 = time.time()
    last_gate = 0
    hangs = 0
    best_wr = -1.0
    capped = 0
    pool = None if args.native else Pool(args.workers, initializer=_winit)
    while played < args.games:
        w = actor_weights(actor)
        tasks = []
        for _ in range(args.round_episodes):
            mode = "selfplay" if rng.random() < args.selfplay_frac \
                else "handcrafted"
            ls = "p1" if rng.random() < 0.5 else "p2"
            od = opps[rng.randrange(len(opps))]
            d1, d2 = (args.learner_deck, od) if ls == "p1" \
                else (od, args.learner_deck)
            tasks.append((w, args.temperature, mode, ls, d1, d2, ep_seed))
            ep_seed += 1
        batch = []
        if args.native:
            # One PyO3 call for the whole round: the native runner plays
            # every episode across OS threads with the GIL released, so
            # there is no worker pool to hang and no per-decision boundary
            # crossing. The records come back in the shape the pool
            # produced, so GAE and PPO below are untouched.
            specs = [(d1, d2, s, mode == "handcrafted", ls == "p1")
                     for (_w, _t, mode, ls, d1, d2, s) in tasks]
            episodes, nstats = AN.stream_episodes(
                spz, actor, args.hidden, args.belief, specs,
                args.temperature, threads=args.native_threads,
                max_actions=args.native_max_actions,
                open_decklist=not args.classify_decklist)
            capped += nstats["capped"]
            for recs, res, final in episodes:
                played += 1
                batch.extend(AT.compute_gae(recs, res, critic,
                                            lam=args.gae_lambda,
                                            final=final))
        else:
            it = pool.imap_unordered(_worker_episode, tasks)
            for _ in range(len(tasks)):
                try:
                    recs, res = it.next(timeout=args.per_result_timeout)
                except mp.TimeoutError:
                    # A worker is spinning in native engine code (unbounded
                    # by MAX_DECISIONS). Can't interrupt native from Python
                    # -- kill and rebuild the pool, keep what was collected.
                    hangs += 1
                    log(f"HANG #{hangs}: no episode in "
                        f"{args.per_result_timeout:.0f}s; rebuilding pool "
                        f"(kept {len(batch)} recs this round)", args.log)
                    pool.terminate()
                    pool.join()
                    pool = Pool(args.workers, initializer=_winit)
                    break
                played += 1
                batch.extend(AT.compute_gae(recs, res, critic,
                                            lam=args.gae_lambda))
        stats = ppo_update_fast(batch, actor, critic, aopt, copt,
                                args.ppo_eps, args.ppo_epochs,
                                args.entropy_beta, args.clip,
                                vhead, vopt, feat)
        if played - last_gate >= args.gate_every:
            last_gate = played
            wr = native_or_python_gate(args, spz, actor, extractor, penta,
                                       decks, args.gate_games)
            gps = played / (time.time() - t0)
            s = stats or {}
            log(f"GATE @{played} games: {100*wr:.1f}% | "
                f"critic_loss={s.get('critic_loss',0):.3f} "
                f"entropy={s.get('entropy',0):.3f} "
                f"G_mean={s.get('G_mean',0):.3f} "
                f"vhead={s.get('vhead_mse',0):.4f}/{s.get('base_mse',0):.4f} "
                f"{'capped=' + str(capped) if args.native else 'hangs=' + str(hangs)}"
                f" [{gps:.2f} g/s]",
                args.log)
            sd_np = {k: v.detach().numpy()
                     for k, v in actor.state_dict().items()}
            np.savez(f"{args.save_prefix}_actor.npz", **sd_np)   # latest
            # Checkpoint the CRITIC too. It is privileged (both seats'
            # redacted observations) so it can never deploy as a live
            # actor -- but it is trained on real value targets, which the
            # actor is not: the actor only ever sees a softmax over one
            # decision's afterstates, so its logit has no absolute scale
            # (see calibrate_aac_spzw.py). That makes the critic the
            # candidate leaf evaluator for determinized search, where a
            # sampled world supplies both hands by construction and using
            # it stays inside what the bot is allowed to see. Until now every run discarded it at exit.
            cr_np = {k: v.detach().numpy()
                     for k, v in critic.state_dict().items()}
            np.savez(f"{args.save_prefix}_critic.npz", **cr_np)
            np.savez(f"{args.save_prefix}_vhead.npz",
                     **{k: v.detach().numpy()
                        for k, v in vhead.state_dict().items()})
            if wr > best_wr:
                best_wr = wr
                np.savez(f"{args.save_prefix}_best.npz", **sd_np)
                np.savez(f"{args.save_prefix}_best_critic.npz", **cr_np)
                log(f"  new best {100*wr:.1f}% -> {args.save_prefix}_best.npz",
                    args.log)
    tail = f"{capped} capped" if args.native else f"{hangs} hangs"
    log(f"PAR-AAC complete: {played} games ({tail}), "
        f"best={100*best_wr:.1f}%", args.log)


if __name__ == "__main__":
    try:
        mp.set_start_method("fork")
    except RuntimeError:
        pass
    main()
