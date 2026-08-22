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
BASELINE = 31.6        # handcrafted bot: the bar the project set out to beat
TARGET = 50.0          # the "path past 50%" milestone

TS_RE = re.compile(r"^\[(\d\d):(\d\d):(\d\d)\]")
GATE_RE = re.compile(
    r"GATE @(\d+) games: ([0-9.]+)%"
    r"(?:.*?entropy=([0-9.]+))?"
    r"(?:.*?\[([0-9.]+) g/s\])?")
GATE0_RE = re.compile(r"GATE @(\d+) games: honest actor vs handcrafted = ([0-9.]+)%")
START_RE = re.compile(r"PAR-AAC start: (.*)")
DONE_RE = re.compile(r"PAR-AAC complete: (\d+) games \(([^)]*)\)")


def parse_log(path):
    name = os.path.basename(path)[:-4]
    gates, config, done = [], "", None
    stamps = []
    try:
        with open(path, errors="replace") as f:
            for line in f:
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
                                  "pct": float(m.group(2)),
                                  "entropy": None, "gps": None})
                    continue
                m = GATE_RE.search(line)
                if m:
                    ts = TS_RE.match(line)
                    if ts:
                        h, mi, sec = (int(x) for x in ts.groups())
                        stamps.append(h * 3600 + mi * 60 + sec)
                    gates.append({
                        "games": int(m.group(1)), "pct": float(m.group(2)),
                        "entropy": float(m.group(3)) if m.group(3) else None,
                        "gps": float(m.group(4)) if m.group(4) else None})
        mtime = os.path.getmtime(path)
    except OSError:
        return None
    if not gates:
        return None
    # The @0 gate is only 40 games; keep it on the curve but never let it
    # into a mean, or it drags the number around by pure noise.
    scored = [g for g in gates if g["games"] > 0]
    pcts = [g["pct"] for g in scored]
    recent = pcts[-8:]
    idle = time.time() - mtime
    # median spacing between this run's own gates (wrapping midnight)
    deltas = sorted((b - a) % 86400 for a, b in zip(stamps, stamps[1:]))
    cadence = deltas[len(deltas) // 2] if deltas else 0
    stale_after = max(STALE_FLOOR_SECS, STALE_INTERVALS * cadence)
    return {
        "name": name,
        "config": config,
        "done": done,
        "stale": done is None and idle > stale_after,
        "idle_min": int(idle // 60),
        "cadence_min": round(cadence / 60, 1) if cadence else None,
        "gates": gates,
        "games": gates[-1]["games"],
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
    for path in sorted(glob.glob(os.path.join(HERE, "*.log"))):
        r = parse_log(path)
        if r:
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
</style></head><body><div class="wrap">
<h1>penta bot — training monitor</h1>
<p class="sub">Honest gate: actor argmax vs the engine's handcrafted bot.
Gates are 400 games (standard error ±2.5 points), so compare <b>means</b>,
never a single gate.</p>
<div class="tiles" id="tiles"></div>
<div class="card"><h2>Gate rate vs games played</h2>
  <div class="chartbox"><svg id="chart" width="1040" height="380"></svg></div>
  <div class="legend" id="legend"></div>
</div>
<div class="card"><h2>Runs</h2>
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
  const all=DATA.runs.filter(r=>r.gates.length);
  const runs=all.slice().sort((a,b)=>
      ((!a.done&&!a.stale)?0:1)-((!b.done&&!b.stale)?0:1)||b.games-a.games
    ).slice(0,CAP);
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
  for(const [v,lab] of [[DATA.baseline,"handcrafted 31.6%"],[DATA.target,"50%"]]){
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
  const GAP=15;
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
        fill="${css("--text-secondary")}">${l.r.name} ${fmt(l.pct)}%</text>`;
  });
  svg.innerHTML=s;
  document.getElementById("legend").innerHTML=runs.map((r,i)=>
    `<span><i class="chip" style="background:${css(SERIES[i])}"></i>${r.name}</span>`
  ).join("")+(all.length>runs.length
    ? `<span style="color:var(--text-muted)">+${all.length-runs.length} more in the table</span>` : "");

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
      <td class="l"><span class="name">${chip}${r.name}</span></td>
      <td>${r.games.toLocaleString()}</td><td>${r.n}</td>
      <td>${fmt(r.mean)}% <span class="cfg">±${fmt(r.se,2)}</span></td>
      <td>${fmt(r.recent)}%</td><td>${fmt(r.best)}%</td>
      <td>${fmt(r.entropy,3)}</td><td>${fmt(r.gps,2)}</td>
      <td class="l cfg">${r.done?"done — "+r.done
        :r.stale?`stopped — idle ${r.idle_min}m`+(r.cadence_min?` (gates ~${r.cadence_min}m)`:"")
        :(r.config||"").replace(/^games=\S+ /,"").slice(0,58)}</td></tr>`;}).join("");
  document.getElementById("foot").textContent =
    "mean ± is the standard error of that run's gate mean. entropy below ~0.15 "
    + "with a flat curve means the policy has stopped exploring.";
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
