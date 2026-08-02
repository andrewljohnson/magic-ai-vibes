#!/bin/zsh
# Robots-era retrain: first champion for the 64-card / 9-deck metagame
# (black mana, Robots deck, renamed decks, Su-Chi rebate, copy faces).
# The 419-schema champion was retired with the schema change (colors
# schema is now 423 inputs: +black pool, +pending mana per player), so
# the arena is on the Handcrafted fallback until this promotes.
# Single-stage headless TD(0.9) pipeline, same recipe that won the
# 62.40% gate on the old pool. Bar: >= 0.55 vs Handcrafted on the
# standard gate (seed 777, reps 70 = 8,960 games; LCB ~ rate - 1%).
set -e
cd "$(dirname "$0")"
LOG=build/telemetry/robots-retrain.log
echo "=== Robots-era headless TD(0.9) retrain ===" > "$LOG"
./build/selfplay-zero train --out data/spz-robots-td09.txt \
  --iterations 900 --games 196 --schema-colors --hidden 128 \
  --threads 8 --seed 43 --probe-every 10 --probe-reps 8 \
  --td-lambda 0.9 \
  --telemetry build/telemetry/telemetry.jsonl >> "$LOG" 2>&1
echo "=== gate: benchmark vs handcrafted ===" >> "$LOG"
./build/selfplay-zero benchmark --model data/spz-robots-td09.txt \
  --baseline handcrafted --reps 70 --rollout --worlds 4 \
  --threads 10 --seed 777 >> "$LOG" 2>&1
RATE=$(grep -E "^aggregate" "$LOG" | tail -1 | grep -oE "win-rate [0-9.]+" | awk '{print $2}')
echo "robots-era gate rate: $RATE" >> "$LOG"
PASS=$(python3 -c "print(1 if float('$RATE') >= 0.55 else 0)")
if [ "$PASS" = "1" ]; then
  echo "GATE CLEARED: promoting first Robots-era champion" >> "$LOG"
  cp data/spz-robots-td09.txt data/spz-champion.txt
  make >> "$LOG" 2>&1
  ./build/matchup-matrix --threads 10 > build/telemetry/matrix-regen.log 2>&1
  echo "PROMOTED + matrices regenerated" >> "$LOG"
else
  echo "GATE NOT CLEARED: arena stays on Handcrafted fallback" >> "$LOG"
fi
echo "=== retrain complete ===" >> "$LOG"
