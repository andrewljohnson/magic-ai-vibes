#!/bin/zsh
# Two-layer capacity candidate: 128x128 deep net, standard recipe.
# Chained after the overnight queue; gates on the identical protocol
# and promotes only past the reigning champion's score.
set -e
cd "$(dirname "$0")"
REPORT=build/telemetry/overnight-report.txt
while ! grep -q "overnight queue complete" "$REPORT" 2>/dev/null; do
  sleep 300
done
BEST=$(grep -oE "gate: [0-9.]+" "$REPORT" | grep -oE "[0-9.]+" | sort -g | tail -1)
echo "--- deep128x128: training (bar $BEST) ---" >> "$REPORT"
./build/selfplay-zero train --out data/spz-deep128.txt \
  --iterations 900 --games 196 --schema-colors --hidden 128 \
  --hidden2 128 --td-lambda 0.9 --threads 8 --seed 43 \
  --probe-every 10 --probe-reps 8 \
  --telemetry build/telemetry/telemetry.jsonl \
  > build/telemetry/deep128.log 2>&1
./build/selfplay-zero benchmark --model data/spz-deep128.txt \
  --baseline handcrafted --reps 70 --rollout --worlds 4 \
  --threads 10 --seed 777 >> build/telemetry/deep128.log 2>&1
RATE=$(grep -E "^aggregate" build/telemetry/deep128.log | tail -1 | grep -oE "win-rate [0-9.]+" | awk '{print $2}')
echo "deep128x128 gate: $RATE (bar $BEST)" >> "$REPORT"
echo "deep128x128 probes: $(./build/selfplay-zero behavior-probe --model data/spz-deep128.txt --seed 4242 2>/dev/null)" >> "$REPORT"
if python3 -c "exit(0 if float('$RATE') > float('$BEST') else 1)"; then
  echo "deep128x128 PROMOTES" >> "$REPORT"
  cp data/spz-champion.txt data/spz-champion-prev.txt 2>/dev/null || true
  cp data/spz-deep128.txt data/spz-champion.txt
  ./build/matchup-matrix --threads 10 > build/telemetry/matrix-regen.log 2>&1 || true
else
  echo "deep128x128 rejected" >> "$REPORT"
fi
echo "=== deep-net stage complete $(date) ===" >> "$REPORT"
