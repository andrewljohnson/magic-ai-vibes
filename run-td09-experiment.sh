#!/bin/zsh
# TD(lambda)=0.9 experiment: fresh stage-1 + rollout head into separate
# artifacts, then the paired gate benchmark vs the promoted champion's
# identical protocol (seed 777, reps 70). NO promotion here — report only.
set -e
cd "$(dirname "$0")"
LOG=build/telemetry/td09-experiment.log
echo "=== TD(0.9) stage 1 ===" > "$LOG"
./build/selfplay-zero train --out data/spz-td09-stage1.txt \
  --iterations 900 --games 196 --schema-colors --hidden 128 \
  --threads 8 --seed 42 --probe-every 10 --probe-reps 8 \
  --td-lambda 0.9 \
  --telemetry build/telemetry/td09-telemetry.jsonl >> "$LOG" 2>&1
echo "=== TD(0.9) stage 2: advantage head ===" >> "$LOG"
./build/selfplay-zero train --out data/spz-td09-stage1.txt \
  --init data/spz-td09-stage1.txt --no-value-training \
  --advantage-out data/spz-td09-advantage.txt \
  --iterations 150 --games 98 --schema-colors --rollout \
  --td-lambda 0.9 \
  --threads 8 --seed 4242 >> "$LOG" 2>&1
echo "=== TD(0.9) gate: benchmark vs handcrafted (paired protocol) ===" >> "$LOG"
./build/selfplay-zero benchmark --model data/spz-td09-stage1.txt \
  --advantage data/spz-td09-advantage.txt \
  --baseline handcrafted --reps 70 --rollout --worlds 4 \
  --threads 10 --seed 777 >> "$LOG" 2>&1
echo "=== TD(0.9) experiment complete (gate report only, no promotion) ===" >> "$LOG"
