#!/usr/bin/env python3
"""Local training monitor for the penta AAC runs.

    PENTA_ENGINE_DIR=engine-0.7.0 python3 monitor.py
    -> http://localhost:8899

Reads the trainer's own .log files (no separate telemetry writer, no build
step, no dependencies beyond the stdlib) and serves one page: a headline
number, the gate curve for every run, and a status table. It re-reads the
logs on each poll, so runs that start or finish appear on their own.

Replaces telemetry-watcher.py + penta-monitor.html, which parsed the older
chunk-N.log format from the determinized lineage and does not understand
"GATE @N games:" lines.

WHY THE TABLE EXISTS as well as the chart: the light-mode categorical
palette sits below 3:1 contrast on three of its slots, so the data-viz
relief rule requires visible direct labels or a table view. This ships
both, and every series is named next to a colour chip so identity is never
carried by colour alone.
"""
import glob
import json
import os
import re
import statistics
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = int(os.environ.get("PENTA_MONITOR_PORT", "8899"))

# A killed run never writes "PAR-AAC complete", so completion alone cannot
# tell live from dead. But a fixed idle threshold is wrong too: a gate lands
# every --gate-every games, which at 2-5 g/s is 10-25 MINUTES, so any short
# threshold reports healthy runs as stopped between gates. Instead, judge a
# run against ITS OWN observed cadence: stale only once it has been quiet for
# several times its typical gate interval.
STALE_FLOOR_SECS = 1800     # never call a run dead sooner than this
STALE_INTERVALS = 3.0       # ... or before 3x its own gate spacing

# A single gate is not a result. Every gate line states its OWN standard
# error ("37.9% +/- 4.4"), which is parsed and used; this is only the
# fallback for old AAC lines that did not print one. It is deliberately the
# error of a ~120-game gate, the size this loop actually runs -- the 2.5
# that sat here described 400-game gates nobody runs any more and made
# every mean look four times more certain than it was.
GATE_SE = 4.6
# PARITY with the built-in bot is 50% -- the gate scores OUR win rate in
# head-to-head games against it, so below 50% is losing to it.
#
# A BASELINE of 31.6 sat here for months labelled "the bar the project set
# out to beat". It was the lower bound of a 95% confidence interval from
# the abandoned C++ alpha-sim, for a LEARNED challenger vs handcrafted --
# never the handcrafted bot's own score, which is 50% against itself by
# symmetry. It made every run look ~18 points better than it was.
BASELINE = 50.0        # parity with the built-in bot
TARGET = 50.0

TS_RE = re.compile(r"^\[(\d\d):(\d\d):(\d\d)\]")
GATE_RE = re.compile(
    r"GATE @(\d+) games: ([0-9.]+)%"
    r"(?:.*?critic_loss=([0-9.]+))?"
    r"(?:.*?entropy=([0-9.]+))?"
    r"(?:.*?G_mean=([0-9.-]+))?"
    r"(?:.*?capped=(\d+))?"
    r"(?:.*?\[([0-9.]+) g/s\])?")
GATE0_RE = re.compile(r"GATE @(\d+) games: actor vs handcrafted = ([0-9.]+)%")
MATCH_RE = re.compile(r"MATCHUPS (.+)")
START_RE = re.compile(r"PAR-AAC start: (.*)")
# --- AlphaZero loop (az_train.py) ---------------------------------------
# Different shape from the AAC logs: strength arrives on its own GATE line
# every --gate-every rounds, while the per-round line carries throughput
# and the health metrics. Both are folded into the SAME gate dict the AAC
# path produces, so the chart, table and small multiples need no changes.
AZ_START_RE = re.compile(r"AZ cold start: (.*)")
AZ_ROUND_RE = re.compile(
    r"round (\d+): (\d+) games \d+s \(([0-9.]+) g/s\).*?"
    r"fin=(\d+)%.*?pol_ent=([0-9.]+).*?tgt_ent=([0-9.]+).*?"
    r"vmse=([0-9.]+)/([0-9.]+)")
# GATE round 179: 37.9% +/- 4.4 (capped-as-draw 38.3%) (45W 1D 74L / 120, 1 capped)
AZ_GATE_RE = re.compile(
    r"GATE round (\d+): ([0-9.]+)%"
    r"(?:\s*\+/-\s*([0-9.]+))?"
    r"(?:.*?capped-as-draw ([0-9.]+)%)?"
    r"(?:.*?\((\d+)W (\d+)D (\d+)L / (\d+)(?:, (\d+) capped)?\))?")
# The net this run started from, re-gated before round 0. Training has to
# beat THIS, not merely its own first gate, so the chart draws it as a floor.
AZ_FLOOR_RE = re.compile(
    r"GATE warm-start baseline: ([0-9.]+)%(?:\s*\+/-\s*([0-9.]+))?")
# KNOWN BROKEN as of 2026-08-26: the comparison behind this line did not
# actually play the two nets against each other, so the percentage means
# nothing yet. Parsed and shown so the repair can be watched, but never
# presented as a measurement -- see the "suspect" styling in the page.
AZ_MIRROR_RE = re.compile(
    r"MIRROR round (\d+): current vs best ([0-9.]+)% over (\d+) games")
DONE_RE = re.compile(r"PAR-AAC complete: (\d+) games \(([^)]*)\)")


def live_prefixes():
    """save-prefix values of trainer processes running right now.

    Definitive where it works, which beats guessing from file mtime: a log
    can be touched by unrelated repo work, and a slow historical run's gate
    cadence buys it hours of grace under the idle heuristic. Reads /proc
    directly to stay dependency-free; on a platform without /proc this
    returns None and callers fall back to the idle heuristic.
    """
    if not os.path.isdir("/proc"):
        return None
    out = set()
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            with open(f"/proc/{pid}/cmdline", "rb") as f:
                argv = f.read().split(b"\0")
        except OSError:
            continue                      # process exited mid-scan
        for i, a in enumerate(argv):
            if a == b"--save-prefix" and i + 1 < len(argv):
                out.add(argv[i + 1].decode(errors="replace"))
            # A run is named after its LOG here, and --log need not match
            # --save-prefix. Matching both stops a live run being drawn as
            # "stopped" just because the two flags disagree.
            elif a == b"--log" and i + 1 < len(argv):
                name = os.path.basename(argv[i + 1].decode(errors="replace"))
                out.add(name[:-4] if name.endswith(".log") else name)
    return out


