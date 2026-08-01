#!/bin/zsh
# UWR-era champion pipeline: first retrain on the 47-card schema with
# Paris mulligans live. Stage 1 value net (v11 recipe: colors schema,
# h128, 900 iterations), stage 2 counterfactual advantage head on the
# frozen net, benchmark vs Handcrafted, then canonical promotion.
set -e
cd "$(dirname "$0")"
LOG=build/telemetry/uwr-v1-pipeline.log
echo "=== stage 1: UWR-era value net (colors schema, h128, 900 iters) ===" >> "$LOG"
./build/selfplay-zero train --out data/spz-uwr-stage1.txt \
  --iterations 900 --games 196 --schema-colors --hidden 128 \
  --threads 10 --seed 42 --probe-every 10 --probe-reps 50 \
  --telemetry build/telemetry/telemetry.jsonl >> "$LOG" 2>&1
echo "=== stage 2: advantage head on frozen stage-1 net ===" >> "$LOG"
./build/selfplay-zero train --out data/spz-uwr-stage1.txt \
  --init data/spz-uwr-stage1.txt --no-value-training \
  --advantage-out data/spz-advantage-uwr.txt \
  --iterations 150 --games 98 --schema-colors \
  --threads 10 --seed 4242 >> "$LOG" 2>&1
echo "=== stage 3: benchmark vs handcrafted ===" >> "$LOG"
./build/selfplay-zero benchmark --model data/spz-uwr-stage1.txt \
  --advantage data/spz-advantage-uwr.txt \
  --baseline handcrafted --reps 70 --rollout --worlds 4 \
  --threads 10 --seed 777 >> "$LOG" 2>&1
echo "=== promotion: canonical champion + matrices ===" >> "$LOG"
cp data/spz-uwr-stage1.txt data/spz-champion.txt
cp data/spz-advantage-uwr.txt data/spz-advantage.txt
./build/matchup-matrix >> "$LOG" 2>&1 || \
  echo "matchup-matrix failed (check flags)" >> "$LOG"
echo "=== pipeline complete ===" >> "$LOG"
