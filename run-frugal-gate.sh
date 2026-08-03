#!/bin/zsh
# A/B the resurrected frugal tie-break on the identical gate protocol.
# The h256 champion gated 0.6217 with frugality DEAD (it was committed
# behind the retired advantage head's null check and never executed).
# This measures the same net, same seed, same reps with frugality
# alive: the delta is pure decision-time arbitration. Runs after the
# deep128x128 gate so it never contends with that benchmark; note the
# deep gate itself now runs frugality-alive against a frugality-dead
# bar, so interpret its result together with this delta.
set -e
cd "$(dirname "$0")"
REPORT=build/telemetry/frugal-report.txt
echo "=== frugal tie-break A/B $(date) ===" > "$REPORT"
while ! grep -q "deep128x128 gate" build/telemetry/overnight-report.txt 2>/dev/null; do
  sleep 300
done
./build/selfplay-zero benchmark --model data/spz-h256.txt \
  --baseline handcrafted --reps 70 --rollout --worlds 4 \
  --threads 10 --seed 777 > build/telemetry/frugal-gate.log 2>&1
RATE=$(grep -E "^aggregate" build/telemetry/frugal-gate.log | tail -1 | grep -oE "win-rate [0-9.]+" | awk '{print $2}')
echo "h256 frugal-alive gate: $RATE (frugality-dead reference 0.6217)" >> "$REPORT"
echo "h256 probes: $(./build/selfplay-zero behavior-probe --model data/spz-h256.txt --seed 4242 2>/dev/null)" >> "$REPORT"
echo "=== frugal A/B complete $(date) ===" >> "$REPORT"