def parse_log(path):
    name = os.path.basename(path)[:-4]
    gates, config, done = [], "", None
    az, az_games, az_last = False, 0, {}
    az_hist = []
    floor, mirror = None, []
    stamps = []
    matchups = []
    try:
        with open(path, errors="replace") as f:
            for line in f:
                m = AZ_START_RE.search(line)
                if m:
                    config = m.group(1).strip()
                    az = True
                    continue
                if az:
                    m = AZ_ROUND_RE.search(line)
                    if m:
                        az_games += int(m.group(2))
                        az_hist.append((az_games, float(m.group(3)),
                                        int(m.group(4)), float(m.group(5)),
                                        float(m.group(6))))
                        az_last = {
                            "gps": float(m.group(3)),
                            "fin": int(m.group(4)),
                            "entropy": float(m.group(5)),
                            "tgt_ent": float(m.group(6)),
                            "vmse": float(m.group(7)),
                            "vbase": float(m.group(8)),
                        }
                        ts = TS_RE.match(line)
                        if ts:
                            h, mi, sec = (int(x) for x in ts.groups())
                            stamps.append(h * 3600 + mi * 60 + sec)
                        continue
                    m = MATCH_RE.search(line)
                    if m:
                        matchups = []
                        for pair in m.group(1).split():
                            if "=" in pair:
                                k, v = pair.rsplit("=", 1)
                                try:
                                    matchups.append(
                                        (k.replace("_", " "), float(v)))
                                except ValueError:
                                    pass
                        continue
                    m = AZ_FLOOR_RE.search(line)
                    if m:
                        floor = {"pct": float(m.group(1)),
                                 "se": float(m.group(2)) if m.group(2)
                                 else None}
                        continue
                    m = AZ_MIRROR_RE.search(line)
                    if m:
                        mirror.append({"round": int(m.group(1)),
                                       "pct": float(m.group(2)),
                                       "games": int(m.group(3))})
                        continue
                    m = AZ_GATE_RE.search(line)
                    if m:
                        n = int(m.group(8)) if m.group(8) else None
                        capped = int(m.group(9)) if m.group(9) else None
                        gates.append({
                            "round": int(m.group(1)),
                            "games": az_games, "pct": float(m.group(2)),
                            "se": float(m.group(3)) if m.group(3) else None,
                            "draw_pct": (float(m.group(4)) if m.group(4)
                                         else None),
                            "w": int(m.group(5)) if m.group(5) else None,
                            "d": int(m.group(6)) if m.group(6) else None,
                            "l": int(m.group(7)) if m.group(7) else None,
                            "n": n,
                            # vmse against its own baseline is this loop's
                            # analogue of critic_loss: below 1.0 means the
                            # value head beats predicting the mean.
                            "closs": (az_last.get("vmse", 0.0)
                                      / max(az_last.get("vbase", 1e-9), 1e-9)
                                      if az_last else None),
                            "entropy": az_last.get("entropy"),
                            # tgt_ent is the search-health metric: it should
                            # start near 1.0 and FALL as the value head
                            # learns. Flat near 1.0 means search has no
                            # opinion; pinned at 0.0 means it is not
                            # branching at all.
                            "gmean": az_last.get("tgt_ent"),
                            "capped": capped,
                            # An AZ gate reports capped games for THAT gate,
                            # not a running total, so it is already a rate --
                            # the differencing below would turn it to noise.
                            "capped_pct": (round(100.0 * capped / n, 1)
                                           if capped is not None and n
                                           else None),
                            "gps": az_last.get("gps"),
                            "ts": stamps[-1] if stamps else None})
                        continue
                    continue
                m = MATCH_RE.search(line)
                if m:
                    matchups = []
                    for pair in m.group(1).split():
                        if "=" in pair:
                            k, v = pair.rsplit("=", 1)
                            try:
                                matchups.append((k.replace("_", " "), float(v)))
                            except ValueError:
                                pass
                    continue
                m = START_RE.search(line)
                if m:
                    config = m.group(1).strip()
                    continue
                m = DONE_RE.search(line)
                if m:
                    done = f"{int(m.group(1)):,} games ({m.group(2)})"
                    continue
                m = GATE0_RE.search(line)
                if m:
                    gates.append({"games": int(m.group(1)),
                                  "pct": float(m.group(2)), "closs": None,
                                  "entropy": None, "gmean": None,
                                  "capped": None, "gps": None, "ts": None,
                                  "se": None, "n": None})
                    continue
                m = GATE_RE.search(line)
                if m:
                    ts = TS_RE.match(line)
                    if ts:
                        h, mi, sec = (int(x) for x in ts.groups())
                        stamps.append(h * 3600 + mi * 60 + sec)
                    g = {"games": int(m.group(1)), "pct": float(m.group(2)),
                         "closs": float(m.group(3)) if m.group(3) else None,
                         "entropy": float(m.group(4)) if m.group(4) else None,
                         "gmean": float(m.group(5)) if m.group(5) else None,
                         "capped": int(m.group(6)) if m.group(6) else None,
                         "gps": float(m.group(7)) if m.group(7) else None,
                         "ts": stamps[-1] if stamps else None,
                         "se": None, "n": None}
                    gates.append(g)
        mtime = os.path.getmtime(path)
    except OSError:
        return None
    # A run with no gate YET is still a live run worth showing. AZ gates
    # every --gate-every rounds, so a fresh run had no gate for ~30 minutes
    # and was invisible here -- leaving the page showing only long-dead
    # runs, which reads exactly like stale data.
    if not gates and not az_games:
        return None
    # The @0 gate is only 40 games; keep it on the curve but never let it
    # into a mean, or it drags the number around by pure noise.
    scored = [g for g in gates if g["games"] > 0]
    pcts = [g["pct"] for g in scored]
    recent = pcts[-8:]
    # Derived per-interval series. The logged g/s is a CUMULATIVE average
    # (games since start / elapsed since start), so it only ever decays
    # smoothly and hides what a run is doing right now. Differencing games
    # against the log timestamps gives the instantaneous rate, which is what
    # reveals contention, a wedged run, or games getting longer as the actor
    # strengthens. Same for `capped`, which is logged as a running total.
    prev = None
    for g in gates:
        g["rate"] = None
        g.setdefault("capped_pct", None)
        if prev and g["ts"] is not None and prev["ts"] is not None:
            dt = (g["ts"] - prev["ts"]) % 86400
            dg = g["games"] - prev["games"]
            if dt > 0 and dg > 0:
                g["rate"] = round(dg / dt, 2)
            if (g["capped_pct"] is None and dg > 0
                    and g["capped"] is not None
                    and prev["capped"] is not None):
                g["capped_pct"] = round(
                    100.0 * (g["capped"] - prev["capped"]) / dg, 1)
        if g["games"] > 0:
            prev = g

    # A run's name says nothing about what it is testing, and after a few
    # experiments nobody remembers which prefix meant what. Derive a short
    # descriptor from the config the trainer logged at startup.
    def cfg(pat, default=None):
        m = re.search(pat, config)
        return m.group(1) if m else default
    decklist = cfg(r"decklist=(\w+)")
    bits = []
    # An AZ run's config has none of the AAC keys, so without this it would
    # render with a blank label. Name what actually distinguishes it.
    if az:
        bits.append("AlphaZero")
        if cfg(r"iters=(\d+)"):
            bits.append(cfg(r"iters=(\d+)") + " sims")
        if cfg(r"hidden=(\d+)"):
            bits.append("h" + cfg(r"hidden=(\d+)"))
        return_label = " · ".join(bits)
    else:
        return_label = None
    if decklist:
        bits.append("open decklists" if decklist.upper() == "OPEN"
                    else "classified deck")
    if cfg(r"hidden=(\d+)"):
        bits.append("h" + cfg(r"hidden=(\d+)"))
    for pat, fmt in ((r"ent=([0-9.]+)", "ent {}"),
                     (r"gae_lam=([0-9.]+)", "λ {}"),
                     (r"a_lr=([0-9.e-]+)", "lr {}")):
        v = cfg(pat)
        if v and v not in ("0.01", "0.95", "0.001"):   # only note non-defaults
            bits.append(fmt.format(v))
    label = return_label if return_label is not None else " · ".join(bits)

    idle = time.time() - mtime
    # median spacing between this run's own gates (wrapping midnight)
    deltas = sorted((b - a) % 86400 for a, b in zip(stamps, stamps[1:]))
    cadence = deltas[len(deltas) // 2] if deltas else 0
    stale_after = max(STALE_FLOOR_SECS, STALE_INTERVALS * cadence)
    return {
        "name": name,
        "config": config,
        "label": label,
        "matchups": matchups,
        "done": done,
        "stale": done is None and idle > stale_after,
        "idle_min": int(idle // 60),
        "cadence_min": round(cadence / 60, 1) if cadence else None,
        "gates": gates,
        # Games ACTUALLY played, not games at the last chartable point. An
        # AZ run only emits a gate every --gate-every rounds, so reading
        # the count off gates[-1] under-reported it by up to that many
        # rounds -- 960 shown against 1,824 played.
        "games": max(gates[-1]["games"] if gates else 0, az_games),
        # Round-level progress, so a run that has not gated yet is still
        # visible. The chart plots gates only, and with --gate-every 20
        # that left a fresh run looking like an empty page next to a table
        # of finished runs.
        "live": az_last,
        "hist": az_hist[-60:],
        "n": len(pcts),
        "last": pcts[-1] if pcts else None,
        "best": max(pcts) if pcts else None,
        "mean": statistics.fmean(pcts) if pcts else None,
        "recent": statistics.fmean(recent) if recent else None,
        "recent_n": len(recent),
        # SE of the MEAN of n gates, built from each gate's own reported
        # error rather than one hard-coded constant.
        "se": ((sum((g["se"] or GATE_SE) ** 2 for g in scored) ** 0.5)
               / len(scored)) if scored else None,
        # Games behind ONE gate -- the page used to claim 400 in prose while
        # the loop ran 120, which is a different result entirely.
        "gate_games": next((g["n"] for g in reversed(scored)
                            if g.get("n")), None),
        "floor": floor,
        # Deliberately last and deliberately unused by any headline: the
        # mirror comparison is broken upstream, so the page shows it only
        # under a "suspect" label.
        "mirror": mirror[-12:],
        "entropy": next((g["entropy"] for g in reversed(scored)
                         if g["entropy"] is not None), None),
        "gps": next((g["gps"] for g in reversed(scored)
                     if g["gps"] is not None), None),
    }


def playout_report():
    """What playout_log.py found watching the bot actually play.

    A gate reports one number per 300 games. It cannot tell you the bot is
    pointing Fireball at its own face or sitting on a free Mox for six
    turns -- those are obvious in one game and invisible in an aggregate.
    """
    path = os.path.join(HERE, "playout_report.json")
    try:
        with open(path) as f:
            rep = json.load(f)
    except (OSError, ValueError):
        return {"available": False}
    rep["available"] = True
    rep["mtime"] = os.path.getmtime(path)
    rep["meta"] = {k: list(v) for k, v in FLAG_META.items()}
    rep["transcripts"] = sorted(
        os.path.basename(p) for p in
        glob.glob(os.path.join(HERE, "playouts", "game-*.txt")))
    return rep


# How bad is each flag, and what does it mean? Severity drives the colour:
# "bug" is never correct play, "suspect" is usually wrong, "judgement" is
# often defensible and reported only so the RATE can be watched.
FLAG_META = {
    "discarded_a_playable_land": (
        "bug", "Discarded a land it could have played",
        "Discarding a land while the turn's land drop is still unused. The "
        "card leaves hand either way, so playing it is strictly better -- it "
        "leaves a permanent behind. Reported from hosted play (Mishra's "
        "Factory discarded on a turn it could have been played)."),
    "x_spell_cast_for_zero": (
        "bug", "X spell cast for X=0",
        "An X spell cast with X=0 deals nothing and the card is spent. "
        "There is no board state where this is correct."),
    "damage_pointed_at_self": (
        "bug", "Damage aimed at our own side",
        "A damage spell or ability targeting our own face or our own "
        "permanent."),
    "buffed_opponent_creature": (
        "bug", "Pumped an opponent's creature",
        "A +X/+X effect pointed at a creature they control. That is simply "
        "helping them."),
    "kept_hand_with_no_mana": (
        "bug", "Kept an opening hand with no mana at all",
        "No land, no Mox, no Black Lotus. The hand cannot cast anything; "
        "this is an automatic mulligan."),
    "no_attack_into_undefended": (
        "bug", "Declined to attack an undefended opponent",
        "Attackers were available and they had NO untapped creatures, so "
        "the damage was free."),
    "attacked_into_bigger_blocker": (
        "bug", "Attacked into a strictly better blocker",
        "Every untapped creature they have kills our attacker and survives "
        "it, and ours has no evasion they cannot match. Occasionally right "
        "if you hold a trick or read them as unwilling to block -- but as a "
        "rate it should be near zero."),
    "skipped_land_drop": (
        "suspect", "Land drop skipped entirely",
        "Had a land in hand and the drop available, and ended the turn "
        "without playing one."),
    "held_free_mana_artifact": (
        "suspect", "Passed main holding a FREE mana artifact",
        "A Mox or Lotus costs nothing to play and only adds mana. Holding "
        "one through your own main phase gives up mana for no gain."),
}



# ======================================================================
# HOSTED PLAY (hosted_bot.py against penta.lacker.workers.dev)
#
# Training is one thing; the bot actually being up and playing strangers
# is another, and nothing here reported it. The three failure modes worth
# a panel, in order of how quietly they happen:
#
#   1. The daemon died. Nothing plays; the site just stops offering us.
#   2. The daemon is alive but the server does not list us online -- the
#      registration lapsed, or heartbeats stopped landing.
#   3. Worst, because it is invisible from outside: search throws on every
#      move and HostedPolicy silently falls back to the first legal action.
#      That is a 1-ply bot wearing our name. It printed "SEARCH FAILED"
#      into hosted_bot.out and nobody was reading hosted_bot.out.
# ======================================================================
HOSTED_SERVER = os.environ.get("PENTA_HOSTED_SERVER",
                               "https://penta.lacker.workers.dev")
HOSTED_OUT = os.path.join(HERE, "hosted_bot.out")
HOSTED_STATE = os.path.join(HERE, "hosted_bot_state.json")
HOSTED_GAMES = os.path.join(HERE, "hosted-games")
SEARCH_FAILED = "SEARCH FAILED"
BOTS_TTL = 25          # seconds to cache the server's bot listing
GAMES_SHOWN = 12       # most recent transcripts rendered as rows
GAMES_SCANNED = 400    # cap the directory walk; transcripts are large

_bots_cache = {"at": 0.0, "value": None}
_game_cache = {}       # path -> (mtime, size, summary, [ms])
# hosted_bot.out only ever appends, and it runs for days. Re-reading the
# whole thing every poll is wasted work that grows without bound, so the
# scan resumes from the last offset and resets only if the file is replaced.
_out_scan = {"pos": 0, "failed": 0, "heartbeat": None, "tail": [],
             "examples": []}
# The server is threaded so one slow /_bots fetch cannot stall the page,
# which means two pollers can land in here at once. The incremental log
# scan is not re-entrant -- both would count the same SEARCH FAILED lines.
_hosted_lock = threading.Lock()


def _hosted_proc():
    """The running hosted_bot.py, if any: (pid, argv-tail) or None."""
    if not os.path.isdir("/proc"):
        return None
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            with open(f"/proc/{pid}/cmdline", "rb") as f:
                argv = [a.decode(errors="replace")
                        for a in f.read().split(b"\0") if a]
        except OSError:
            continue
        if any(a.endswith("hosted_bot.py") for a in argv):
            try:
                started = os.path.getmtime(f"/proc/{pid}")
            except OSError:
                started = None
            return {"pid": int(pid),
                    "args": " ".join(argv[1:])[:200],
                    "started": started}
    return None


def _bots_listing():
    """GET /_bots, cached. Never raises: the network is not the dashboard."""
    now = time.time()
    if _bots_cache["value"] is not None and now - _bots_cache["at"] < BOTS_TTL:
        return _bots_cache["value"]
    out = {"ok": False, "error": None, "bots": []}
    try:
        req = urllib.request.Request(f"{HOSTED_SERVER}/_bots",
                                     headers={"User-Agent": "penta-monitor"})
        with urllib.request.urlopen(req, timeout=6) as r:
            doc = json.loads(r.read().decode())
        out = {"ok": True, "error": None,
               "bots": doc.get("bots") or [],
               "protocol": (doc.get("compatibility") or {})
               .get("protocolVersion")}
    except Exception as error:                # noqa: BLE001 - any failure
        out["error"] = f"{type(error).__name__}: {error}"[:160]
    _bots_cache.update(at=now, value=out)
    return out


def _scan_game(path):
    """Summarise one transcript, cached on (mtime, size).

    Deliberately does NOT json-parse the move rows: each one embeds a full
    observation, so a finished game is ~160KB and a naive parse of every
    poll would cost more than everything else on this page combined. Only
    the short result/error rows are parsed; latency is pulled out with a
    substring scan.
    """
    try:
        st = os.stat(path)
    except OSError:
        return None
    hit = _game_cache.get(path)
    if hit and hit[0] == st.st_mtime and hit[1] == st.st_size:
        return hit[2], hit[3]
    summary = {"file": os.path.basename(path), "mtime": st.st_mtime,
               "outcome": None, "message": None, "moves": 0, "seconds": None,
               "abandoned": False, "errors": 0, "slow": 0, "no_worlds": 0,
               "med_ms": None, "max_ms": None}
    ms = []
    try:
        with open(path, errors="replace") as f:
            for line in f:
                if '"t": "move"' in line:
                    summary["moves"] += 1
                    i = line.find('"ms": ')
                    if i >= 0:
                        j = i + 6
                        k = j
                        while k < len(line) and line[k].isdigit():
                            k += 1
                        if k > j:
                            ms.append(int(line[j:k]))
                    if '"worlds": 0' in line or '"worlds": null' in line:
                        summary["no_worlds"] += 1
                    continue
                if '"t": "result"' in line:
                    try:
                        row = json.loads(line)
                    except ValueError:
                        continue
                    res = row.get("result") or {}
                    summary["outcome"] = res.get("outcome")
                    summary["message"] = res.get("message")
                    summary["seconds"] = row.get("seconds")
                elif '"t": "abandoned"' in line:
                    summary["abandoned"] = True
                    try:
                        summary["seconds"] = json.loads(line).get("seconds")
                    except ValueError:
                        pass
                elif '"t": "slow-move"' in line:
                    summary["slow"] += 1
                elif "-error" in line:
                    summary["errors"] += 1
    except OSError:
        return None
    if ms:
        ordered = sorted(ms)
        summary["med_ms"] = ordered[len(ordered) // 2]
        summary["max_ms"] = ordered[-1]
    _game_cache[path] = (st.st_mtime, st.st_size, summary, ms)
    return summary, ms


def _pct(values, q):
    if not values:
        return None
    ordered = sorted(values)
    i = min(len(ordered) - 1, int(q * len(ordered)))
    return ordered[i]


def hosted_status():
    """Everything the hosted panel shows. Cheap enough for a 20s poll."""
    with _hosted_lock:
        return _hosted_status()


def _hosted_status():
    proc = _hosted_proc()
    state = {}
    try:
        with open(HOSTED_STATE) as f:
            state = json.load(f)
    except (OSError, ValueError):
        pass
    name = state.get("name")

    # --- the log: heartbeats, and the one string that matters -----------
    log_mtime = None
    try:
        st = os.stat(HOSTED_OUT)
        log_mtime = st.st_mtime
        if st.st_size < _out_scan["pos"]:        # truncated or replaced
            _out_scan.update(pos=0, failed=0, heartbeat=None, tail=[],
                             examples=[])
        if st.st_size > _out_scan["pos"]:
            with open(HOSTED_OUT, errors="replace") as f:
                f.seek(_out_scan["pos"])
                for line in f:
                    if SEARCH_FAILED in line:
                        _out_scan["failed"] += 1
                        if len(_out_scan["examples"]) < 4:
                            _out_scan["examples"].append(line.strip()[:200])
                    if line.startswith("heartbeat ok"):
                        _out_scan["heartbeat"] = line.strip()
                    _out_scan["tail"].append(line.rstrip("\n")[:200])
                    if len(_out_scan["tail"]) > 60:
                        del _out_scan["tail"][:30]
                _out_scan["pos"] = f.tell()
    except OSError:
        pass
    failed = _out_scan["failed"]
    heartbeats = _out_scan["heartbeat"]
    fail_lines = list(_out_scan["examples"])
    tail = list(_out_scan["tail"])

    # --- the server's own view of us ------------------------------------
    listing = _bots_listing()
    mine = None
    for bot in listing["bots"]:
        if bot.get("id") == state.get("id") or (
                name and bot.get("name") == name):
            mine = bot
            break

    # --- transcripts ------------------------------------------------------
    paths = sorted(glob.glob(os.path.join(HOSTED_GAMES, "*.jsonl")))
    paths = paths[-GAMES_SCANNED:]
    games, all_ms = [], []
    for p in paths:
        got = _scan_game(p)
        if not got:
            continue
        summary, ms = got
        games.append(summary)
        all_ms.extend(ms)
    games.sort(key=lambda g: g["mtime"], reverse=True)
    finished = [g for g in games if g["outcome"]]
    wins = sum(1 for g in finished if g["outcome"] == "win")
    losses = sum(1 for g in finished if g["outcome"] == "loss")
    other = len(finished) - wins - losses
    # A game whose only row is the result never gave us a decision -- the
    # opponent walked before we moved. Counting those as play would flatter
    # the record, so they are reported on their own.
    walkovers = sum(1 for g in finished if g["moves"] == 0)
    return {
        "server": HOSTED_SERVER,
        "daemon": proc,
        "registration": {"name": name, "id": state.get("id"),
                         "deck": state.get("deck"),
                         "server": state.get("server")},
        "listing": {"ok": listing["ok"], "error": listing["error"],
                    "count": len(listing["bots"]), "me": mine,
                    "age": round(time.time() - _bots_cache["at"])},
        "search_failed": failed,
        "search_failed_examples": fail_lines,
        "heartbeat": heartbeats,
        "log_mtime": log_mtime,
        "log_tail": tail[-12:],
        "games": {"total": len(games), "finished": len(finished),
                  "win": wins, "loss": losses, "other": other,
                  "walkover": walkovers,
                  "abandoned": sum(1 for g in games if g["abandoned"]),
                  "errors": sum(g["errors"] for g in games),
                  "slow": sum(g["slow"] for g in games),
                  "pct": (100.0 * wins / len(finished)) if finished else None},
        "latency": {"moves": len(all_ms), "med": _pct(all_ms, 0.5),
                    "p90": _pct(all_ms, 0.9), "max": max(all_ms)
                    if all_ms else None},
        "recent": games[:GAMES_SHOWN],
    }


def collect():
    runs = []
    alive = live_prefixes()
    for path in sorted(glob.glob(os.path.join(HERE, "*.log"))):
        r = parse_log(path)
        if r:
            if alive is not None and not r["done"]:
                r["stale"] = r["name"] not in alive
            runs.append(r)
    # Busiest runs first; a finished run sorts below a live one of equal size.
    # live first, then stopped, then finished; biggest first within a group
    runs.sort(key=lambda r: (r["done"] is None and not r["stale"],
                             r["games"]), reverse=True)
    return {"runs": runs, "baseline": BASELINE, "target": TARGET}


PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>penta bot — training monitor</title>
<style>
:root{
  color-scheme:light;
  --bg:#f7f7f5; --surface-1:#fcfcfb; --border:#e2e1dc;
  --text-primary:#0b0b0b; --text-secondary:#52514e; --text-muted:#87867f;
  --grid:#ebeae5; --rule:#c9c8c1;
  --s1:#2a78d6; --s2:#eb6834; --s3:#1baf7a; --s4:#eda100; --s5:#e87ba4;
  --s6:#008300; --s7:#4a3aa7; --s8:#e34948;
}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){
  color-scheme:dark;
  --bg:#111110; --surface-1:#1a1a19; --border:#333330;
  --text-primary:#fff; --text-secondary:#c3c2b7; --text-muted:#8f8e85;
  --grid:#26262433; --rule:#44443f;
  --s1:#3987e5; --s2:#d95926; --s3:#199e70; --s4:#c98500; --s5:#d55181;
  --s6:#008300; --s7:#9085e9; --s8:#e66767;
}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text-primary);
  font:14px/1.5 ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif;
  padding:24px}
