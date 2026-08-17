#!/bin/zsh
# INERT curve -- the FULL old-C++ recipe, built the way the old bot built it:
# FRESH net (no warm-start) + belief features + INERT single-state search
# (opponent hidden cards benign/lands-first, K=1, no PIMC sampling) + explore.
# Value co-adapts to inert rollout dynamics from scratch. Native path, gate
# ALSO inert. Fresh net climbs from ~0, so warmup before gating; early-stop
# tracks THIS curve's running best (not the promo bar). No auto-redeploy
# (1081 belief net needs the belief head; deploy manually if it beats 31.6%).
set -e
cd "$(dirname "$0")"
PROGRESS=progress.txt
LATEST=penta_net_inert_latest.npz
BEST_NET=penta_net.inert.best.npz
ENGINE=engine-0.7.0
HEAD=policy_head.belief1081.npz
export PENTA_POLICY_NET=$HEAD PENTA_POLICY_WEIGHT=0.15
CHUNKS=${CHUNKS:-"1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20"}
GAMES=${GAMES:-1500}
K_TRAIN=${K_TRAIN:-2}
PROMO_BAR=${PROMO_BAR:-31.6}
RING=${RING:-penta_ring_inert.npz}
LR_WARMUP=${LR_WARMUP:-200}
GATE_AFTER=${GATE_AFTER:-6000}     # fresh-net warmup before gating
HONEST_AT=${HONEST_AT:-28}
CHEAP_TIMEOUT=${CHEAP_TIMEOUT:-1800}
HONEST_TIMEOUT=${HONEST_TIMEOUT:-4800}
RUN_BEST=0
BELOW=0
hc_pct() { echo "$1" | sed -E 's/.*-> *([0-9.]+)%.*/\1/' }
gate() { # k games timeout -> INERT gate line (K collapses to 1)
  timeout "$3" env PENTA18_DIR=$ENGINE python3 gate_hosted.py --determinized --inert \
    --k-worlds "$1" --games "$2" --value-net "$LATEST" --head "$HEAD" \
    2>/dev/null | tail -1
}

rm -f "$RING" "$LATEST"
echo "=== INERT curve $(date) | FRESH 1081 (belief+inert), no warm-start, + $HEAD | native K_train=$K_TRAIN inert, ${GAMES}-game chunks | inert gates, warmup<${GATE_AFTER}g, honest K4 when cheap>=${HONEST_AT}%, promo ${PROMO_BAR}% | full old-C++ recipe ===" >> "$PROGRESS"
TOTAL=0
for CHUNK in ${=CHUNKS}; do
  if [[ "$TOTAL" -eq 0 ]]; then INIT_ARG=(); else INIT_ARG=(--init "$LATEST"); fi
  PENTA_ENGINE_DIR=$ENGINE python3 trainer.py --native-rows --belief --inert \
    --determinized-k $K_TRAIN --games $GAMES "${INIT_ARG[@]}" \
    --out "$LATEST" \
    --search-topk 4 --playout-budget 120 \
    --league-handcrafted 0.15 --ring "$RING" --lr-warmup $LR_WARMUP \
    --seed-base $((14000000 + TOTAL)) > "chunk-inert-${CHUNK}.log" 2>&1
  TOTAL=$((TOTAL + GAMES))
  cp "$LATEST" "penta_net.ckpt-inert-${TOTAL}.npz"
  if [[ "$TOTAL" -lt "$GATE_AFTER" ]]; then
    echo "$(date +%H:%M) cumulative-games=$TOTAL | warmup (no gate < ${GATE_AFTER}g)" >> "$PROGRESS"
    continue
  fi
  CHEAP=$(gate 2 60 "$CHEAP_TIMEOUT")
  CP=$(hc_pct "$CHEAP"); [[ -z "$CP" ]] && CP=0
  echo "$(date +%H:%M) cumulative-games=$TOTAL | inert cheap K2/60: $CHEAP" >> "$PROGRESS"
  if awk "BEGIN{exit !($CP >= $HONEST_AT)}"; then
    HON=$(gate 4 120 "$HONEST_TIMEOUT")
    HP=$(hc_pct "$HON"); [[ -z "$HP" ]] && HP=0
    echo "      inert honest K4/120: $HON" >> "$PROGRESS"
    if awk "BEGIN{exit !($HP > $PROMO_BAR)}"; then
      cp "$LATEST" "$BEST_NET"
      CONF=$(gate 4 400 "$HONEST_TIMEOUT")
      echo "      *** INERT BEATS BASELINE: ckpt-inert-${TOTAL} (${HP}% > ${PROMO_BAR}%) | confirm-400: $CONF | manual redeploy w/ belief head ***" >> "$PROGRESS"
      PROMO_BAR=$HP
    fi
    CP=$HP
  fi
  if awk "BEGIN{exit !($CP > $RUN_BEST)}"; then RUN_BEST=$CP; fi
  if awk "BEGIN{exit !($RUN_BEST > 8 && $CP < 0.6 * $RUN_BEST)}"; then
    BELOW=$((BELOW+1)); else BELOW=0; fi
  if [[ "$BELOW" -ge 2 ]]; then
    echo "=== INERT curve STOPPED EARLY $(date): two gates < 60% of run-best ${RUN_BEST}% ===" >> "$PROGRESS"; exit 0
  fi
done
echo "=== INERT curve complete $(date) ===" >> "$PROGRESS"
