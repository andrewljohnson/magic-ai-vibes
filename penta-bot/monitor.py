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
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

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

# 400-game gates -> ~2.5 point standard error on a single gate. Everything
# in the UI that compares runs uses a MEAN over gates for this reason; a
# single gate is not a result.
GATE_SE = 2.5
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
AZ_GATE_RE = re.compile(r"GATE round \d+: ([0-9.]+)%")  # first % only
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
    return out


def parse_log(path):
    name = os.path.basename(path)[:-4]
    gates, config, done = [], "", None
    az, az_games, az_last = False, 0, {}
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
                    m = AZ_GATE_RE.search(line)
                    if m:
                        gates.append({
                            "games": az_games, "pct": float(m.group(1)),
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
                            "capped": None, "gps": az_last.get("gps"),
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
                                  "capped": None, "gps": None, "ts": None})
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
                         "ts": stamps[-1] if stamps else None}
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
        g["capped_pct"] = None
        if prev and g["ts"] is not None and prev["ts"] is not None:
            dt = (g["ts"] - prev["ts"]) % 86400
            dg = g["games"] - prev["games"]
            if dt > 0 and dg > 0:
                g["rate"] = round(dg / dt, 2)
            if dg > 0 and g["capped"] is not None and prev["capped"] is not None:
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
        "n": len(pcts),
        "last": pcts[-1] if pcts else None,
        "best": max(pcts) if pcts else None,
        "mean": statistics.fmean(pcts) if pcts else None,
        "recent": statistics.fmean(recent) if recent else None,
        "recent_n": len(recent),
        "se": GATE_SE / len(pcts) ** 0.5 if pcts else None,
        "entropy": next((g["entropy"] for g in reversed(scored)
                         if g["entropy"] is not None), None),
        "gps": next((g["gps"] for g in reversed(scored)
                     if g["gps"] is not None), None),
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
</style></head><body><div class="wrap">
<h1>penta bot — training monitor</h1>
<p class="sub">Evaluation: actor argmax vs the engine's handcrafted bot.
Gates are 400 games (standard error ±2.5 points), so compare <b>means</b>,
never a single gate.</p>
<div class="tiles" id="tiles"></div>
<div class="card"><h2>Gate rate vs games played</h2>
  <div class="chartbox"><svg id="chart" width="1040" height="380"></svg></div>
  <div class="legend" id="legend"></div>
</div>
<div class="card"><h2>Training health</h2>
  <div class="grid" id="panels"></div>
  <p class="foot">Same colours as above. Each panel has its own y scale —
  never a shared one, they measure different things.</p>
</div>
<div class="card"><h2>Win rate by opponent deck</h2>
  <p class="foot" style="margin:0 0 10px">Our bot pilots <b>Sligh</b> in
  every one of these games; the named deck is piloted by the engine's
  <b>built-in handcrafted bot</b>. 50% is parity; below that we lose. Seats
  alternate. Latest evaluation per live run — one aggregate number hides
  everything that matters here.<br>
  The <b>Sligh mirror is absent by construction</b>: the gate builds its
  opponent list as every deck EXCEPT the one we pilot. Measured separately
  at round 239 it is 53.3% ± 4.6, our strongest matchup — we know that deck
  best because we pilot it every game.</p>
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
  const all=DATA.runs.filter(r=>r.gates.length);   // the TABLE shows every run
  // The CHART shows only what is training. Finished and stopped runs pile
  // up one per experiment arm, and 19 series on an 8-hue palette repeats
  // colours and buries the runs that matter. Falls back to the most recent
  // runs when nothing is live, so the chart is never blank.
  const chartable=(()=>{const live=all.filter(r=>!r.done&&!r.stale);
    return live.length?live:all;})();
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
  for(const [v,lab] of [[DATA.baseline,"parity with built-in bot 50%"]]){
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
      <div>${g.games.toLocaleString()} games — <b>${fmt(g.pct)}%</b></div>`+
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
      <div class="n">across ${all.length} runs</div></div>
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
  panels(runs);
  grid(runs);
  document.getElementById("foot").textContent =
    "mean ± is the standard error of that run's gate mean. entropy below ~0.15 "
    + "with a flat curve means the policy has stopped exploring.";
}
// Small multiples. Each measures a different thing, so each gets its own
// y scale; a shared axis would be meaningless and a dual axis is never the
// answer. Series colours match the main chart so identity carries across.
const PANELS=[
  {k:"entropy", t:"Policy entropy", d:1,
   why:"Leading indicator. Drifting to ~0.15 with a flat score = stopped exploring."},
  {k:"rate", t:"Games / sec (actual)", d:1,
   why:"Per-interval, not the log's cumulative average. Falls as an actor strengthens."},
  {k:"capped_pct", t:"Episodes discarded %", d:1,
   why:"Hit the 600-decision cap and were dropped. Long games are lost from training."},
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
    <p class="foot">Blue above 50%, orange below. Sligh is the deck we
    pilot; these are the fourteen it faces.</p>`;
}

async function poll(){
  try{DATA=await (await fetch("/data")).json();draw();}catch(e){}
}
poll();setInterval(poll,15000);
addEventListener("resize",draw);
matchMedia("(prefers-color-scheme:dark)").addEventListener("change",draw);
</script></body></html>
"""


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/data"):
            body = json.dumps(collect()).encode()
            ctype = "application/json"
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
    print(f"penta monitor: {len(snap['runs'])} run logs in {HERE}")
    print(f"  -> http://localhost:{PORT}")
    try:
        HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
    except KeyboardInterrupt:
        sys.exit(0)