.wrap{max-width:1100px;margin:0 auto}
h1{font-size:18px;margin:0 0 2px;letter-spacing:-.01em}
.sub{color:var(--text-secondary);font-size:13px;margin:0 0 20px}
.tiles{display:flex;flex-wrap:wrap;gap:12px;margin-bottom:20px}
.tile{background:var(--surface-1);border:1px solid var(--border);
  border-radius:10px;padding:12px 16px;min-width:150px}
.tile .k{color:var(--text-muted);font-size:11px;text-transform:uppercase;
  letter-spacing:.06em}
.tile .v{font-size:26px;font-variant-numeric:tabular-nums;
  letter-spacing:-.02em;margin-top:2px}
.tile .n{color:var(--text-secondary);font-size:12px}
.card{background:var(--surface-1);border:1px solid var(--border);
  border-radius:10px;padding:16px;margin-bottom:20px}
.card h2{font-size:13px;margin:0 0 12px;color:var(--text-secondary);
  font-weight:600;letter-spacing:.02em}
.chartbox{overflow-x:auto}
svg{display:block}
.legend{display:flex;flex-wrap:wrap;gap:14px;margin-top:12px}
.legend span{display:inline-flex;align-items:center;gap:6px;
  color:var(--text-secondary);font-size:12px}
