#!/bin/zsh
# Resume the real-8 era retrain from the iteration-200 checkpoint (the
# original run was killed externally at ~218/900). Warm start with the
# same recipe for the remaining 700 iterations - same-config
# continuation is proven safe (v11 precedent); a fresh seed decorrelates
# the continuation's game stream from the replayed early iterations.
set -e
cd "$(dirname "$0")"
REPORT=build/telemetry/real8-report.txt
echo "=== real-8 resume from ckpt-200 $(date) ===" >> "$REPORT"
START=$(date +%s)
./build/selfplay-zero train --out data/spz-real8.txt \
  --init data/spz-real8.txt.ckpt-200.txt \
  --iterations 700 --games 196 --schema-colors \
  --td-lambda 0.9 --threads 8 --seed 4408 --probe-every 10 \
  --probe-reps 8 \
  --telemetry build/telemetry/telemetry.jsonl \
  > build/telemetry/real8-resume.log 2>&1
END=$(date +%s)
echo "resume wall time: $(( (END-START)/60 )) min (+ ~120 before the kill)" >> "$REPORT"
./build/selfplay-zero benchmark --model data/spz-real8.txt \
  --baseline handcrafted --reps 100 --rollout --worlds 4 \
  --threads 10 --seed 777 \
  > build/telemetry/real8-gate.log 2>&1
RATE=$(grep -E "^aggregate" build/telemetry/real8-gate.log | tail -1 | grep -oE "win-rate [0-9.]+" | awk '{print $2}')
echo "real-8 gate: $RATE (floor bar 0.58)" >> "$REPORT"
echo "probes: $(./build/selfplay-zero behavior-probe --model data/spz-real8.txt --seed 4242 2>/dev/null)" >> "$REPORT"
if python3 -c "exit(0 if float('$RATE') >= 0.58 else 1)"; then
  echo "real-8 PROMOTES to era champion" >> "$REPORT"
  cp data/spz-real8.txt data/spz-champion.txt
else
  echo "real-8 below floor - arena stays on Handcrafted fallback" >> "$REPORT"
fi
for D in 6 7; do
  for I in 0 1 2 3; do
    ./build/replay --model data/spz-real8.txt \
      --seed $((18000 + D * 100 + I * 7)) --spz-deck $D \
      --opp-deck $(( (D + I + 1) % 6 )) \
      --name "real8-d${D}-${I}" > /dev/null 2>&1 || true
  done
done
echo "8 scouting replays written (real8-d*)" >> "$REPORT"
echo "=== real-8 retrain complete $(date) ===" >> "$REPORT"
