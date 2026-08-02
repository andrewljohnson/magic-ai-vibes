#!/bin/zsh
# Headless TD(0.9) confirmation line: current code (factory prunes in
# rollouts), stage 1 ONLY — no advantage head — then the standard gate.
# Bars: deployed champion (with head) = 61.51; its headless ablation =
# 62.34. Promote headless iff >= 0.615, retiring the head stage.
set -e
cd "$(dirname "$0")"
LOG=build/telemetry/headless-td09.log
echo "=== headless TD(0.9) stage 1 (only stage) ===" > "$LOG"
./build/selfplay-zero train --out data/spz-headless-td09.txt \
  --iterations 900 --games 196 --schema-colors --hidden 128 \
  --threads 8 --seed 43 --probe-every 10 --probe-reps 8 \
  --td-lambda 0.9 \
  --telemetry build/telemetry/telemetry.jsonl >> "$LOG" 2>&1
echo "=== gate: headless benchmark vs handcrafted ===" >> "$LOG"
./build/selfplay-zero benchmark --model data/spz-headless-td09.txt \
  --baseline handcrafted --reps 70 --rollout --worlds 4 \
  --threads 10 --seed 777 >> "$LOG" 2>&1
RATE=$(grep -E "^aggregate" "$LOG" | tail -1 | grep -oE "win-rate [0-9.]+" | awk '{print $2}')
echo "headless gate rate: $RATE" >> "$LOG"
PASS=$(python3 -c "print(1 if float('$RATE') >= 0.615 else 0)")
if [ "$PASS" = "1" ]; then
  echo "GATE CLEARED: promoting HEADLESS champion, retiring the head" >> "$LOG"
  cp data/spz-champion.txt data/spz-champion-prev.txt
  [ -f data/spz-advantage.txt ] && mv data/spz-advantage.txt data/spz-advantage-prev.txt
  cp data/spz-headless-td09.txt data/spz-champion.txt
  ./build/matchup-matrix --threads 10 > build/telemetry/matrix-regen.log 2>&1
  echo "PROMOTED headless + matrices regenerated" >> "$LOG"
else
  echo "GATE NOT CLEARED: no promotion (deployed champion stands)" >> "$LOG"
fi
echo "=== headless experiment complete ===" >> "$LOG"