.chip{width:10px;height:10px;border-radius:3px;flex:none}
table{border-collapse:collapse;width:100%;font-size:13px}
th,td{text-align:right;padding:7px 10px;border-bottom:1px solid var(--border);
  font-variant-numeric:tabular-nums;white-space:nowrap}
th{color:var(--text-muted);font-weight:600;font-size:11px;
  text-transform:uppercase;letter-spacing:.05em}
td.l,th.l{text-align:left}
tr:last-child td{border-bottom:none}
.name{display:inline-flex;align-items:center;gap:7px}
.cfg{color:var(--text-muted);font-size:11px}
.dead{color:var(--text-muted)}
.tip{position:fixed;pointer-events:none;opacity:0;transition:opacity .1s;
  background:var(--surface-1);border:1px solid var(--rule);border-radius:8px;
  padding:8px 10px;font-size:12px;box-shadow:0 4px 14px #0002;z-index:9}
.tip b{font-weight:600}
.tip .row{display:flex;align-items:center;gap:6px;justify-content:space-between}
.foot{color:var(--text-muted);font-size:12px;margin-top:4px}
.tog{display:inline-flex;align-items:center;gap:7px;margin:0 0 16px;
  color:var(--text-secondary);font-size:12px;cursor:pointer;user-select:none}
.tog input{cursor:pointer}
.empty{color:var(--text-muted);font-size:13px;padding:6px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));
  gap:16px}
.panel h3{font-size:12px;margin:0 0 1px;color:var(--text-primary);
  font-weight:600}
.panel .why{font-size:11px;color:var(--text-muted);margin:0 0 6px;
  min-height:28px}
.mu{border-collapse:collapse;font-size:12px}
.mu td,.mu th{padding:5px 8px;white-space:nowrap}
.mu th{color:var(--text-muted);font-weight:600;font-size:11px;
  text-transform:uppercase;letter-spacing:.04em;text-align:left}
.mu td.v{text-align:right;font-variant-numeric:tabular-nums;
  border-radius:4px;color:var(--text-primary)}
.pill{display:inline-flex;align-items:center;gap:6px;padding:2px 10px;
  border-radius:999px;font-size:12px;font-weight:600;
  border:1px solid var(--rule);color:var(--text-secondary)}
.pill i{width:8px;height:8px;border-radius:50%;background:currentColor;
  flex:none}
.ok{color:var(--s3);border-color:color-mix(in oklab,var(--s3) 40%,transparent)}
.warn{color:var(--s4);border-color:color-mix(in oklab,var(--s4) 40%,transparent)}
.bad{color:var(--s8);border-color:color-mix(in oklab,var(--s8) 45%,transparent);
  background:color-mix(in oklab,var(--s8) 8%,transparent)}
.alarm{border:1px solid var(--s8);border-radius:8px;padding:10px 12px;
  margin:10px 0 0;background:color-mix(in oklab,var(--s8) 10%,transparent)}
.alarm b{color:var(--s8)}
.alarm .n{font-size:26px;font-variant-numeric:tabular-nums;color:var(--s8);
  font-weight:600;line-height:1.1}
.suspect{border:1px dashed var(--s4);border-radius:8px;padding:10px 12px;
  background:color-mix(in oklab,var(--s4) 7%,transparent)}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:11px;
  color:var(--text-secondary);white-space:pre-wrap;word-break:break-all}
</style></head><body><div class="wrap">
<h1>penta bot — training monitor</h1>
<p class="sub" id="sub">Evaluation: our bot vs the engine's built-in
handcrafted bot. <b>50% is parity</b> — below it we are losing. Each gate
carries its own ± standard error, so compare <b>means</b>, never a single
gate.</p>
<div class="tiles" id="tiles"></div>
<div class="card"><h2>Hosted bot — playing on the live server</h2>
  <p class="foot" style="margin:0 0 10px">Training tells you what the net
  scores in the lab. This is the daemon (<code>hosted_bot.py</code>) that
  actually sits on the server and plays whoever shows up.</p>
  <div id="hosted"><p class="empty">loading…</p></div>
</div>
<div class="card"><h2>Now training</h2>
  <p class="foot" style="margin:0 0 10px">Round-by-round progress of the
  live run. The chart below plots GATES, which only happen every
  --gate-every rounds, so this is what a run looks like between them.</p>
  <div id="now"></div>
</div>
<div class="card"><h2>Gate rate vs games played</h2>
  <div class="chartbox"><svg id="chart" width="1040" height="380"></svg></div>
  <div class="legend" id="legend"></div>
</div>
<div class="card"><h2>Training health</h2>
  <div class="grid" id="panels"></div>
  <p class="foot">Same colours as above. Each panel has its own y scale —
  never a shared one, they measure different things.</p>
</div>
<div class="card"><h2>How it actually plays
  <a href="/playouts" style="font-size:13px;font-weight:400">open &rarr;</a></h2>
  <p class="foot" style="margin:0">Move-by-move review of real games, with
  the misplays it found — a win rate cannot tell you the bot aimed Fireball
  at its own face.</p>
</div>
<div class="card"><h2>Current vs best (mirror) — <span style="color:var(--s4)">suspect, under repair</span></h2>
  <div id="mirror"></div>
</div>
<div class="card"><h2>Win rate by opponent deck</h2>
  <p class="foot" style="margin:0 0 10px">Our bot pilots <b>Sligh</b> in
  every one of these games; the named deck is piloted by the engine's
  <b>built-in handcrafted bot</b>. 50% is parity; below that we lose. Seats
  alternate. Latest evaluation per run — one aggregate number hides
  everything that matters here.<br>
  The <b>Sligh mirror is absent by construction</b>: the gate builds its
  opponent list as every deck EXCEPT the one we pilot, so no number for it
  appears below. Any mirror figure has to come from a separate run, and the
  one this loop logs is currently broken — see the panel above.</p>
  <div id="grid"></div>
</div>
<div class="card"><h2>Runs <span id="hint" style="font-weight:400;color:var(--text-muted)"></span></h2>
  <table><thead><tr>
    <th class="l">run</th><th>games</th><th>gates</th><th>mean</th>
    <th>last 8</th><th>best</th><th>entropy</th><th>g/s</th>
    <th class="l">config</th></tr></thead>
  <tbody id="rows"></tbody></table>
  <p class="foot" id="foot"></p>
