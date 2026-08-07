#!/bin/zsh
# Learning-curve run with league play and gate-guarded promotion.
#
# Starts from the smoke net in penta_net_latest.npz (3000 games) and runs
# $CHUNKS x 2000 games. Per chunk: train (warm start from the
# LATEST weights), snapshot to penta_net.ckpt-<cumulative>.npz (history is
# never overwritten), gate 60 vs random + 120 vs handcrafted on fixed
# seeds, append the progress line, and promote to penta_net.npz (the
# best/deliverable artifact) only when the handcrafted gate beats the best
# so far. League: 15% of games vs the built-in handcrafted bot, ~25% vs
# the previous chunk's frozen snapshot; epsilon floor 0.08. Each chunk
# gets a fresh, non-overlapping seed range so no seed is replayed.
set -e
cd "$(dirname "$0")"
PROGRESS=progress.txt
LATEST=penta_net_latest.npz
BEST_NET=penta_net.npz
CHUNKS=${CHUNKS:-"1 2 3"}

hc_pct() { echo "$1" | sed -E 's/.*-> *([0-9.]+)%.*/\1/' }

TOTAL=3000
echo "=== curve run $(date) | league: handcrafted 0.15 + prev-chunk frozen 0.25, eps floor 0.08, td-lambda 1.0 | from fresh v2 smoke net ($TOTAL games) ===" >> "$PROGRESS"

# Baseline: snapshot the smoke net (it is chunk 1's frozen league
# opponent) and gate it to set the promotion bar.
cp "$LATEST" "penta_net.ckpt-${TOTAL}.npz"
RAND=$(python3 gate.py --net "$LATEST" --games 60 \
  --opponents random 2>/dev/null | tail -1)
HC=$(python3 gate.py --net "$LATEST" --games 120 \
  --opponents handcrafted 2>/dev/null | tail -1)
echo "$(date +%H:%M) cumulative-games=$TOTAL | random: $RAND | handcrafted: $HC" >> "$PROGRESS"
BEST=$(hc_pct "$HC")
cp "$LATEST" "$BEST_NET"
echo "      baseline: smoke net promoted as initial $BEST_NET (handcrafted ${BEST}%)" >> "$PROGRESS"

for CHUNK in ${=CHUNKS}; do
  FROZEN="penta_net.ckpt-${TOTAL}.npz"
  python3 trainer.py --games 2000 --init "$LATEST" --out "$LATEST" \
    --epsilon-start 0.10 --epsilon-final 0.08 \
    --league-handcrafted 0.15 --league-frozen "$FROZEN" \
    --seed-base $((1000000 + TOTAL)) \
    > "chunk-${CHUNK}.log" 2>&1
  TOTAL=$((TOTAL + 2000))
  cp "$LATEST" "penta_net.ckpt-${TOTAL}.npz"
  RAND=$(python3 gate.py --net "$LATEST" --games 60 \
    --opponents random 2>/dev/null | tail -1)
  HC=$(python3 gate.py --net "$LATEST" --games 120 \
    --opponents handcrafted 2>/dev/null | tail -1)
  echo "$(date +%H:%M) cumulative-games=$TOTAL | random: $RAND | handcrafted: $HC" >> "$PROGRESS"
  PCT=$(hc_pct "$HC")
  if awk "BEGIN{exit !($PCT > $BEST)}"; then
    cp "$LATEST" "$BEST_NET"
    echo "      promoted ckpt-${TOTAL} -> $BEST_NET (handcrafted ${PCT}% > ${BEST}%)" >> "$PROGRESS"
    BEST=$PCT
  else
    echo "      kept $BEST_NET (ckpt-${TOTAL} handcrafted ${PCT}% <= ${BEST}%)" >> "$PROGRESS"
  fi
done
echo "=== curve run complete $(date) ===" >> "$PROGRESS"
