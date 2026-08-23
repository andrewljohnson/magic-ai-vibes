#!/usr/bin/env python3
"""AlphaZero loop: self-play by search, train toward the search, repeat.

    PENTA_ENGINE_DIR=engine-0.7.0 .venv-torch/bin/python az_train.py \
        --rounds 200 --games 48 --iters 32 --log az.log --save-prefix az

WHY THIS EXISTS. Plain self-play has no policy improvement operator: the
actor trains toward its own sampled advantage, nothing ratchets, and
measured here it drifts DOWN (52.5% -> 45.0% over 7.7k games). Search IS
an operator -- it produces a better move distribution than the raw policy
(53.1% vs 50.7% at 1-ply) -- so training the policy toward the search's
visit distribution is what turns self-play into improvement.

THE NETS.
  policy  (state, action) -> score.  Factorised so the state half is
          computed ONCE per search node and only a small per-action term
          repeats. This is what made search 224x faster (1790ms -> 8ms per
          decision); see spz-core/src/action_feat.rs.
  value   state -> P(win).  Trained on game outcomes.

Both start RANDOM. That is the cold start and it is the honest test: if
the loop works, search with a weak prior still beats the weak prior, and
training on its output pulls the policy up. Expect the first evaluations
to look terrible -- a random policy loses to the built-in bot roughly 4%
of the time, and the curve has to climb from there.
"""
import argparse
import json
import os
import sys
import time

import numpy as np
import torch
import torch.nn as nn

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from extractor import Extractor, pinned_catalog  # noqa: E402
from hosted_policy import load_decklists  # noqa: E402
import aac_native as AN  # noqa: E402

DEV = torch.device("cpu")


class PolicyHead(nn.Module):
    """score(state, action) = w2 . tanh(Ws.state + Wa.action + b1) + b2.

    The factorisation is not a detail -- it is the whole point. `Ws.state`
    is one matmul per search node; `Wa.action` is a small matmul per legal
    action. The alternative (score the afterstate) costs a game clone and a
    1081-feature extraction per action, measured at 13.15 ms each.
    """

    def __init__(self, state_dim, action_dim, hidden=128):
        super().__init__()
        self.ws = nn.Linear(state_dim, hidden, bias=True)
        self.wa = nn.Linear(action_dim, hidden, bias=False)
        self.out = nn.Linear(hidden, 1)
        # SMALL INIT, deliberately. The search softmaxes these scores into
        # PUCT priors, so a head with ordinary torch init produces peaked
        # priors from the very first game -- the tree then drives down one
        # line, each iteration simulating many plies, and a single episode
        # that should take 0.7s takes over a minute. Near-zero outputs mean
        # near-uniform priors, which is both fast AND what a cold start
        # should believe: nothing yet.
        #
        # This is the third time output SCALE, not learning, has been the
        # bug here (the actor's logits saturating the sigmoid; the value
        # head double-squashed; now this). Check the range.
        for p in (self.ws.weight, self.wa.weight, self.out.weight):
            nn.init.uniform_(p, -0.01, 0.01)
        nn.init.zeros_(self.ws.bias)
        nn.init.zeros_(self.out.bias)

    def forward(self, state, actions, counts):
        """state (N,S); actions (R,A); counts (N,) actions per state."""
        pre = self.ws(state)                      # (N,H) -- once per state
        seg = torch.repeat_interleave(
            torch.arange(len(counts), device=state.device), counts)
        h = torch.tanh(pre[seg] + self.wa(actions))
        return self.out(h).squeeze(-1)            # (R,)


class ValueHead(nn.Module):
    def __init__(self, state_dim, hidden=128):
        super().__init__()
        self.f1 = nn.Linear(state_dim, hidden)
        self.f2 = nn.Linear(hidden, 1)

    def forward(self, x):
        return self.f2(torch.tanh(self.f1(x))).squeeze(-1)


def export_policy(net, path, state_dim, action_dim):
    """Flat binary the Rust PolicyHead reads: three u64 dims, then
    ws, b1, wa, w2, b2 as f64."""
    sd = net.state_dict()
    hidden = sd["out.weight"].shape[1]
    with open(path, "wb") as f:
        f.write(np.array([hidden, state_dim, action_dim],
                         dtype="<u8").tobytes())
        for k in ("ws.weight", "ws.bias", "wa.weight", "out.weight"):
            f.write(np.ascontiguousarray(
                sd[k].detach().numpy(), dtype="<f8").tobytes())
        f.write(np.array([float(sd["out.bias"][0])], dtype="<f8").tobytes())


def export_value(net, path):
    sd = net.state_dict()
    hidden, inputs = sd["f1.weight"].shape
    with open(path, "wb") as f:
        f.write(np.array([hidden, inputs], dtype="<u8").tobytes())
        for k in ("f1.weight", "f1.bias", "f2.weight"):
            f.write(np.ascontiguousarray(
                sd[k].detach().numpy(), dtype="<f8").tobytes())
        f.write(np.array([float(sd["f2.bias"][0])], dtype="<f8").tobytes())


