#!/bin/zsh
# Runs after both TD(0.9) gates: promotes only on a clear paired win
# (champion bar 58.20 on the identical protocol; require +1.0 = ~2 sigma
# at 8,960 games), keeps the prev convention, and regenerates matrices
# including the spz-nohead ablation row.
set -e
cd "$(dirname "$0")"
RATE=$(grep -E "^aggregate" build/telemetry/td09-experiment.log | tail -1 | grep -oE "win-rate [0-9.]+" | awk '{print $2}')
echo "TD(0.9) with-head gate rate: $RATE"
PASS=$(python3 -c "print(1 if float('$RATE') >= 0.592 else 0)")
if [ "$PASS" != "1" ]; then
  echo "GATE NOT CLEARED (needs >= 0.592): no promotion"
  exit 0
fi
echo "GATE CLEARED: promoting TD(0.9) champion"
cp data/spz-champion.txt data/spz-champion-prev.txt
cp data/spz-advantage.txt data/spz-advantage-prev.txt
cp data/spz-td09-stage1.txt data/spz-champion.txt
cp data/spz-td09-advantage.txt data/spz-advantage.txt
./build/matchup-matrix --threads 10 > build/telemetry/matrix-regen.log 2>&1
echo "PROMOTED + matrices regenerated"