</div></div>
<div class="tip" id="tip"></div>
<script>
const SERIES=["--s1","--s2","--s3","--s4","--s5","--s6","--s7","--s8"];
const css=v=>getComputedStyle(document.documentElement).getPropertyValue(v).trim();
const fmt=(x,d=1)=>x==null?"—":x.toFixed(d);
let DATA=null;

function draw(){
  if(!DATA)return;
  // Chart at most 8 series: hues are assigned in fixed order and NEVER
  // cycled, so a 9th line would repeat a colour and break identity. Live
  // runs win the slots, then the largest stopped/finished ones; everything
  // else stays in the table below (which is the full record).
  const CAP=8;
  // Default to runs that are actually training. Finished and stopped runs
  // accumulate fast (one per experiment arm) and drown the live ones --
  // 19 runs on an 8-hue chart repeats colours and buries what matters.
  // Runs with no gate yet still belong in the table and the live panel;
  // only the CHART needs gate points to plot.
  const all=DATA.runs.filter(r=>r.gates.length||r.live);
  // Zero live runs is a NORMAL state now, not an error: training gets
  // stopped, and old logs get archived. Say so plainly instead of rendering
  // an empty chart, -Infinity headline tiles and a bare table.
  if(!all.length){
    document.getElementById("tiles").innerHTML=`
      <div class="tile"><div class="k">training</div>
        <div class="v">—</div><div class="n">no run in progress</div></div>`;
    document.getElementById("chart").innerHTML=
      `<text x="20" y="40" font-size="13" fill="${css("--text-muted")}">`+
      `No run logs in penta-bot/ — nothing to plot.</text>`;
    document.getElementById("legend").innerHTML="";
    document.getElementById("rows").innerHTML=
      `<tr><td class="l" colspan="9" style="color:var(--text-muted)">`+
      `No training run. The hosted panel above still works.</td></tr>`;
    document.getElementById("foot").textContent="";
    document.getElementById("sub").innerHTML=
      "Evaluation: our bot vs the engine's built-in handcrafted bot. "+
      "<b>50% is parity</b> — below it we are losing.";
    now([]);panels([]);grid([]);mirror([]);
    return;
  }
  // The CHART shows only what is training. Finished and stopped runs pile
  // up one per experiment arm, and 19 series on an 8-hue palette repeats
  // colours and buries the runs that matter. Falls back to the most recent
  // runs when nothing is live, so the chart is never blank.
  const chartable=(()=>{const plottable=all.filter(r=>r.gates.length);
    const live=plottable.filter(r=>!r.done&&!r.stale);
    return live.length?live:plottable;})();
  const runs=chartable.slice().sort((a,b)=>b.games-a.games).slice(0,CAP);
  const charted=new Set(runs.map(r=>r.name));
  const svg=document.getElementById("chart");
  const W=svg.clientWidth||1040,H=380,P={t:14,r:132,b:34,l:44};
  const maxG=Math.max(1,...runs.map(r=>r.games));
  const pcts=runs.flatMap(r=>r.gates.map(g=>g.pct));
  const lo=Math.max(0,Math.min(DATA.baseline-4,...pcts)-2);
  const hi=Math.max(DATA.target+3,...pcts)+2;
  const X=g=>P.l+(W-P.l-P.r)*(g/maxG);
  const Y=p=>P.t+(H-P.t-P.b)*(1-(p-lo)/(hi-lo));
  let s=`<rect x="0" y="0" width="${W}" height="${H}" fill="none"/>`;
  // recessive grid + axis ticks
  for(let p=Math.ceil(lo/10)*10;p<=hi;p+=10){
    s+=`<line x1="${P.l}" y1="${Y(p)}" x2="${W-P.r}" y2="${Y(p)}"
        stroke="${css("--grid")}" stroke-width="1"/>
        <text x="${P.l-8}" y="${Y(p)+4}" text-anchor="end" font-size="11"
        fill="${css("--text-muted")}">${p}%</text>`;
  }
  for(let i=0;i<=4;i++){const g=maxG*i/4;
    s+=`<text x="${X(g)}" y="${H-12}" text-anchor="middle" font-size="11"
        fill="${css("--text-muted")}">${Math.round(g/1000)}k</text>`;}
  // annotated reference lines: the bar we beat, and the milestone
  // The warm-start floor: the score of the net this run STARTED from.
  // Training that sits below its own floor is going backwards, and that is
  // invisible without the line.
  const fl=runs.map(r=>r.floor).find(f=>f);
  const refs=[[DATA.baseline,"parity with built-in bot 50%"]];
  if(fl) refs.push([fl.pct,`warm-start floor ${fl.pct.toFixed(1)}% `+
    `(the net this run began from)`]);
  for(const [v,lab] of refs){
    if(v<lo||v>hi)continue;
    s+=`<line x1="${P.l}" y1="${Y(v)}" x2="${W-P.r}" y2="${Y(v)}"
        stroke="${css("--rule")}" stroke-width="1" stroke-dasharray="4 4"/>
        <text x="${W-P.r-4}" y="${Y(v)-5}" text-anchor="end" font-size="10"
        fill="${css("--text-muted")}">${lab}</text>`;
  }
  runs.forEach((r,i)=>{
    const c=css(SERIES[i]);
    const pts=r.gates.map(g=>`${X(g.games)},${Y(g.pct)}`).join(" ");
    s+=`<polyline points="${pts}" fill="none" stroke="${c}" stroke-width="2"
        stroke-linejoin="round" stroke-linecap="round"/>`;
  });
  // Direct labels last, in a de-collided column at the right edge: without
  // this the end-points pile into an unreadable stack whenever runs sit at
  // similar rates -- which is exactly when the chart matters most.
  const labs=runs.map((r,i)=>{const g=r.gates[r.gates.length-1];
    return {r,i,gx:X(g.games),gy:Y(g.pct),y:Y(g.pct),pct:g.pct};})
    .sort((a,b)=>a.y-b.y);
  // Labels are two lines when a run has a descriptor, so the de-collision
  // spacing has to account for the second line or they overlap again.
  const GAP=runs.some(r=>r.label)?27:15;
  for(let k=1;k<labs.length;k++)
    if(labs[k].y-labs[k-1].y<GAP) labs[k].y=labs[k-1].y+GAP;
  const over=labs.length?labs[labs.length-1].y-(H-P.b):0;
  if(over>0) labs.forEach(l=>l.y-=over);
  labs.forEach(l=>{
    const c=css(SERIES[l.i]);
    const lx=W-P.r+12;
    s+=`<circle cx="${l.gx}" cy="${l.gy}" r="4" fill="${c}"
        stroke="${css("--surface-1")}" stroke-width="2"/>`;
    if(Math.abs(l.y-l.gy)>2)
      s+=`<path d="M${l.gx+5} ${l.gy} L${lx-6} ${l.y}" fill="none"
          stroke="${css("--rule")}" stroke-width="1"/>`;
    s+=`<circle cx="${lx-1}" cy="${l.y-3}" r="3.5" fill="${c}"/>
        <text x="${lx+8}" y="${l.y}" font-size="11"
        fill="${css("--text-secondary")}">${l.r.name} ${fmt(l.pct)}%</text>`
      + (l.r.label?`<text x="${lx+8}" y="${l.y+12}" font-size="10"
        fill="${css("--text-muted")}">${l.r.label}</text>`:"");
  });
  svg.innerHTML=s;
  document.getElementById("legend").innerHTML=runs.map((r,i)=>
    `<span><i class="chip" style="background:${css(SERIES[i])}"></i>${r.name}`
     + (r.label?` <span style="color:var(--text-muted)">(${r.label})</span>`:"")
     + `</span>`
  ).join("")+(all.length>runs.length
    ? `<span style="color:var(--text-muted)">+${all.length-runs.length} more in the table below</span>` : "");

  // hover: nearest gate on any series
  const tip=document.getElementById("tip");
  svg.onmousemove=e=>{
    const b=svg.getBoundingClientRect();
    const mx=e.clientX-b.left,my=e.clientY-b.top;
    let best=null,bd=1e9;
    runs.forEach((r,i)=>r.gates.forEach(g=>{
      const d=(X(g.games)-mx)**2+(Y(g.pct)-my)**2;
      if(d<bd){bd=d;best={r,g,i};}}));
    if(!best||bd>2600){tip.style.opacity=0;return;}
    const {r,g,i}=best;
    tip.innerHTML=`<div class="row"><b>${r.name}</b>
      <i class="chip" style="background:${css(SERIES[i%8])}"></i></div>
      <div>${g.games.toLocaleString()} games — <b>${fmt(g.pct)}%</b>`+
      (g.se!=null?` ± ${fmt(g.se)}`:"")+`</div>`+
      (g.w!=null?`<div>${g.w}W ${g.d}D ${g.l}L / ${g.n}</div>`:"")+
      (g.entropy!=null?`<div>entropy ${fmt(g.entropy,3)}`+
      (g.gps!=null?` · ${fmt(g.gps,2)} g/s`:"")+`</div>`:"");
    tip.style.left=(e.clientX+14)+"px";
    tip.style.top=(e.clientY-10)+"px";
    tip.style.opacity=1;
  };
  svg.onmouseleave=()=>tip.style.opacity=0;

  // headline tiles
  const live=all.filter(r=>!r.done&&!r.stale);
  const bestRun=all.reduce((a,b)=>(b.best??-1)>(a.best??-1)?b:a,all[0]||{});
  const tot=all.reduce((a,r)=>a+r.games,0);
  document.getElementById("tiles").innerHTML=`
    <div class="tile"><div class="k">best gate</div>
      <div class="v">${fmt(bestRun.best)}%</div>
      <div class="n">${bestRun.name||"—"}</div></div>
    <div class="tile"><div class="k">best run mean</div>
      <div class="v">${fmt(Math.max(...all.map(r=>r.recent??-1)))}%</div>
      <div class="n">last 8 gates</div></div>
    <div class="tile"><div class="k">games played</div>
      <div class="v">${(tot/1000).toFixed(0)}k</div>
      <div class="n">across ${all.length} run${all.length===1?"":"s"}</div></div>
    <div class="tile"><div class="k">live runs</div>
      <div class="v">${live.length}</div>
      <div class="n">${all.length-live.length} stopped/finished</div></div>`;

  document.getElementById("rows").innerHTML=all.map(r=>{
    const ci=runs.findIndex(x=>x.name===r.name);
    const chip=ci>=0
      ? `<i class="chip" style="background:${css(SERIES[ci])}"></i>`
      : `<i class="chip" style="background:transparent;border:1px solid var(--rule)"></i>`;
    return `
    <tr class="${(r.done||r.stale)?"dead":""}">
      <td class="l"><span class="name">${chip}${r.name}</span>
        ${r.label?`<div class="cfg" style="margin-left:17px">${r.label}</div>`:""}</td>
      <td>${r.games.toLocaleString()}</td><td>${r.n}</td>
      <td>${fmt(r.mean)}% <span class="cfg">±${fmt(r.se,2)}</span></td>
      <td>${fmt(r.recent)}%</td><td>${fmt(r.best)}%</td>
      <td>${fmt(r.entropy,3)}</td><td>${fmt(r.gps,2)}</td>
      <td class="l cfg">${r.done?"done — "+r.done
        :r.stale?`stopped — idle ${r.idle_min}m`+(r.cadence_min?` (gates ~${r.cadence_min}m)`:"")
        :(r.config||"").replace(/^games=\S+ /,"").slice(0,58)}</td></tr>`;}).join("");
  // The FULL list, not the charted subset. `runs` above is filtered to
  // runs that have gated, and a run that has not gated yet is exactly the
  // one this panel exists to show.
  now(DATA.runs);
  panels(runs);
  grid(runs);
  mirror(all);
  document.getElementById("foot").textContent =
    "mean ± is the standard error of that run's gate mean, built from the ± "
    + "each gate reported. 50% is parity with the built-in bot.";
  // Read the gate size and error off the LOG. The prose here used to
  // assert "400 games, ±2.5" while the loop ran 120-game gates at ±4.5.
  const top=all.find(r=>r.gate_games)||{};
  const lastSe=(top.gates||[]).map(g=>g.se).filter(v=>v!=null).pop();
  document.getElementById("sub").innerHTML =
    "Evaluation: our bot vs the engine's built-in handcrafted bot. "
    + "<b>50% is parity</b> — below it we are losing."
    + (top.gate_games
       ? ` Each gate is ${top.gate_games} games`
         + (lastSe!=null?` (±${lastSe.toFixed(1)} points on one gate)`:"")
         + `, so compare <b>means</b>, never a single gate.`
       : " Each gate carries its own ± standard error, so compare"
         + " <b>means</b>, never a single gate.");
}
// Small multiples. Each measures a different thing, so each gets its own
// y scale; a shared axis would be meaningless and a dual axis is never the
// answer. Series colours match the main chart so identity carries across.
const PANELS=[
  {k:"entropy", t:"Policy entropy", d:1,
   why:"Leading indicator. Drifting to ~0.15 with a flat score = stopped exploring."},
  {k:"rate", t:"Games / sec (actual)", d:1,
   why:"Per-interval, not the log's cumulative average. Falls as an actor strengthens."},
  {k:"capped_pct", t:"Games hitting the cap %", d:1,
   why:"Ran past the decision cap and were cut short. Long games are the ones we lose."},
  {k:"closs", t:"Critic loss", d:2,
   why:"Is the value estimate tracking? Rising with flat entropy is trouble."},
];
function panels(runs){
  const box=document.getElementById("panels");
  box.innerHTML=PANELS.map(p=>{
    const W=230,H=94,P={t:8,r:8,b:16,l:34};
    const series=runs.map((r,i)=>({i,name:r.name,
      pts:r.gates.filter(g=>g.games>0&&g[p.k]!=null)
                 .map(g=>[g.games,g[p.k]])})).filter(s=>s.pts.length>1);
    if(!series.length) return `<div class="panel"><h3>${p.t}</h3>
      <p class="why">${p.why}</p><p class="empty">no data yet</p></div>`;
    const xs=series.flatMap(s=>s.pts.map(v=>v[0]));
    const ys=series.flatMap(s=>s.pts.map(v=>v[1]));
    const xh=Math.max(...xs)||1;
    let lo=Math.min(...ys),hi=Math.max(...ys);
    if(hi-lo<1e-9){hi=lo+1;}
    const pad=(hi-lo)*0.12; lo-=pad; hi+=pad;
    const X=v=>P.l+(W-P.l-P.r)*(v/xh);
    const Y=v=>P.t+(H-P.t-P.b)*(1-(v-lo)/(hi-lo));
    let g=`<line x1="${P.l}" y1="${Y(hi)}" x2="${W-P.r}" y2="${Y(hi)}"
      stroke="${css("--grid")}" stroke-width="1"/>
      <line x1="${P.l}" y1="${Y(lo)}" x2="${W-P.r}" y2="${Y(lo)}"
      stroke="${css("--grid")}" stroke-width="1"/>
      <text x="${P.l-5}" y="${Y(hi)+4}" text-anchor="end" font-size="10"
      fill="${css("--text-muted")}">${hi.toFixed(p.d)}</text>
      <text x="${P.l-5}" y="${Y(lo)+4}" text-anchor="end" font-size="10"
      fill="${css("--text-muted")}">${lo.toFixed(p.d)}</text>`;
    for(const s of series){
      const c=css(SERIES[s.i]);
      g+=`<polyline points="${s.pts.map(v=>`${X(v[0])},${Y(v[1])}`).join(" ")}"
        fill="none" stroke="${c}" stroke-width="2" stroke-linejoin="round"
        stroke-linecap="round"/>`;
      const last=s.pts[s.pts.length-1];
      g+=`<circle cx="${X(last[0])}" cy="${Y(last[1])}" r="3" fill="${c}"
        stroke="${css("--surface-1")}" stroke-width="1.5"/>`;
    }
    const vals=series.map(s=>{const v=s.pts[s.pts.length-1][1];
      return `<span style="color:${css(SERIES[s.i])}">●</span>
        <span style="color:var(--text-secondary)">${v.toFixed(p.d)}</span>`;}).join(" ");
    return `<div class="panel"><h3>${p.t}</h3><p class="why">${p.why}</p>
      <svg width="${W}" height="${H}">${g}</svg>
      <div style="font-size:11px;margin-top:2px">${vals}</div></div>`;
  }).join("");
}

