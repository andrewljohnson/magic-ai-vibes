#!/bin/zsh
# Eight-deck-era champion: the metagame grew to eight real decks
# (Blue Skies and The Deck joined at the 96-card pool), which reset
# the value-net schema — the 88-card nets are shelved as *.96bak.
# Same champion recipe as real-6 (h256, TD 0.9, colors schema, 900
# iters), no --decks restriction: the full field IS the metagame.
# Gate: the eight-deck field vs Handcrafted, seed 777. Era reset means
# a floor bar, not the old champion's score: promote if >= 0.58, else
# leave the arena on Handcrafted fallback and report.
set -e
cd "$(dirname "$0")"
REPORT=build/telemetry/real8-report.txt
mkdir -p build/telemetry
echo "=== real-8 era retrain $(date) ===" > "$REPORT"
START=$(date +%s)
./build/selfplay-zero train --out data/spz-real8.txt \
  --iterations 900 --games 196 --schema-colors --hidden 256 \
  --td-lambda 0.9 --threads 8 --seed 43 --probe-every 10 \
  --probe-reps 8 \
  --telemetry build/telemetry/telemetry.jsonl \
  > build/telemetry/real8.log 2>&1
END=$(date +%s)
echo "training wall time: $(( (END-START)/60 )) min" >> "$REPORT"
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
# Scouting corpus (deck indices in spz_decks() order: 0 = rg-berserk,
# 1 = atog, 2 = br-midrange, 3 = robots, 4 = white-weenie, 5 = uwr,
# 6 = blue-skies, 7 = the-deck): the two NEW decks as the SPZ seat
# vs four of the other decks each.
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
