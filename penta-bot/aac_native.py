#!/usr/bin/env python3
"""Python side of the NATIVE AAC self-play loop (spz-core `aac` module).

The Python trainer's throughput wall was episode GENERATION: per decision
it forked the game once per candidate action, round-tripped each afterstate
through protocol JSON, and re-extracted 1081 features in numpy -- inside a
fork() worker pool that a native engine stall could hang. `spz_core`'s
`aac_stream_episodes` does all of that natively across OS threads with the
GIL released, and hands back one round of trajectories as flat buffers.

This module is the thin adapter: weights out, records in. The records it
returns are exactly the dicts `aac_torch.compute_gae` and
`aac_torch_par.ppo_update_fast` already consume, so the LEARNER is
untouched -- Python still owns PPO, GAE, the critic, gating and
checkpointing. Only move generation crossed the language boundary.

Validated by `aac_lockstep.py`, which replays a native episode's chosen
actions through the Python path and compares features, privileged rows and
rewards bit-for-bit.

MAX_ACTIONS: the afterstate expansion costs one game fork + one feature
extraction PER LEGAL ACTION, so a decision's cost is linear in its action
count. Almost every decision offers a handful; a few (mass blocker or
combat-damage assignment) offer orders of magnitude more, and those few
decisions dominate wall clock. That is what the old fork-pool trainer saw
as a "native engine hang" -- the worker was not stuck, it was expanding an
enormous action list, and the watchdog killed it at 25s. Above the cap the
native runner scores a prefix, plays the best of it, and emits NO training
row (a softmax over a truncated candidate set is not the distribution the
actor actually sampled from, so it must not become a PPO target). Set
max_actions=0 to expand everything, which is what the lockstep check runs.
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

CATALOG = os.path.join(HERE, "pinned-catalog.json")
DECKLISTS = os.path.join(HERE, "builtin-decklists.json")

# Chosen from the measured action-count distribution; see MAX_ACTIONS above.
DEFAULT_MAX_ACTIONS = 64

# Whether the belief block gets the opponent's REAL decklist (open) or has
# to classify it from revealed cards. Measured classification accuracy is
# 76.6% overall but 0% on turn 1 / 53% turn 2 / 64% turn 3, so a quarter of
# decisions -- concentrated early -- run on the wrong decklist. The
# determinized/ISMCTS lineage has always used open decklists, which is why
# its 57.7% reference and a classified AAC number were never comparable.
# Neither setting ever reads the opponent's hidden HAND.
DEFAULT_OPEN_DECKLIST = False

_RESULT_NAME = {0: "p1", 1: "p2", 2: "draw", -1: None}


def load_spz_core():
    """Import the spz_core extension, with a build hint on failure."""
    try:
        import spz_core  # noqa: PLC0415
    except ImportError as exc:                       # pragma: no cover
        raise ImportError(
            "spz_core extension not importable. Build it with:\n"
            "  cd spz-core && cargo build --release && "
            "cp target/release/libspz_core.so ../spz_core.so") from exc
    if not hasattr(spz_core, "aac_stream_episodes"):
        raise ImportError(
            "spz_core is stale: no aac_stream_episodes. Rebuild it "
            "(cd spz-core && cargo build --release && "
            "cp target/release/libspz_core.so ../spz_core.so)")
    return spz_core


def flat_weights(actor):
    """(w1, b1, w2, b2) as flat float64 lists for the native actor.

    Mirrors `aac_torch_par.actor_weights`, but widened to f64 -- the
    native scorer accumulates in f64 exactly like the numpy worker's
    `_np_logits` did. float32 -> float64 is exact, so no weight value
    changes.
    """
    sd = actor.state_dict()
    w1 = sd["f1.weight"].detach().numpy().astype(np.float64)
    b1 = sd["f1.bias"].detach().numpy().astype(np.float64)
    w2 = sd["f2.weight"].detach().numpy().squeeze(0).astype(np.float64)
    b2 = float(sd["f2.bias"].detach().numpy()[0])
    return w1.ravel().tolist(), b1.tolist(), w2.tolist(), b2


def _catalog(catalog_json):
    if catalog_json is not None:
        return catalog_json
    with open(CATALOG) as f:
        return f.read()


def stream_episodes(spz_core, actor, hidden, belief, specs, temperature,
                    threads=0, max_actions=DEFAULT_MAX_ACTIONS,
                    open_decklist=False, catalog_json=None,
                    decklists_path=None):
    """Play `specs` natively; return (episodes, stats).

    `specs` is a list of (d1, d2, seed, handcrafted, learner_is_p1).
    `episodes` is a list of (records, result, final) triples shaped like
    `_worker_episode`'s return: each record is a dict with cand / chosen /
    logp_old / temp / seat / priv / r, and `result` is "p1" / "p2" /
    "draw" / None (None = the episode hit the 600-decision cap). `final`
    carries that episode's per-seat privileged row at the cut so the
    learner can bootstrap it instead of discarding it; it is None for
    episodes that finished normally.
    """
    w1, b1, w2, b2 = flat_weights(actor)
    (cand_buf, rec_u32, logp, logit_buf, priv_buf, reward,
     seats, ep_records, ep_result, ep_diag, feat, final_buf) = \
        spz_core.aac_stream_episodes(
            _catalog(catalog_json), decklists_path or DECKLISTS, belief,
            hidden, w1, b1, w2, b2, temperature, threads, max_actions,
            open_decklist, specs)

    n_rec = len(rec_u32) // 2
    cand_counts, chosen = rec_u32[:n_rec], rec_u32[n_rec:]
    # One [features(p1), features(p2)] block per TRUNCATED episode, in
    # order. These let compute_gae bootstrap V(s_T) rather than throw the
    # episode away -- see the truncation note there.
    final_all = np.frombuffer(final_buf, dtype="<f4").reshape(-1, 2 * feat)
    n_ep = len(ep_records)
    decisions = np.asarray(ep_diag[:n_ep], dtype=np.int64)
    widest = np.asarray(ep_diag[n_ep:], dtype=np.int64)

    cand_all = np.frombuffer(cand_buf, dtype="<f4")
    priv_all = np.frombuffer(priv_buf, dtype="<f4").reshape(-1, 2 * feat)
    counts = np.asarray(cand_counts, dtype=np.int64)
    # Row offsets into the concatenated candidate buffer.
    row_off = np.zeros(len(counts) + 1, dtype=np.int64)
    np.cumsum(counts, out=row_off[1:])
    temp = max(temperature, 1e-6)

    episodes = []
    r = 0
    t_idx = 0
    for e in range(n_ep):
        records = []
        for _ in range(int(ep_records[e])):
            m = int(counts[r])
            cand = cand_all[row_off[r] * feat:row_off[r + 1] * feat]
            records.append({
                "cand": cand.reshape(m, feat),
                "chosen": int(chosen[r]),
                "logp_old": float(logp[r]),
                "temp": temp,
                "seat": "p1" if seats[r] == 0 else "p2",
                "priv": priv_all[r],
                "r": float(reward[r]),
            })
            r += 1
        res = _RESULT_NAME[int(ep_result[e])]
        final = None
        if res is None and records and t_idx < len(final_all):
            # per-seat privileged row: own view first, then the other seat
            f = final_all[t_idx]
            final = {"p1": np.concatenate([f[:feat], f[feat:]]),
                     "p2": np.concatenate([f[feat:], f[:feat]])}
            t_idx += 1
        episodes.append((records, res, final))

    stats = {
        "records": int(len(counts)),
        "capped": int(sum(1 for x in ep_result if x == -1)),
        "max_decisions": int(decisions.max()) if n_ep else 0,
        "mean_decisions": float(decisions.mean()) if n_ep else 0.0,
        "widest_actions": int(widest.max()) if n_ep else 0,
        "mean_candidates": float(counts.mean()) if len(counts) else 0.0,
        "max_candidates": int(counts.max()) if len(counts) else 0,
        "logits": np.frombuffer(logit_buf, dtype="<f8"),
        "feat": feat,
    }
    return episodes, stats


def gate(spz_core, actor, hidden, belief, learner_deck, games, seed_base,
         threads=0, max_actions=DEFAULT_MAX_ACTIONS, open_decklist=False,
         catalog_json=None, decklists_path=None, full=False):
    """Evaluation, natively: actor argmax vs the handcrafted bot.

    Same protocol as `aac_torch_par.gate_belief` (alternating seats,
    rotating opponent decks, capped games scored as draws), but the games
    run across native threads instead of one Python loop -- so a 400-game
    confirmation stops costing more than the training round it validates.

    Returns the score rate, or (rate, decisions, widest, matchups) with
    full=True, where `matchups` is the per-opponent-deck breakdown. One
    aggregate hides a bot that crushes half the field and is unplayable
    into the other half -- which is the part worth fixing.
    """
    w1, b1, w2, b2 = flat_weights(actor)
    rate, decisions, widest, names, sums, counts = spz_core.aac_gate(
        _catalog(catalog_json), decklists_path or DECKLISTS, belief, hidden,
        w1, b1, w2, b2, learner_deck, games, seed_base, threads, max_actions,
        open_decklist)
    if full:
        matchups = [{"deck": n, "rate": (s / c if c else None), "games": c}
                    for n, s, c in zip(names, sums, counts)]
        return rate, np.asarray(decisions), np.asarray(widest), matchups
    return rate
