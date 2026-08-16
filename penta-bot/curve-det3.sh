#!/bin/zsh
# DET3 curve v2 -- FRESH value net + exploration, TRUSTED Python determinized
# path. v1 died on gate cadence: gating a FRESH net at K=4/120 took 5+ HOURS
# (a weak net plays 400-decision games; K=4 reconstructs+searches every move).
# Early gates are also pure waste -- a fresh net is nowhere near the 31.6% bar
# for thousands of games. v2 fixes cadence with a two-tier, warmup-gated,
# timeout-capped gate:
#   * no gating until GATE_AFTER cumulative games (fresh-net warmup)
#   * CHEAP tracking gate K=2/80 each chunk (fast climb signal), hard timeout
#   * HONEST gate K=4/120 only when the cheap gate >= HONEST_AT (near the bar)
#   * PROMOTE: honest > PROMO_BAR -> K=4/400 confirm -> redeploy SPZ
# Resumes from the v1 chunk-1 checkpoint so that work isn't wasted.
set -e
cd "$(dirname "$0")"
PROGRESS=progress.txt
LATEST=penta_net_latest.npz
BEST_NET=penta_net.npz
ENGINE=engine-0.7.0
HEAD=policy_head.ckpt-dagger1.npz
export PENTA_POLICY_NET=$HEAD PENTA_POLICY_WEIGHT=0.15
CHUNKS=${CHUNKS:-"1 2 3 4 5 6 7 8 9 10 11 12"}
GAMES=${GAMES:-2000}
K_TRAIN=${K_TRAIN:-2}
PROMO_BAR=${PROMO_BAR:-31.6}
RING=${RING:-penta_ring_det3.npz}
LR_WARMUP=${LR_WARMUP:-200}
WORKERS=${WORKERS:-8}
START_NET=${START_NET:-penta_net.ckpt-det3-1000.npz}  # resume v1 chunk-1
START_TOTAL=${START_TOTAL:-1000}
GATE_AFTER=${GATE_AFTER:-5000}   # no gating below this cumulative-games
HONEST_AT=${HONEST_AT:-22}       # cheap %>= this -> run the honest K=4 gate
CHEAP_TIMEOUT=${CHEAP_TIMEOUT:-2400}   # 40m hard cap on a tracking gate
HONEST_TIMEOUT=${HONEST_TIMEOUT:-4800} # 80m hard cap on an honest gate
RUN_BEST=0
BELOW=0
hc_pct() { echo "$1" | sed -E 's/.*-> *([0-9.]+)%.*/\1/' }
gate() { # k games timeout -> prints the gate summary line (or a timeout note)
  timeout "$3" env PENTA18_DIR=$ENGINE python3 gate_hosted.py --determinized \
    --k-worlds "$1" --games "$2" --value-net "$LATEST" 2>/dev/null | tail -1
}

cp "$START_NET" "$LATEST"
echo "=== DET3 v2 curve $(date) | resume from $START_NET (cum=$START_TOTAL) | Python det, K_train=$K_TRAIN, ${GAMES}-game chunks | gate: none<${GATE_AFTER}g, cheap K2/80, honest K4/120 when cheap>=${HONEST_AT}%, promo ${PROMO_BAR}% ===" >> "$PROGRESS"
TOTAL=$START_TOTAL
for CHUNK in ${=CHUNKS}; do
  PENTA_ENGINE_DIR=$ENGINE python3 trainer.py \
    --determinized-k $K_TRAIN --games $GAMES --init "$LATEST" \
    --out "$LATEST" --workers $WORKERS \
    --search-topk 4 --playout-budget 120 \
    --league-handcrafted 0.15 --ring "$RING" --lr-warmup $LR_WARMUP \
    --seed-base $((12000000 + TOTAL)) > "chunk-det3-${CHUNK}.log" 2>&1
  TOTAL=$((TOTAL + GAMES))
  CK="penta_net.ckpt-det3-${TOTAL}.npz"; cp "$LATEST" "$CK"
  if [[ "$TOTAL" -lt "$GATE_AFTER" ]]; then
    echo "$(date +%H:%M) cumulative-games=$TOTAL | warmup (no gate < ${GATE_AFTER}g)" >> "$PROGRESS"
    continue
  fi
  CHEAP=$(gate 2 80 "$CHEAP_TIMEOUT")
  CP=$(hc_pct "$CHEAP"); [[ -z "$CP" ]] && CP=0
  echo "$(date +%H:%M) cumulative-games=$TOTAL | cheap K2/80: $CHEAP" >> "$PROGRESS"
  # honest gate only when the cheap tracking signal is near the bar
  if awk "BEGIN{exit !($CP >= $HONEST_AT)}"; then
    HON=$(gate 4 120 "$HONEST_TIMEOUT")
    HP=$(hc_pct "$HON"); [[ -z "$HP" ]] && HP=0
    echo "      honest K4/120: $HON" >> "$PROGRESS"
    if awk "BEGIN{exit !($HP > $PROMO_BAR)}"; then
      cp "$LATEST" "$BEST_NET"
      CONF=$(gate 4 400 "$HONEST_TIMEOUT")
      echo "      PROMOTED ckpt-det3-${TOTAL} (${HP}% > ${PROMO_BAR}%) | confirm-400: $CONF" >> "$PROGRESS"
      PROMO_BAR=$HP
      pkill -f "hosted_bot.py --server https" 2>/dev/null || true
      sleep 2
      nohup python3 hosted_bot.py --server https://penta.lacker.workers.dev \
        --name SPZ --deck Sligh --weight 0.15 --engine-dir $ENGINE \
        --k-worlds 4 --value-net "$BEST_NET" > hosted_daemon.log 2>&1 & disown
      echo "      SPZ redeployed on ckpt-det3-${TOTAL}" >> "$PROGRESS"
    fi
    CP=$HP  # track climb on the honest number once we have it
  fi
  if awk "BEGIN{exit !($CP > $RUN_BEST)}"; then RUN_BEST=$CP; fi
  if awk "BEGIN{exit !($RUN_BEST > 8 && $CP < 0.6 * $RUN_BEST)}"; then
    BELOW=$((BELOW+1)); else BELOW=0; fi
  if [[ "$BELOW" -ge 2 ]]; then
    echo "=== DET3 v2 STOPPED EARLY $(date): two gates < 60% of run-best ${RUN_BEST}% ===" >> "$PROGRESS"; exit 0
  fi
done
echo "=== DET3 v2 curve complete $(date) ===" >> "$PROGRESS"
