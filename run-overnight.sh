#!/bin/zsh
# Overnight experiment queue. Runs after the in-flight BR-era retrain
# gates. Each stage trains a full 900-iteration candidate and gates it
# on the standard seed-777 reps-70 benchmark; a candidate promotes
# only if it beats the reigning champion's own gate score on the
# identical protocol (spz-prev convention preserved). Stages:
#   1. h256   - capacity: the one never-tested axis (search is
#               net-bound, so this trades throughput for judgment)
#   2. td095  - TD(lambda) sweep high point
#   3. td080  - TD(lambda) sweep low point
# Afterwards: behavior probes on every artifact and a scouting replay
# corpus for all eleven decks, ready for morning review.
set -e
cd "$(dirname "$0")"
REPORT=build/telemetry/overnight-report.txt
echo "=== overnight queue $(date) ===" > "$REPORT"

# Wait for the BR-era retrain (running now) to finish and promote.
while ! grep -q "br retrain complete" build/telemetry/br-retrain.log 2>/dev/null; do
  sleep 120
done
BEST=$(grep -oE "gate rate: [0-9.]+" build/telemetry/br-retrain.log | grep -oE "[0-9.]+" | tail -1)
echo "br-era champion gate: $BEST" >> "$REPORT"

run_candidate() {
  local name=$1; shift
  local extra=("$@")
  local out="data/spz-${name}.txt"
  local log="build/telemetry/${name}.log"
  echo "--- $name: training ---" >> "$REPORT"
  ./build/selfplay-zero train --out "$out" \
    --iterations 900 --games 196 --schema-colors \
    --threads 8 --seed 43 --probe-every 10 --probe-reps 8 \
    --telemetry build/telemetry/telemetry.jsonl \
    "${extra[@]}" > "$log" 2>&1
  ./build/selfplay-zero benchmark --model "$out" \
    --baseline handcrafted --reps 70 --rollout --worlds 4 \
    --threads 10 --seed 777 >> "$log" 2>&1
  local rate=$(grep -E "^aggregate" "$log" | tail -1 | grep -oE "win-rate [0-9.]+" | awk '{print $2}')
  echo "$name gate: $rate (champion bar $BEST)" >> "$REPORT"
  if python3 -c "exit(0 if float('$rate') > float('$BEST') else 1)"; then
    echo "$name PROMOTES" >> "$REPORT"
    cp data/spz-champion.txt data/spz-champion-prev.txt 2>/dev/null || true
    cp "$out" data/spz-champion.txt
    BEST=$rate
  else
    echo "$name rejected" >> "$REPORT"
  fi
}

run_candidate h256 --hidden 256 --td-lambda 0.9
run_candidate td095 --hidden 128 --td-lambda 0.95
run_candidate td080 --hidden 128 --td-lambda 0.8

echo "--- final matrices + probes ---" >> "$REPORT"
./build/matchup-matrix --threads 10 > build/telemetry/matrix-regen.log 2>&1 || true
for artifact in data/spz-br-td09.txt data/spz-h256.txt data/spz-td095.txt data/spz-td080.txt; do
  if [ -f "$artifact" ]; then
    echo "probes $artifact: $(./build/selfplay-zero behavior-probe --model $artifact --seed 4242 2>/dev/null)" >> "$REPORT"
  fi
done

echo "--- scouting corpus (final champion) ---" >> "$REPORT"
for D in 0 1 2 3 4 5 6 7 8 9 10; do
  for I in 0 1 2 3; do
    ./build/replay --model data/spz-champion.txt \
      --seed $((12000 + D * 100 + I * 7)) --spz-deck $D \
      --opp-deck $(( (D + 1 + I * 3) % 11 )) \
      --name "night-d${D}-${I}" > /dev/null 2>&1 || true
  done
done
echo "44 scouting replays written (night-d*-*)" >> "$REPORT"
echo "=== overnight queue complete $(date) ===" >> "$REPORT"