// Win rate per opponent deck. SEQUENTIAL colour (one hue, light->dark) --
// this is magnitude, not identity, so a categorical palette would be wrong
// and a rainbow worse. 50% is the meaningful midpoint, so the scale is
// diverging around it: below is warm, above is cool.
function now(runs){
  const box=document.getElementById("now");
  const live=runs.filter(r=>!r.stale && r.done===null && r.live &&
                            Object.keys(r.live).length);
  if(!live.length){box.innerHTML=`<p class="empty">No run is training right now.</p>`;return;}
  box.innerHTML=live.map(r=>{
    const l=r.live, h=r.hist||[];
    const spark=(idx,lo,hi)=>{
      if(h.length<2) return "";
      const W=260,H=28;
      const pts=h.map((p,i)=>{
        const x=i*(W/(h.length-1));
        const v=Math.max(lo,Math.min(hi,p[idx]));
        return `${x.toFixed(1)},${(H-(v-lo)/(hi-lo)*H).toFixed(1)}`;
      }).join(" ");
      return `<svg width="${W}" height="${H}"><polyline points="${pts}"
        fill="none" stroke="var(--s1)" stroke-width="2"/></svg>`;
    };
    const cell=(lab,val,extra="")=>`<div style="min-width:120px">
      <div class="foot" style="margin:0">${lab}</div>
      <div style="font-size:18px;font-variant-numeric:tabular-nums">${val}</div>
      ${extra}</div>`;
    return `<div><b>${r.name}</b> <span class="foot">${r.label||""}</span></div>
     <div style="display:flex;gap:22px;flex-wrap:wrap;margin-top:8px">
      ${cell("games", r.games.toLocaleString())}
      ${cell("games/sec", (l.gps||0).toFixed(2), spark(1,0,2))}
      ${cell("finished", (l.fin||0)+"%", spark(2,0,100))}
      ${cell("policy entropy", (l.entropy||0).toFixed(3), spark(3,0,1.8))}
      ${cell("search entropy", (l.tgt_ent||0).toFixed(3), spark(4,0,1))}
      ${cell("value mse / base",
             (l.vmse||0).toFixed(3)+" / "+(l.vbase||0).toFixed(3))}
      ${cell("last gate", r.last!=null? r.last.toFixed(1)+"%" : "none yet")}
     </div>`;
  }).join("<hr style='border:0;border-top:1px solid var(--border);margin:14px 0'>");
}

