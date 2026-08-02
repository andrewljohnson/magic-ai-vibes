#!/bin/zsh
set -e
cd "$(dirname "$0")"
LOG=build/telemetry/uwr-v1-pipeline.log
echo "=== stage 2 (resumed): advantage head on frozen stage-1 net ===" >> "$LOG"
./build/selfplay-zero train --out data/spz-uwr-stage1.txt \
  --init data/spz-uwr-stage1.txt --no-value-training \
  --advantage-out data/spz-advantage-uwr.txt \
  --iterations 150 --games 98 --schema-colors --rollout \
  --threads 8 --seed 4242 >> "$LOG" 2>&1
echo "=== stage 3: benchmark vs handcrafted ===" >> "$LOG"
./build/selfplay-zero benchmark --model data/spz-uwr-stage1.txt \
  --advantage data/spz-advantage-uwr.txt \
  --baseline handcrafted --reps 70 --rollout --worlds 4 \
  --threads 10 --seed 777 >> "$LOG" 2>&1
echo "=== promotion: canonical champion + matrices ===" >> "$LOG"
cp data/spz-uwr-stage1.txt data/spz-champion.txt
cp data/spz-advantage-uwr.txt data/spz-advantage.txt
./build/matchup-matrix >> "$LOG" 2>&1 || echo "matchup-matrix failed" >> "$LOG"
echo "=== pipeline complete ===" >> "$LOG"
