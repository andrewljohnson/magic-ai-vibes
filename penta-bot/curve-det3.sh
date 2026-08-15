#!/bin/zsh
# DET3 curve -- FRESH value net + exploration, TRUSTED Python determinized
# path (NOT the native runner). det2 plateaued at 26-28% because the native
# runner it used was greedy (epsilon 0); det3's fix is exploration ON via
# trainer.py's reference self-play (choose() epsilon branch), which needs no
# native lockstep. Net starts FRESH (seed=1, v3 schema) -- the warm-start
# ckpt-v3-48000 basin is shaped by the closed-era true-state leak, disproven
# as a start. Policy head (dagger1, honest imitation) is kept @ 0.15.
#
# Training: trainer (Python) --determinized-k K_TRAIN, epsilon-greedy.
# Gating: hosted path (gate_hosted --determinized --k-worlds K_GATE) vs
# 0.7.0 handcrafted -- the deployed-bot metric.
# Promotion: beat PROMO_BAR (certified 31.6%) -> 400-game confirm ->
# redeploy SPZ onto the new pair (same registration/token).
# Early-stop: decoupled from the promo bar. A fresh net climbs from ~0, so
# early-stop watches a RUNNING best of THIS curve's gates: two consecutive
# gates below 60% of the running best (a collapse), not below the promo bar.
set -e
cd "$(dirname "$0")"
PROGRESS=progress.txt
LATEST=penta_net_latest.npz
BEST_NET=penta_net.npz
ENGINE=engine-0.7.0
HEAD=policy_head.ckpt-dagger1.npz
export PENTA_POLICY_NET=$HEAD PENTA_POLICY_WEIGHT=0.15
CHUNKS=${CHUNKS:-"1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16"}
GAMES=${GAMES:-1000}
K_TRAIN=${K_TRAIN:-2}
K_GATE=${K_GATE:-4}
PROMO_BAR=${PROMO_BAR:-31.6}   # beat this to promote + redeploy SPZ
RING=${RING:-penta_ring_det3.npz}
LR_WARMUP=${LR_WARMUP:-200}
WORKERS=${WORKERS:-8}
RUN_BEST=0                      # running max gate of THIS curve (early-stop ref)
BELOW=0
hc_pct() { echo "$1" | sed -E 's/.*-> *([0-9.]+)%.*/\1/' }

rm -f "$RING" "$LATEST"
echo "=== DET3 FRESH+exploration curve $(date) | Python det path, epsilon-greedy, K_train=$K_TRAIN, gate K=$K_GATE (hosted vs 0.7.0 handcrafted), promo bar ${PROMO_BAR}% | FRESH net (v3, no warm-start) + dagger1 head @0.15 | ${GAMES}-game chunks ===" >> "$PROGRESS"
TOTAL=0
for CHUNK in ${=CHUNKS}; do
  # chunk 1 starts FRESH (no --init); later chunks warm from LATEST.
  if [[ "$TOTAL" -eq 0 ]]; then INIT_ARG=(); else INIT_ARG=(--init "$LATEST"); fi
  PENTA_ENGINE_DIR=$ENGINE python3 trainer.py \
    --determinized-k $K_TRAIN --games $GAMES "${INIT_ARG[@]}" \
    --out "$LATEST" --workers $WORKERS \
    --search-topk 4 --playout-budget 120 \
    --league-handcrafted 0.15 --ring "$RING" --lr-warmup $LR_WARMUP \
    --seed-base $((11000000 + TOTAL)) > "chunk-det3-${CHUNK}.log" 2>&1
  TOTAL=$((TOTAL + GAMES))
  CK="penta_net.ckpt-det3-${TOTAL}.npz"; cp "$LATEST" "$CK"
  HC=$(PENTA18_DIR=$ENGINE python3 gate_hosted.py --determinized \
    --k-worlds $K_GATE --games 120 --value-net "$LATEST" 2>/dev/null | tail -1)
  echo "$(date +%H:%M) cumulative-games=$TOTAL | handcrafted: $HC" >> "$PROGRESS"
  PCT=$(hc_pct "$HC")
  # promotion: beat the certified bar -> confirm-400 -> redeploy SPZ
  if awk "BEGIN{exit !($PCT > $PROMO_BAR)}"; then
    cp "$LATEST" "$BEST_NET"
    CONF=$(PENTA18_DIR=$ENGINE python3 gate_hosted.py --determinized \
      --k-worlds $K_GATE --games 400 --value-net "$LATEST" 2>/dev/null | tail -1)
    echo "      PROMOTED ckpt-det3-${TOTAL} (${PCT}% > ${PROMO_BAR}%) | confirm-400: $CONF" >> "$PROGRESS"
    PROMO_BAR=$PCT
    pkill -f "hosted_bot.py --server https" 2>/dev/null || true
    sleep 2
    nohup python3 hosted_bot.py --server https://penta.lacker.workers.dev \
      --name SPZ --deck Sligh --weight 0.15 --engine-dir $ENGINE \
      --k-worlds $K_GATE --value-net "$BEST_NET" \
      > hosted_daemon.log 2>&1 & disown
    echo "      SPZ redeployed on ckpt-det3-${TOTAL}" >> "$PROGRESS"
  else
    echo "      kept $BEST_NET (ckpt-det3-${TOTAL} ${PCT}% <= promo bar ${PROMO_BAR}%)" >> "$PROGRESS"
  fi
  # early-stop watches THIS curve's running best (a fresh net climbs from 0)
  if awk "BEGIN{exit !($PCT > $RUN_BEST)}"; then RUN_BEST=$PCT; fi
  if awk "BEGIN{exit !($RUN_BEST > 5 && $PCT < 0.6 * $RUN_BEST)}"; then
    BELOW=$((BELOW+1)); else BELOW=0; fi
  if [[ "$BELOW" -ge 2 ]]; then
    echo "=== DET3 STOPPED EARLY $(date): two gates < 60% of run-best ${RUN_BEST}% ===" >> "$PROGRESS"; exit 0
  fi
done
echo "=== DET3 curve complete $(date) ===" >> "$PROGRESS"
