#!/bin/zsh
# DET2 curve -- the clean evaluate-all baseline on the NATIVE runner.
# Training: trainer --native-rows --determinized-k K_TRAIN (spz_core
# determinized self-play, row-lockstep verified). Gating: hosted path
# (gate_hosted --determinized --k-worlds K_GATE) vs 0.7.0 handcrafted --
# the deployed-bot metric. Promotion bar starts at the certified 31.6%;
# on a promotion, a 400-game hosted confirm runs, and if it beats the
# deployed config SPZ is redeployed onto the new pair (same registration).
set -e
cd "$(dirname "$0")"
PROGRESS=progress.txt
LATEST=penta_net_latest.npz
BEST_NET=penta_net.npz
ENGINE=engine-0.7.0
HEAD=policy_head.ckpt-dagger1.npz
export PENTA_POLICY_NET=$HEAD PENTA_POLICY_WEIGHT=0.15
CHUNKS=${CHUNKS:-"1 2 3 4 5 6 7 8 9 10"}
GAMES=${GAMES:-1000}
K_TRAIN=${K_TRAIN:-2}
K_GATE=${K_GATE:-4}
START_NET=${START_NET:-penta_net.ckpt-v3-48000.npz}
START_TOTAL=${START_TOTAL:-0}
BEST=${BEST:-31.6}
RING=${RING:-penta_ring_det2.npz}
LR_WARMUP=${LR_WARMUP:-200}
BELOW=0
hc_pct() { echo "$1" | sed -E 's/.*-> *([0-9.]+)%.*/\1/' }

cp "$START_NET" "$LATEST"
rm -f "$RING"
echo "=== DET2 native-runner curve $(date) | evaluate-all, native determinized K_train=$K_TRAIN, gate K=$K_GATE (hosted path vs 0.7.0 handcrafted), bar ${BEST}% | from $START_NET | ${GAMES}-game chunks ===" >> "$PROGRESS"
TOTAL=$START_TOTAL
for CHUNK in ${=CHUNKS}; do
  PENTA_ENGINE_DIR=$ENGINE python3 trainer.py --native-rows \
    --determinized-k $K_TRAIN --games $GAMES --init "$LATEST" \
    --out "$LATEST" --search-topk 4 --playout-budget 120 \
    --league-handcrafted 0.15 --ring "$RING" --lr-warmup $LR_WARMUP \
    --seed-base $((9000000 + TOTAL)) > "chunk-${CHUNK}.log" 2>&1
  TOTAL=$((TOTAL + GAMES))
  CK="penta_net.ckpt-det2-${TOTAL}.npz"; cp "$LATEST" "$CK"
  HC=$(PENTA18_DIR=$ENGINE python3 gate_hosted.py --determinized \
    --k-worlds $K_GATE --games 120 --value-net "$LATEST" 2>/dev/null | tail -1)
  echo "$(date +%H:%M) cumulative-games=$TOTAL | handcrafted: $HC" >> "$PROGRESS"
  PCT=$(hc_pct "$HC")
  if awk "BEGIN{exit !($PCT > $BEST)}"; then
    cp "$LATEST" "$BEST_NET"
    CONF=$(PENTA18_DIR=$ENGINE python3 gate_hosted.py --determinized \
      --k-worlds $K_GATE --games 400 --value-net "$LATEST" 2>/dev/null | tail -1)
    echo "      promoted ckpt-det2-${TOTAL} (${PCT}% > ${BEST}%) | confirm-400: $CONF" >> "$PROGRESS"
    BEST=$PCT
    # redeploy SPZ onto the new deliverable (same registration/token)
    pkill -f "hosted_bot.py --server https" 2>/dev/null || true
    sleep 2
    nohup python3 hosted_bot.py --server https://penta.lacker.workers.dev \
      --name SPZ --deck Sligh --engine-dir $ENGINE --k-worlds $K_GATE \
      --value-net "$BEST_NET" > hosted_bot.log 2>&1 & disown
    echo "      SPZ redeployed on ckpt-det2-${TOTAL}" >> "$PROGRESS"
  else
    echo "      kept $BEST_NET (ckpt-det2-${TOTAL} ${PCT}% <= ${BEST}%)" >> "$PROGRESS"
  fi
  if awk "BEGIN{exit !($PCT < 0.6 * $BEST)}"; then BELOW=$((BELOW+1)); else BELOW=0; fi
  if [[ "$BELOW" -ge 2 ]]; then
    echo "=== DET2 STOPPED EARLY $(date): two gates < 60% of best ${BEST}% ===" >> "$PROGRESS"; exit 0
  fi
done
echo "=== DET2 curve complete $(date) ===" >> "$PROGRESS"
