#!/bin/zsh
# BELIEF curve -- replicate the old honest C++ bot's 57.7% recipe on penta by
# giving the value net the hidden-pool BELIEF features it was missing (each
# player's decklist minus everything seen; the old bot had these, penta didn't).
# Schema v2+belief = 1081. WARM-start from the 31.6% baseline GROWN to 1081 with
# zero belief columns (so it starts exactly at baseline, then LEARNS to use the
# belief block). Native path (~0.4 g/s) + exploration. Honest two-tier gates.
# NO auto-redeploy: a 1081 belief net needs the belief head, and the live daemon
# has no --head arg -- deploy manually once a belief net beats 31.6%.
set -e
cd "$(dirname "$0")"
PROGRESS=progress.txt
LATEST=penta_net_belief_latest.npz
BEST_NET=penta_net.belief.best.npz
ENGINE=engine-0.7.0
HEAD=policy_head.belief1081.npz       # 1081 grown head (zero belief cols)
export PENTA_POLICY_NET=$HEAD PENTA_POLICY_WEIGHT=0.15
CHUNKS=${CHUNKS:-"1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16"}
GAMES=${GAMES:-1500}
K_TRAIN=${K_TRAIN:-2}
PROMO_BAR=${PROMO_BAR:-31.6}
RING=${RING:-penta_ring_belief.npz}
LR_WARMUP=${LR_WARMUP:-200}
START_NET=${START_NET:-penta_net.belief1081.npz}  # 31.6% baseline grown to 1081
START_TOTAL=${START_TOTAL:-0}
GATE_AFTER=${GATE_AFTER:-0}        # warm at baseline -> gate every chunk
HONEST_AT=${HONEST_AT:-28}         # cheap K2 %>= this -> honest K4 gate
CHEAP_TIMEOUT=${CHEAP_TIMEOUT:-1800}
HONEST_TIMEOUT=${HONEST_TIMEOUT:-4800}
RUN_BEST=0
BELOW=0
hc_pct() { echo "$1" | sed -E 's/.*-> *([0-9.]+)%.*/\1/' }
gate() { # k games timeout -> gate summary line (belief net + belief head)
  timeout "$3" env PENTA18_DIR=$ENGINE python3 gate_hosted.py --determinized \
    --k-worlds "$1" --games "$2" --value-net "$LATEST" --head "$HEAD" \
    2>/dev/null | tail -1
}

rm -f "$RING"
cp "$START_NET" "$LATEST"
echo "=== BELIEF curve $(date) | warm from $START_NET (1081, belief cols zero) + $HEAD | native K_train=$K_TRAIN, ${GAMES}-game chunks | honest K4/120 gate when cheap>=${HONEST_AT}%, promo ${PROMO_BAR}% | replicate old-C++ hidden-pool recipe ===" >> "$PROGRESS"
TOTAL=$START_TOTAL
for CHUNK in ${=CHUNKS}; do
  PENTA_ENGINE_DIR=$ENGINE python3 trainer.py --native-rows --belief \
    --determinized-k $K_TRAIN --games $GAMES --init "$LATEST" \
    --out "$LATEST" \
    --search-topk 4 --playout-budget 120 \
    --league-handcrafted 0.15 --ring "$RING" --lr-warmup $LR_WARMUP \
    --seed-base $((13000000 + TOTAL)) > "chunk-belief-${CHUNK}.log" 2>&1
  TOTAL=$((TOTAL + GAMES))
  cp "$LATEST" "penta_net.ckpt-belief-${TOTAL}.npz"
  CHEAP=$(gate 2 60 "$CHEAP_TIMEOUT")
  CP=$(hc_pct "$CHEAP"); [[ -z "$CP" ]] && CP=0
  echo "$(date +%H:%M) cumulative-games=$TOTAL | cheap K2/60: $CHEAP" >> "$PROGRESS"
  if awk "BEGIN{exit !($CP >= $HONEST_AT)}"; then
    HON=$(gate 4 120 "$HONEST_TIMEOUT")
    HP=$(hc_pct "$HON"); [[ -z "$HP" ]] && HP=0
    echo "      honest K4/120: $HON" >> "$PROGRESS"
    if awk "BEGIN{exit !($HP > $PROMO_BAR)}"; then
      cp "$LATEST" "$BEST_NET"
      CONF=$(gate 4 400 "$HONEST_TIMEOUT")
      echo "      *** BELIEF BEATS BASELINE: ckpt-belief-${TOTAL} (${HP}% > ${PROMO_BAR}%) | confirm-400: $CONF | manual redeploy w/ belief head ***" >> "$PROGRESS"
      PROMO_BAR=$HP
    fi
    CP=$HP
  fi
  if awk "BEGIN{exit !($CP > $RUN_BEST)}"; then RUN_BEST=$CP; fi
  if awk "BEGIN{exit !($RUN_BEST > 8 && $CP < 0.6 * $RUN_BEST)}"; then
    BELOW=$((BELOW+1)); else BELOW=0; fi
  if [[ "$BELOW" -ge 2 ]]; then
    echo "=== BELIEF curve STOPPED EARLY $(date): two gates < 60% of run-best ${RUN_BEST}% ===" >> "$PROGRESS"; exit 0
  fi
done
echo "=== BELIEF curve complete $(date) ===" >> "$PROGRESS"