// The MIRROR line (current net vs the best net) is KNOWN BROKEN as of
// 2026-08-26: the comparison did not actually play the two nets against
// each other, so the percentage is not a measurement of anything. It is
// shown anyway -- deleting the display would hide the repair -- but it is
// fenced off, never fed into a headline tile, and never plotted next to a
// real gate where it could be mistaken for one.
function mirror(runs){
  const box=document.getElementById("mirror");
  const rs=(runs||[]).filter(r=>r.mirror&&r.mirror.length);
  const warn=`<p class="foot" style="margin:0"><b style="color:var(--s4)">
    Do not read these as results.</b> The comparison behind the MIRROR line
    did not actually play the current net against the best net, so the
    percentages below measure nothing. They are parsed and displayed only so
    the fix can be seen landing — the values should start moving once the
    underlying match is repaired.</p>`;
  if(!rs.length){box.innerHTML=`<div class="suspect">${warn}
    <p class="empty" style="margin:6px 0 0">No MIRROR lines in the current
    log.</p></div>`;return;}
  box.innerHTML=`<div class="suspect">${warn}
    <table class="mu" style="margin-top:8px"><tr><th>run</th><th>round</th>
    <th>reported</th><th>games</th></tr>
    ${rs.flatMap(r=>r.mirror.slice(-6).reverse().map(m=>
      `<tr><td>${r.name}</td><td>${m.round}</td>
       <td class="v" style="color:var(--text-muted)">${m.pct.toFixed(1)}%
       <span class="cfg">(suspect)</span></td>
       <td class="v">${m.games}</td></tr>`)).join("")}
    </table></div>`;
}

function grid(runs){
  const rs=runs.filter(r=>r.matchups&&r.matchups.length);
  const box=document.getElementById("grid");
  if(!rs.length){box.innerHTML=
    `<p class="empty">No matchup data yet — logged at each evaluation.</p>`;
   return;}
  const decks=rs[0].matchups.map(m=>m[0]);
  const cell=v=>{
    if(v==null) return `<td class="v">—</td>`;
    // 0 at 20%, 1 at 80%; midpoint 50% is neutral
    const t=Math.max(0,Math.min(1,(v-20)/60));
    const bg = t<0.5
      ? `color-mix(in oklab, var(--s2) ${Math.round((0.5-t)*160)}%, transparent)`
      : `color-mix(in oklab, var(--s1) ${Math.round((t-0.5)*160)}%, transparent)`;
    return `<td class="v" style="background:${bg}">${v.toFixed(0)}%</td>`;
  };
  box.innerHTML=`<div style="overflow-x:auto"><table class="mu">
    <tr><th>run</th>${decks.map(d=>`<th style="text-align:right">${d}</th>`).join("")}</tr>
    ${rs.map(r=>{
      const by=Object.fromEntries(r.matchups);
      return `<tr><td>${r.name}</td>${decks.map(d=>cell(by[d])).join("")}</tr>`;
    }).join("")}</table></div>
    <p class="foot">Blue above 50%, orange below; 50% is parity with the
    built-in bot. Sligh is the deck we pilot; these are the
    ${decks.length} it faces.</p>`;
}


// ---- hosted bot -------------------------------------------------------
// Three questions, in the order they go wrong: is the process alive, does
// the server think we are online, and is SEARCH ACTUALLY RUNNING. The last
// one is the reason this panel exists: on a search failure HostedPolicy
// falls back to the first legal action and keeps playing, so a dead search
// looks exactly like a live bot from every angle except this counter.
const ago=t=>{
  if(!t)return "—";
  const s=Math.max(0,Date.now()/1000-t);
  if(s<90)return Math.round(s)+"s ago";
  if(s<5400)return Math.round(s/60)+"m ago";
  if(s<172800)return Math.round(s/3600)+"h ago";
  return Math.round(s/86400)+"d ago";
};
const esc=x=>String(x==null?"":x).replace(/[<>&]/g,c=>
  ({"<":"&lt;",">":"&gt;","&":"&amp;"}[c]));

function hosted(h){
  const box=document.getElementById("hosted");
  if(!h){box.innerHTML=`<p class="empty">no hosted data</p>`;return;}
  const pill=(cls,txt)=>`<span class="pill ${cls}"><i></i>${txt}</span>`;
  const d=h.daemon, me=(h.listing||{}).me, g=h.games||{}, lat=h.latency||{};
  let pills="";
  pills+= d ? pill("ok",`daemon up · pid ${d.pid}`)
            : pill("bad","daemon NOT running");
  if(!h.listing.ok)
    pills+=pill("warn","server unreachable");
  else if(me&&me.online)
    pills+=pill("ok",`registered online as ${esc(me.name)}`+
                     (me.busy?" · in a game":" · idle"));
  else if(me)
    pills+=pill("bad",`listed but OFFLINE as ${esc(me.name)}`);
  else
    pills+=pill("bad",`not listed on the server (${h.listing.count} bot`+
                      `${h.listing.count===1?"":"s"} up)`);
  pills+= h.search_failed
        ? pill("bad",`${h.search_failed.toLocaleString()} SEARCH FAILED`)
        : pill("ok","search healthy");

  // The alarm. Loud, red, and above the numbers, because every other
  // number on this panel is meaningless while this one is nonzero.
  const alarm = h.search_failed ? `
    <div class="alarm">
      <div class="n">${h.search_failed.toLocaleString()} × SEARCH FAILED</div>
      <div><b>The bot is playing a 1-ply fallback.</b> Search threw and
      HostedPolicy played the first legal action instead. Every hosted
      result below was produced by that fallback, not by the net + search —
      read them as noise until this is zero.</div>
      ${(h.search_failed_examples||[]).map(e=>
        `<div class="mono">${esc(e)}</div>`).join("")}
      <div class="foot">Only the first few failures print;
      <code>hosted_bot.out</code> goes quiet after that by design.</div>
    </div>` : "";

  const tile=(k,v,n)=>`<div class="tile"><div class="k">${k}</div>
    <div class="v">${v}</div><div class="n">${n||""}</div></div>`;
  const record = g.finished
    ? `${g.win}W ${g.loss}L${g.other?` ${g.other} other`:""}`
    : "no finished games";
  const tiles=`<div class="tiles" style="margin:12px 0 0">
    ${tile("games", g.total||0,
           `${g.finished||0} finished${g.abandoned?` · ${g.abandoned} abandoned`:""}`)}
    ${tile("record", record,
           g.pct!=null?`${g.pct.toFixed(0)}% of finished games`:"—")}
    ${tile("median move", lat.med!=null?lat.med+" ms":"—",
           lat.p90!=null?`p90 ${lat.p90} ms · max ${lat.max} ms`:"no moves logged")}
    ${tile("heartbeat", h.heartbeat?h.heartbeat.replace("heartbeat ok ",""):"—",
           h.log_mtime?`log written ${ago(h.log_mtime)}`:"no hosted_bot.out")}
  </div>`;

  // A win handed over because the opponent's clock ran out is not evidence
  // the bot played well; separating them keeps the record honest.
  const caveat = g.walkover
    ? `<p class="foot">${g.walkover} of ${g.finished} finished game${
        g.finished===1?"":"s"} ended before we made a single move (opponent
        walked or timed out) — those say nothing about how we play.</p>` : "";

  const rows=(h.recent||[]).map(r=>{
    const out = r.abandoned ? `<span style="color:var(--s4)">abandoned</span>`
      : r.outcome==="win" ? `<span style="color:var(--s3)">win</span>`
      : r.outcome==="loss" ? `<span style="color:var(--s8)">loss</span>`
      : r.outcome ? esc(r.outcome) : `<span class="cfg">in progress</span>`;
    return `<tr><td class="l cfg">${esc(r.file)}</td>
      <td class="l">${out}</td>
      <td>${r.moves}</td>
      <td>${r.seconds!=null?r.seconds+"s":"—"}</td>
      <td>${r.med_ms!=null?r.med_ms+" ms":"—"}</td>
      <td>${r.no_worlds?`<span style="color:var(--s4)">${r.no_worlds}</span>`:0}</td>
      <td>${r.errors||0}</td>
      <td class="l cfg">${esc(r.message||"")}</td></tr>`;}).join("");

  box.innerHTML = `<div style="display:flex;flex-wrap:wrap;gap:8px">${pills}</div>
    ${alarm}${tiles}${caveat}
    <p class="foot" style="margin-top:14px">${esc(h.server)} ·
    registered as <b>${esc((h.registration||{}).name||"—")}</b> piloting
    <b>${esc((h.registration||{}).deck||"—")}</b>
    · id <span class="cfg">${esc((h.registration||{}).id||"—")}</span>
    ${h.listing.ok?`· listing ${h.listing.age}s old`
      :`· <span style="color:var(--s8)">${esc(h.listing.error||"fetch failed")}</span>`}</p>
    ${rows?`<div style="overflow-x:auto"><table style="margin-top:6px">
      <thead><tr><th class="l">transcript</th><th class="l">result</th>
      <th>our moves</th><th>length</th><th>median move</th>
      <th>no-worlds moves</th><th>errors</th><th class="l">server message</th>
      </tr></thead><tbody>${rows}</tbody></table></div>
      <p class="foot">One row per file in <code>hosted-games/</code>, newest
      first. <b>no-worlds moves</b> are decisions where every hypothesis
      reconstruction failed, so the move came from the unpruned fallback
      rather than the determinized search.</p>`
     :`<p class="empty">No transcripts in hosted-games/ yet.</p>`}`;
}

async function pollHosted(){
  try{hosted(await (await fetch("/hosted-data",{cache:"no-store"})).json());}
  catch(e){document.getElementById("hosted").innerHTML=
    `<p class="empty">hosted status unavailable (${e})</p>`;}
}
pollHosted();setInterval(pollHosted,20000);

