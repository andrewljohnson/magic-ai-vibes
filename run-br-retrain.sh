#!/bin/zsh
# White-weenie-era retrain: first champion for the 71-card / 10-deck
# pool (BR Midrange, Swamp, artifact-creature copies, demand-ranked
# rocks, complete black audit). Schema shifts again; 71-card
# artifacts retired. Recipe unchanged: headless TD(0.9),
# 900 iters, h128, seed 43. Pool reset => floor bar 0.58 vs HC
# (recent champion-class runs land 62-64 on comparable fields).
set -e
cd "$(dirname "$0")"
LOG=build/telemetry/br-retrain.log
echo "=== br-era headless TD(0.9) retrain ===" > "$LOG"
./build/selfplay-zero train --out data/spz-br-td09.txt \
  --iterations 900 --games 196 --schema-colors --hidden 128 \
  --threads 8 --seed 43 --probe-every 10 --probe-reps 8 \
  --td-lambda 0.9 \
  --telemetry build/telemetry/telemetry.jsonl >> "$LOG" 2>&1
echo "=== gate: benchmark vs handcrafted ===" >> "$LOG"
./build/selfplay-zero benchmark --model data/spz-br-td09.txt \
  --baseline handcrafted --reps 70 --rollout --worlds 4 \
  --threads 10 --seed 777 >> "$LOG" 2>&1
RATE=$(grep -E "^aggregate" "$LOG" | tail -1 | grep -oE "win-rate [0-9.]+" | awk '{print $2}')
echo "br-era gate rate: $RATE (fresh 10-deck era, floor bar 0.58)" >> "$LOG"
PASS=$(python3 -c "print(1 if float('$RATE') >= 0.58 else 0)")
if [ "$PASS" = "1" ]; then
  echo "GATE CLEARED: promoting first br-era champion" >> "$LOG"
  cp data/spz-br-td09.txt data/spz-champion.txt
  make >> "$LOG" 2>&1
  ./build/matchup-matrix --threads 10 > build/telemetry/matrix-regen.log 2>&1
  echo "PROMOTED + matrices regenerated" >> "$LOG"
else
  echo "GATE NOT CLEARED: arena stays on Handcrafted fallback" >> "$LOG"
fi
echo "=== br retrain complete ===" >> "$LOG"
