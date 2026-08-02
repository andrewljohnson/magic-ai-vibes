#!/bin/zsh
# Race-features retrain: colors schema 423 -> 435 (+12 observer-relative
# race features: clocks, turns-to-lethal, lethal flags, evasive clocks)
# plus two new behavior probes (race-removal, counter-respect). Recipe
# unchanged: headless TD(0.9), 900 iters, h128, seed 43. Bar: beat the
# deployed champion's own gate score (0.6325 on seed 777 reps 70).
# Falsification prior: v2's race block never beat v1 as a wholesale
# swap; this is its first incremental test.
set -e
cd "$(dirname "$0")"
LOG=build/telemetry/race-retrain.log
echo "=== race-features headless TD(0.9) retrain ===" > "$LOG"
./build/selfplay-zero train --out data/spz-race-td09.txt \
  --iterations 900 --games 196 --schema-colors --hidden 128 \
  --threads 8 --seed 43 --probe-every 10 --probe-reps 8 \
  --td-lambda 0.9 \
  --telemetry build/telemetry/telemetry.jsonl >> "$LOG" 2>&1
echo "=== gate: benchmark vs handcrafted ===" >> "$LOG"
./build/selfplay-zero benchmark --model data/spz-race-td09.txt \
  --baseline handcrafted --reps 70 --rollout --worlds 4 \
  --threads 10 --seed 777 >> "$LOG" 2>&1
RATE=$(grep -E "^aggregate" "$LOG" | tail -1 | grep -oE "win-rate [0-9.]+" | awk '{print $2}')
echo "race-features gate rate: $RATE (champion bar 0.6325)" >> "$LOG"
PASS=$(python3 -c "print(1 if float('$RATE') >= 0.6325 else 0)")
if [ "$PASS" = "1" ]; then
  echo "GATE CLEARED: promoting race-features champion" >> "$LOG"
  cp data/spz-champion.txt data/spz-champion-prev.txt
  cp data/spz-race-td09.txt data/spz-champion.txt
  make >> "$LOG" 2>&1
  ./build/matchup-matrix --threads 10 > build/telemetry/matrix-regen.log 2>&1
  echo "PROMOTED + matrices regenerated" >> "$LOG"
else
  echo "GATE NOT CLEARED: 63.25% champion stands (435-schema challenger recorded)" >> "$LOG"
fi
echo "=== race retrain complete ===" >> "$LOG"