async function poll(){
  try{DATA=await (await fetch("/data",{cache:"no-store"})).json();draw();}
  catch(e){}
}
poll();setInterval(poll,15000);
addEventListener("resize",draw);
matchMedia("(prefers-color-scheme:dark)").addEventListener("change",draw);
</script></body></html>
"""


PLAYOUT_PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>penta bot — how it actually plays</title>
<style>
:root{
  color-scheme:light;
  --bg:#f7f7f5; --surface-1:#fcfcfb; --border:#e2e1dc;
  --text-primary:#0b0b0b; --text-secondary:#52514e; --text-muted:#87867f;
  --grid:#ebeae5; --rule:#c9c8c1;
  --s1:#2a78d6; --s2:#eb6834; --s3:#1baf7a; --s4:#eda100; --s5:#e87ba4;
  --s6:#008300; --s7:#4a3aa7; --s8:#e34948;
}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){
  color-scheme:dark;
  --bg:#111110; --surface-1:#1a1a19; --border:#333330;
  --text-primary:#fff; --text-secondary:#c3c2b7; --text-muted:#8f8e85;
  --grid:#26262433; --rule:#44443f;
  --s1:#3987e5; --s2:#d95926; --s3:#199e70; --s4:#c98500; --s5:#d55181;
  --s6:#008300; --s7:#9085e9; --s8:#e66767;
}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text-primary);
  font:14px/1.5 ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif;
  padding:24px}
.wrap{max-width:1100px;margin:0 auto}
h1{font-size:18px;margin:0 0 2px;letter-spacing:-.01em}
.sub{color:var(--text-secondary);font-size:13px;margin:0 0 20px}
.tiles{display:flex;flex-wrap:wrap;gap:12px;margin-bottom:20px}
.tile{background:var(--surface-1);border:1px solid var(--border);
  border-radius:10px;padding:12px 16px;min-width:150px}
.tile .k{color:var(--text-muted);font-size:11px;text-transform:uppercase;
  letter-spacing:.06em}
.tile .v{font-size:26px;font-variant-numeric:tabular-nums;
  letter-spacing:-.02em;margin-top:2px}
.tile .n{color:var(--text-secondary);font-size:12px}
.card{background:var(--surface-1);border:1px solid var(--border);
  border-radius:10px;padding:16px;margin-bottom:20px}
.card h2{font-size:13px;margin:0 0 12px;color:var(--text-secondary);
  font-weight:600;letter-spacing:.02em}
.chartbox{overflow-x:auto}
svg{display:block}
.legend{display:flex;flex-wrap:wrap;gap:14px;margin-top:12px}
.legend span{display:inline-flex;align-items:center;gap:6px;
  color:var(--text-secondary);font-size:12px}
.chip{width:10px;height:10px;border-radius:3px;flex:none}
table{border-collapse:collapse;width:100%;font-size:13px}
th,td{text-align:right;padding:7px 10px;border-bottom:1px solid var(--border);
  font-variant-numeric:tabular-nums;white-space:nowrap}
th{color:var(--text-muted);font-weight:600;font-size:11px;
  text-transform:uppercase;letter-spacing:.05em}
td.l,th.l{text-align:left}
tr:last-child td{border-bottom:none}
.name{display:inline-flex;align-items:center;gap:7px}
.cfg{color:var(--text-muted);font-size:11px}
.dead{color:var(--text-muted)}
.tip{position:fixed;pointer-events:none;opacity:0;transition:opacity .1s;
  background:var(--surface-1);border:1px solid var(--rule);border-radius:8px;
  padding:8px 10px;font-size:12px;box-shadow:0 4px 14px #0002;z-index:9}
.tip b{font-weight:600}
.tip .row{display:flex;align-items:center;gap:6px;justify-content:space-between}
.foot{color:var(--text-muted);font-size:12px;margin-top:4px}
.tog{display:inline-flex;align-items:center;gap:7px;margin:0 0 16px;
  color:var(--text-secondary);font-size:12px;cursor:pointer;user-select:none}
.tog input{cursor:pointer}
.empty{color:var(--text-muted);font-size:13px;padding:6px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));
  gap:16px}
.panel h3{font-size:12px;margin:0 0 1px;color:var(--text-primary);
  font-weight:600}
.panel .why{font-size:11px;color:var(--text-muted);margin:0 0 6px;
  min-height:28px}
.mu{border-collapse:collapse;font-size:12px}
.mu td,.mu th{padding:5px 8px;white-space:nowrap}
.mu th{color:var(--text-muted);font-weight:600;font-size:11px;
  text-transform:uppercase;letter-spacing:.04em;text-align:left}
.mu td.v{text-align:right;font-variant-numeric:tabular-nums;
  border-radius:4px;color:var(--text-primary)}
</style>
<style>
.sev{display:inline-block;padding:1px 7px;border-radius:9px;font-size:11px;
  font-weight:600;letter-spacing:.02em}
.sev-bug{background:#e3494822;color:var(--s8)}
.sev-suspect{background:#eda10022;color:var(--s4)}
.sev-judgement{background:#87867f22;color:var(--text-muted)}
.ex{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;
  color:var(--text-secondary);margin:2px 0 2px 0;white-space:pre-wrap}
.finding{border-top:1px solid var(--border);padding:12px 0}
.finding:first-child{border-top:0}
.rate{font-variant-numeric:tabular-nums;font-weight:600}
</style>
</head><body>
<div class="wrap">
<h1>How the bot actually plays <a href="/" style="font-size:13px;font-weight:400">&larr; training monitor</a></h1>
<p class="foot" id="sub"></p>

<div class="card"><h2>Behaviour flags</h2>
  <p class="foot" style="margin:0 0 10px">
  A gate reports one number per 300 games. It cannot tell you the bot is
  pointing Fireball at its own face or sitting on a free Mox for six turns.
  These come from <code>playout_log.py</code>, which replays games move by
  move and marks decisions that look wrong.
  <b>Rates matter more than instances</b> — several of these are defensible
  Magic once, and a defect every game.</p>
  <div id="flags"></div>
</div>

<div class="card"><h2>Transcripts</h2>
  <p class="foot" style="margin:0 0 10px">Full move-by-move logs, in
  <code>penta-bot/playouts/</code>. Each line is one of OUR decisions: turn,
  step, both life totals, how many options were offered, and what was
  chosen.</p>
  <div id="files" class="foot"></div>
</div>
</div>
<script>
const SEV_ORDER={bug:0,suspect:1,judgement:2};
async function load(){
  const r=await fetch("/playout-data",{cache:"no-store"});
  const d=await r.json();
  const sub=document.getElementById("sub");
  if(!d.available){
    sub.textContent="No playout report yet — run playout_log.py.";
    return;
  }
  const rec=d.record||{};
  sub.innerHTML=`net <b>${d.net}</b> · ${d.iters} search sims · `+
    `${d.games} games (${rec.win||0}W ${rec.loss||0}L${rec.other?` ${rec.other} other`:""}) · `+
    `${(d.decisions||0).toLocaleString()} decisions · `+
    `generated ${new Date(d.mtime*1000).toLocaleString()}`;
  const meta=d.meta||{};
  const rows=Object.entries(d.flags||{}).map(([k,v])=>{
    const m=meta[k]||["judgement",k,""];
    return {k,v,rate:(d.per_game_rate||{})[k]||0,sev:m[0],title:m[1],why:m[2],
            ex:(d.examples||{})[k]||[]};
  }).sort((a,b)=>(SEV_ORDER[a.sev]-SEV_ORDER[b.sev])||(b.rate-a.rate));
  document.getElementById("flags").innerHTML = rows.length ? rows.map(f=>`
    <div class="finding">
      <div><span class="sev sev-${f.sev}">${f.sev}</span>
        <b>${f.title}</b>
        &nbsp;<span class="rate">${f.rate.toFixed(2)}/game</span>
        <span class="foot">(${f.v} in ${d.games} games)</span></div>
      <p class="foot" style="margin:4px 0 6px">${f.why}</p>
      ${f.ex.map(e=>`<div class="ex">${e.replace(/</g,"&lt;")}</div>`).join("")}
    </div>`).join("")
    : `<p class="empty">Nothing flagged.</p>`;
  document.getElementById("files").innerHTML =
    (d.transcripts||[]).map(t=>`playouts/${t}`).join("<br>") ||
    "none written";
}
load(); setInterval(load, 30000);
</script>
</body></html>
"""


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/data"):
            body = json.dumps(collect()).encode()
            ctype = "application/json"
        elif self.path.startswith("/hosted-data"):
            # Its own route, not folded into /data: this one reaches out to
            # the network, and a slow server must never stall the training
            # numbers on the same page.
            body = json.dumps(hosted_status()).encode()
            ctype = "application/json"
        elif self.path.startswith("/playout-data"):
            body = json.dumps(playout_report()).encode()
            ctype = "application/json"
        elif self.path.startswith("/playouts"):
            body = PLAYOUT_PAGE.encode()
            ctype = "text/html; charset=utf-8"
        elif self.path in ("/", "/index.html"):
            body = PAGE.encode()
            ctype = "text/html; charset=utf-8"
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):        # keep the console quiet
        pass


if __name__ == "__main__":
    snap = collect()
    host = hosted_status()
    # flush: stdout is a redirect to monitor.out in normal use, so without
    # this the startup banner sits in the buffer until the process exits --
    # which is precisely when nobody is looking for it.
    print(f"penta monitor: {len(snap['runs'])} run logs in {HERE}", flush=True)
    print(f"  hosted bot: daemon "
          f"{'up' if host['daemon'] else 'DOWN'}, "
          f"{host['games']['total']} transcripts, "
          f"{host['search_failed']} SEARCH FAILED", flush=True)
    print(f"  -> http://localhost:{PORT}", flush=True)
    try:
        # Threaded: /hosted-data reaches the network, and on a single
        # thread one slow server call froze every other request on the page.
        ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
    except KeyboardInterrupt:
        sys.exit(0)
