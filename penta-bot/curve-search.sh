#!/bin/zsh
# SEARCH-ERA learning-curve run: league play + gate-guarded promotion with
# the rollout-search recipe ON at both train and gate time.
#
# Continues from the promoted search-smoke deliverable (penta_net.npz =
# penta_net_latest.npz = ckpt-search-3000, handcrafted gate 33.3% with
# --search-topk 4) and runs $CHUNKS x 2000 games. Per chunk: train with
# search (trainer defaults: topk 4, 1 playout, budget 120,
# playout-max-eval 8 -- the exact smoke recipe), league play (15%
# handcrafted + prev-chunk frozen snapshot), ring continuation
# (penta_ring.npz threads across chunks; NEVER deleted here), lr-warmup,
# snapshot to penta_net.ckpt-search-<cumulative>.npz (the 1-ply
# ckpt-<N>.npz history is never touched), gate 60 vs random + 120 vs
# handcrafted with SEARCH-ENABLED gates on the fixed gate seeds, and
# promote to penta_net.npz only on beating the running best.
#
# The promotion bar starts at the search-smoke gate (33.3%): the gate is
# deterministic (fixed seeds, deterministic engine/net), so re-gating the
# identical start net would reproduce it exactly -- no baseline re-gate,
# and the deliverable is never demoted by a baseline copy.
#
# Seeds: chunk seed-base = 2000000 + cumulative, a fresh range disjoint
# from every 1-ply-era chunk (1000000..1025000), the search smoke
# (1000000..1002999), and the gate seeds (5000000+).
#
# Early stop: a handcrafted gate below $STOP_BELOW% is collapse
# territory; the run stops instead of burning the remaining chunks.
set -e
cd "$(dirname "$0")"
PROGRESS=progress.txt
LATEST=penta_net_latest.npz
BEST_NET=penta_net.npz
CHUNKS=${CHUNKS:-"1 2 3 4"}
RING=${RING:-penta_ring.npz}
LR_WARMUP=${LR_WARMUP:-200}
WORKERS=${WORKERS:-8}
SEARCH_TOPK=${SEARCH_TOPK:-4}
START_TOTAL=${START_TOTAL:-3000}
BEST=${BEST:-33.3}
STOP_BELOW=${STOP_BELOW:-20}

hc_pct() { echo "$1" | sed -E 's/.*-> *([0-9.]+)%.*/\1/' }

TOTAL=$START_TOTAL
echo "=== search-era curve run $(date) | search topk $SEARCH_TOPK at train+gate | league: handcrafted 0.15 + prev-chunk frozen 0.25, eps 0.10->0.08, td-lambda 1.0, ring continued + lr-warmup $LR_WARMUP | from $LATEST (ckpt-search-$TOTAL, $TOTAL games) | promotion bar $BEST% (search smoke gate) ===" >> "$PROGRESS"

for CHUNK in ${=CHUNKS}; do
  FROZEN="penta_net.ckpt-search-${TOTAL}.npz"
  python3 trainer.py --games 2000 --init "$LATEST" --out "$LATEST" \
    --workers "$WORKERS" \
    --search-topk "$SEARCH_TOPK" \
    --epsilon-start 0.10 --epsilon-final 0.08 \
    --league-handcrafted 0.15 --league-frozen "$FROZEN" \
    --ring "$RING" --lr-warmup "$LR_WARMUP" \
    --seed-base $((2000000 + TOTAL)) \
    > "chunk-${CHUNK}.log" 2>&1
  TOTAL=$((TOTAL + 2000))
  cp "$LATEST" "penta_net.ckpt-search-${TOTAL}.npz"
  RAND=$(python3 gate.py --net "$LATEST" --games 60 \
    --search-topk "$SEARCH_TOPK" \
    --opponents random 2>/dev/null | tail -1)
  HC=$(python3 gate.py --net "$LATEST" --games 120 \
    --search-topk "$SEARCH_TOPK" \
    --opponents handcrafted 2>/dev/null | tail -1)
  echo "$(date +%H:%M) cumulative-games=$TOTAL | random: $RAND | handcrafted: $HC" >> "$PROGRESS"
  PCT=$(hc_pct "$HC")
  if awk "BEGIN{exit !($PCT > $BEST)}"; then
    cp "$LATEST" "$BEST_NET"
    echo "      promoted ckpt-search-${TOTAL} -> $BEST_NET (handcrafted ${PCT}% > ${BEST}%)" >> "$PROGRESS"
    BEST=$PCT
  else
    echo "      kept $BEST_NET (ckpt-search-${TOTAL} handcrafted ${PCT}% <= ${BEST}%)" >> "$PROGRESS"
  fi
  if awk "BEGIN{exit !($PCT < $STOP_BELOW)}"; then
    echo "=== search-era curve run STOPPED EARLY $(date): ckpt-search-${TOTAL} gated ${PCT}% < ${STOP_BELOW}% (collapse territory) ===" >> "$PROGRESS"
    exit 0
  fi
done
echo "=== search-era curve run complete $(date) ===" >> "$PROGRESS"
