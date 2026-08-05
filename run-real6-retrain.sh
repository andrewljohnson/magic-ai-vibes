#!/bin/zsh
# First 13-deck-era champion: train on the REAL six-deck field only
# (roadmap decision 2026-08-05 - synthetics retired from training).
# Champion recipe (h256, TD 0.9, colors schema, 900 iters). Gate: the
# six-deck field vs upgraded Handcrafted, seed 777. Era reset means a
# floor bar, not the old champion's score: promote if >= 0.58, else
# leave the arena on Handcrafted fallback and report.
set -e
cd "$(dirname "$0")"
REPORT=build/telemetry/real6-report.txt
DECKS=uwr,robots,white-weenie,br-midrange,rg-berserk,atog
echo "=== real-6 era retrain $(date) ===" > "$REPORT"
START=$(date +%s)
./build/selfplay-zero train --out data/spz-real6.txt \
  --iterations 900 --games 196 --schema-colors --hidden 256 \
  --td-lambda 0.9 --threads 8 --seed 43 --probe-every 10 \
  --probe-reps 8 --decks $DECKS \
  --telemetry build/telemetry/telemetry.jsonl \
  > build/telemetry/real6.log 2>&1
END=$(date +%s)
echo "training wall time: $(( (END-START)/60 )) min" >> "$REPORT"
./build/selfplay-zero benchmark --model data/spz-real6.txt \
  --baseline handcrafted --reps 100 --rollout --worlds 4 \
  --threads 10 --seed 777 --decks $DECKS \
  > build/telemetry/real6-gate.log 2>&1
RATE=$(grep -E "^aggregate" build/telemetry/real6-gate.log | tail -1 | grep -oE "win-rate [0-9.]+" | awk '{print $2}')
echo "real-6 gate: $RATE (floor bar 0.58)" >> "$REPORT"
echo "probes: $(./build/selfplay-zero behavior-probe --model data/spz-real6.txt --seed 4242 2>/dev/null)" >> "$REPORT"
if python3 -c "exit(0 if float('$RATE') >= 0.58 else 1)"; then
  echo "real-6 PROMOTES to era champion" >> "$REPORT"
  cp data/spz-real6.txt data/spz-champion.txt
  cp data/spz-real6.txt data/spz-oldschool.txt
else
  echo "real-6 below floor - arena stays on Handcrafted fallback" >> "$REPORT"
fi
# Scouting corpus for the new decks (11 = rg-berserk, 12 = atog).
for D in 11 12; do
  for I in 0 1 2 3; do
    ./build/replay --model data/spz-real6.txt \
      --seed $((15000 + D * 100 + I * 7)) --spz-deck $D \
      --opp-deck $(( 7 + (D + I) % 4 )) \
      --name "real6-d${D}-${I}" > /dev/null 2>&1 || true
  done
done
echo "8 scouting replays written (real6-d*)" >> "$REPORT"
echo "=== real-6 retrain complete $(date) ===" >> "$REPORT"