def log(msg, path):
    line = f"[{time.strftime('%H:%M:%S')}] {msg}"
    print(line, flush=True)
    if path:
        with open(path, "a") as f:
            f.write(line + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rounds", type=int, default=200)
    ap.add_argument("--games", type=int, default=48, help="games per round")
    ap.add_argument("--iters", type=int, default=32, help="search iterations")
    ap.add_argument("--threads", type=int, default=12)
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--epochs", type=int, default=4)
    ap.add_argument("--max-actions", type=int, default=64)
    ap.add_argument("--gate-every", type=int, default=10)
    ap.add_argument("--gate-games", type=int, default=200)
    ap.add_argument("--log", default="az.log")
    ap.add_argument("--save-prefix", default="az")
    args = ap.parse_args()

    spz = AN.load_spz_core()
    if not hasattr(spz, "az_stream_episodes"):
        raise SystemExit("spz_core lacks az_stream_episodes; rebuild it")
    ex = Extractor(version=2, belief=True)
    catalog = json.dumps(pinned_catalog())
    decks = list(load_decklists())
    state_dim = ex.size
    action_dim = spz.az_action_dim(catalog, True)

    policy = PolicyHead(state_dim, action_dim, args.hidden).to(DEV)
    value = ValueHead(state_dim, args.hidden).to(DEV)
    popt = torch.optim.Adam(policy.parameters(), lr=args.lr)
    vopt = torch.optim.Adam(value.parameters(), lr=args.lr)

    pol_path = os.path.join(HERE, f"{args.save_prefix}_policy.azp")
    val_path = os.path.join(HERE, f"{args.save_prefix}_value.spzw")
    export_policy(policy, pol_path, state_dim, action_dim)
    export_value(value, val_path)

    log(f"AZ cold start: rounds={args.rounds} games/round={args.games} "
        f"iters={args.iters} hidden={args.hidden} "
        f"state_dim={state_dim} action_dim={action_dim}", args.log)

    seed = 1
    for rnd in range(args.rounds):
        specs = []
        for g in range(args.games):
            d1 = decks[(seed + g) % len(decks)]
            d2 = decks[(seed + g * 7 + 3) % len(decks)]
            specs.append((d1, d2, seed * 1000 + g))
        seed += 1
        t0 = time.time()
        out = spz.az_stream_episodes(
            catalog, val_path, pol_path, args.iters, 1.5,
            "builtin-decklists.json", args.max_actions, args.threads, specs)
        (cand_b, rec, vis_b, priv_b, seat, ep_rec, ep_res, feat) = out
        gen = time.time() - t0

        n = len(rec) // 3
        if n == 0:
            log(f"round {rnd}: no records", args.log)
            continue
        m = np.asarray(rec[:n], dtype=np.int64)
        state = np.frombuffer(priv_b, dtype="<f4").reshape(-1, 2 * feat)
        acts = np.frombuffer(cand_b, dtype="<f4").reshape(-1, action_dim)
        visits = np.frombuffer(vis_b, dtype="<u4").astype(np.float32)

        # value target: the seat's outcome, broadcast over its records
        z = np.zeros(n, dtype=np.float32)
        i = 0
        for e, k in enumerate(ep_rec):
            r = int(ep_res[e])
            for _ in range(int(k)):
                if r == 2 or r < 0:
                    z[i] = 0.5
                else:
                    z[i] = 1.0 if r == seat[i] else 0.0
                i += 1

        st = torch.as_tensor(state[:, :feat])       # own view only
        at = torch.as_tensor(np.ascontiguousarray(acts))
        ct = torch.as_tensor(m)
        zt = torch.as_tensor(z)
        segs = torch.repeat_interleave(torch.arange(n), ct)
        # policy target: normalised visit counts
        vt = torch.as_tensor(visits)
        vsum = torch.zeros(n).scatter_add(0, segs, vt)
        pi = vt / vsum[segs].clamp(min=1e-9)

        for _ in range(args.epochs):
            logits = policy(st, at, ct)
            mx = torch.full((n,), -1e30).scatter_reduce(
                0, segs, logits, reduce="amax", include_self=True).detach()
            e = (logits - mx[segs]).exp()
            s = torch.zeros(n).scatter_add(0, segs, e)
            logp = (logits - mx[segs]) - torch.log(s[segs] + 1e-12)
            ploss = -(pi * logp).sum() / n          # cross-entropy to visits
            popt.zero_grad(); ploss.backward()
            nn.utils.clip_grad_norm_(policy.parameters(), 1.0); popt.step()

            vloss = nn.functional.binary_cross_entropy_with_logits(
                value(st), zt)
            vopt.zero_grad(); vloss.backward()
            nn.utils.clip_grad_norm_(value.parameters(), 1.0); vopt.step()

        export_policy(policy, pol_path, state_dim, action_dim)
        export_value(value, val_path)

        with torch.no_grad():
            ent = float(-(pi * logp).sum() / n)
            vmse = float(((torch.sigmoid(value(st)) - zt) ** 2).mean())
            base = float(((zt - zt.mean()) ** 2).mean())
        log(f"round {rnd}: {args.games} games {gen:.0f}s "
            f"({args.games/max(gen,1e-9):.2f} g/s) {n} decisions | "
            f"pol_ce={float(ploss):.4f} ent={ent:.3f} "
            f"vmse={vmse:.4f}/{base:.4f}", args.log)


if __name__ == "__main__":
    main()
