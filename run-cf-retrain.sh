#!/bin/zsh
# Counterfactual dominance-pair retrain: the champion recipe (real-8
# era: h256, TD 0.9, colors schema, 900 iters, seed 43) plus the new
# auxiliary counterfactual ranking targets (deploying a free permanent
# >= holding it, holding a card >= discarding it, inert graveyard pad
# ~ neutral) at fraction 0.15 / weight 0.1. The value-bias probe
# measured the champion pricing a free Mox deploy at +0.0096 — inside
# the decision-time tie band, so free-permanent casts are coin flips —
# plus a graveyard-credit pocket in the control decks.
# Gate: reps-100 vs Handcrafted, seed 777. SAME-ERA gate: promote only
# if strictly above the reigning champion's 0.715, else report only.
# Success metric beyond the gate: mox-deploy mean delta well above
# +0.0096 with the discard wrong-direction share staying low.
# Threads 6: a penta python training run shares this machine.
set -e
cd "$(dirname "$0")"
REPORT=build/telemetry/cf-report.txt
mkdir -p build/telemetry
echo "=== counterfactual retrain $(date) ===" > "$REPORT"
START=$(date +%s)
./build/selfplay-zero train --out data/spz-cf.txt \
  --iterations 900 --games 196 --schema-colors --hidden 256 \
  --td-lambda 0.9 --threads 6 --seed 43 --probe-every 10 \
  --probe-reps 8 \
  --counterfactual-fraction 0.15 --counterfactual-weight 0.1 \
  --telemetry build/telemetry/telemetry.jsonl \
  > build/telemetry/cf-retrain.log 2>&1
END=$(date +%s)
echo "training wall time: $(( (END-START)/60 )) min" >> "$REPORT"
./build/selfplay-zero benchmark --model data/spz-cf.txt \
  --baseline handcrafted --reps 100 --rollout --worlds 4 \
  --threads 6 --seed 777 \
  > build/telemetry/cf-gate.log 2>&1
RATE=$(grep -E "^aggregate" build/telemetry/cf-gate.log | tail -1 | grep -oE "win-rate [0-9.]+" | awk '{print $2}')
echo "cf gate: $RATE (reigning champion 0.715; same-era gate, promote only if strictly above)" >> "$REPORT"
echo "probes: $(./build/selfplay-zero behavior-probe --model data/spz-cf.txt --seed 4242 2>/dev/null)" >> "$REPORT"
# Value-bias probe BEFORE any promotion so the comparison line is the
# reigning champion, not a fresh copy of ourselves. This is the real
# success metric for the counterfactual targets.
./build/value-bias-probe --model data/spz-cf.txt \
  --model data/spz-champion.txt --threads 6 \
  --out build/telemetry/value-bias-cf.json \
  > build/telemetry/cf-value-bias.log 2>&1 || true
echo "--- value-bias probe (expect: cf mox-deploy mean well above +0.0096, discard wrong-direction share low) ---" >> "$REPORT"
sed -n '/=== model/,$p' build/telemetry/cf-value-bias.log >> "$REPORT" || true
if python3 -c "exit(0 if float('$RATE') > 0.715 else 1)"; then
  echo "cf PROMOTES to champion (gate $RATE > 0.715)" >> "$REPORT"
  cp data/spz-cf.txt data/spz-champion.txt
else
  echo "cf does not beat the reigning champion (gate $RATE <= 0.715) - no promotion" >> "$REPORT"
fi
echo "=== counterfactual retrain complete $(date) ===" >> "$REPORT"
