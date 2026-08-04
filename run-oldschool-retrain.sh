#!/bin/zsh
# Old-school specialist: train ONLY on the four "real" decks
# (Lion-dib-bolt, Robots, White Weenie, BR Midrange) with the champion
# recipe (h256, TD 0.9), then compare specialist vs the 11-deck h256
# champion on the IDENTICAL 4-deck seed-777 protocol. Also reports the
# training wall time against the ~4.5h an 11-deck 900-iter run takes.
# Chained behind the sqrt-band frugality gate so benchmarks never
# contend.
set -e
cd "$(dirname "$0")"
REPORT=build/telemetry/oldschool-report.txt
DECKS=uwr,robots,white-weenie,br-midrange
echo "=== old-school specialist $(date) ===" > "$REPORT"
while ! grep -q "sqrt band gate complete" build/telemetry/frugal-report.txt 2>/dev/null; do
  sleep 300
done
START=$(date +%s)
./build/selfplay-zero train --out data/spz-oldschool.txt \
  --iterations 900 --games 196 --schema-colors --hidden 256 \
  --td-lambda 0.9 --threads 8 --seed 43 --probe-every 10 \
  --probe-reps 8 --decks $DECKS \
  --telemetry build/telemetry/telemetry.jsonl \
  > build/telemetry/oldschool.log 2>&1
END=$(date +%s)
echo "training wall time: $(( (END-START)/60 )) min (11-deck h256 took ~270)" >> "$REPORT"
for M in data/spz-oldschool.txt data/spz-h256.txt; do
  NAME=$(basename $M .txt)
  ./build/selfplay-zero benchmark --model $M --baseline handcrafted \
    --reps 150 --rollout --worlds 4 --threads 10 --seed 777 \
    --decks $DECKS > build/telemetry/oldschool-gate-$NAME.log 2>&1
  RATE=$(grep -E "^aggregate" build/telemetry/oldschool-gate-$NAME.log | tail -1 | grep -oE "win-rate [0-9.]+" | awk '{print $2}')
  echo "$NAME 4-deck gate: $RATE" >> "$REPORT"
done
echo "probes: $(./build/selfplay-zero behavior-probe --model data/spz-oldschool.txt --seed 4242 2>/dev/null)" >> "$REPORT"
echo "=== old-school specialist complete $(date) ===" >> "$REPORT"
