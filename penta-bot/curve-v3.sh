#!/bin/zsh
# NEW-ERA (v3) full-package run: fresh training from scratch with the
# 2026-08-09 lever package -- v2 features (castability + race math, 825
# inputs), rollout search topk 4 / 1 playout at train AND gate (the
# cost-adjusted sweep winner), 8-deck league (Sligh, White Weenie,
# The Deck, Counterburn, Goblins, Erhnamgeddon, Mono Black, Jeskai
# Aggro), per-turn TD set by $TD_PER_TURN (0 = hard outcome targets;
# the A/B winner is baked into the default below).
#
# $CHUNKS x 4000-game ring-persistent league chunks. Chunk 1 starts a
# FRESH net (old nets have the 675-input v1 schema and cannot transfer)
# with smoke-style exploration (eps 0.25 -> 0.10, no frozen snapshot
# yet); chunks 2+ continue at league epsilon (0.10 -> 0.08) with the
# previous chunk's snapshot frozen into the league. The replay ring
# (penta_ring.npz) is deleted at run start -- new feature width -- and
# threaded through every chunk.
#
# Gates: 60 vs random + 120 vs handcrafted on the fixed gate seeds
# (5000000+), search topk 4, rotating over the NEW 8-deck pairs -- gate
# numbers are NOT comparable to the 4-deck era, so the old 37.5%
# deliverable (ckpt-search-7000) is re-gated ONCE on this new config as
# the honest cross-era reference line before the run.
#
# Promotion: penta_net.npz on beating the running best, bar starts at 0
# (chunk 1 always promotes). Early stop: a handcrafted gate below 60% of
# the running best for TWO consecutive chunks.
#
# Seeds: chunk seed-base = 3000000 + cumulative (3000000..3044000),
# disjoint from the 1-ply era (1000000..1025000), the search era
# (1000000..1002999 smoke, 2003000..2013000 chunks), the A/B smokes
# (1000000..1002999, different features), and the gate seeds (5000000+).
set -e
cd "$(dirname "$0")"
PROGRESS=progress.txt
LATEST=penta_net_latest.npz
BEST_NET=penta_net.npz
CHUNKS=${CHUNKS:-"1 2 3 4 5 6 7 8 9 10 11"}
GAMES=${GAMES:-4000}
RING=${RING:-penta_ring.npz}
LR_WARMUP=${LR_WARMUP:-200}
WORKERS=${WORKERS:-8}
SEARCH_TOPK=${SEARCH_TOPK:-4}
TD_PER_TURN=${TD_PER_TURN:-0.0}
CROSS_ERA_NET=${CROSS_ERA_NET:-penta_net.ckpt-search-7000.npz}
BEST=0
BELOW=0

hc_pct() { echo "$1" | sed -E 's/.*-> *([0-9.]+)%.*/\1/' }

echo "=== v3 curve run $(date) | v2 features (825), search topk $SEARCH_TOPK train+gate, 8 decks, td-per-turn $TD_PER_TURN | league: handcrafted 0.15 + prev-chunk frozen 0.25, ring fresh + lr-warmup $LR_WARMUP | fresh net, ${GAMES}-game chunks, promotion bar 0 ===" >> "$PROGRESS"

# Honest cross-era reference: the old-era deliverable once, on THIS gate
# config (8 decks, topk 4). Old net = v1 features; gate.py dispatches by
# input size. Note line only -- not a cumulative-games line, so the
# dashboard does not chart it, and it never sets the promotion bar.
REF=$(python3 gate.py --net "$CROSS_ERA_NET" --games 120 \
  --search-topk "$SEARCH_TOPK" --opponents handcrafted 2>/dev/null | tail -1)
echo "      cross-era reference: $CROSS_ERA_NET (old v1 features) on this gate config: $REF" >> "$PROGRESS"

rm -f "$RING"
TOTAL=0
for CHUNK in ${=CHUNKS}; do
  EXTRA=()
  if [[ "$TOTAL" -eq 0 ]]; then
    EXTRA+=(--epsilon-start 0.25 --epsilon-final 0.10)
  else
    EXTRA+=(--init "$LATEST" --epsilon-start 0.10 --epsilon-final 0.08 \
            --league-frozen "penta_net.ckpt-v3-${TOTAL}.npz")
  fi
  if awk "BEGIN{exit !($TD_PER_TURN > 0)}"; then
    EXTRA+=(--td-lambda-per-turn "$TD_PER_TURN")
  fi
  python3 trainer.py --games "$GAMES" --out "$LATEST" \
    --workers "$WORKERS" \
    --search-topk "$SEARCH_TOPK" \
    --league-handcrafted 0.15 \
    --ring "$RING" --lr-warmup "$LR_WARMUP" \
    --seed-base $((3000000 + TOTAL)) \
    "${EXTRA[@]}" \
    > "chunk-${CHUNK}.log" 2>&1
  TOTAL=$((TOTAL + GAMES))
  cp "$LATEST" "penta_net.ckpt-v3-${TOTAL}.npz"
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
    echo "      promoted ckpt-v3-${TOTAL} -> $BEST_NET (handcrafted ${PCT}% > ${BEST}%)" >> "$PROGRESS"
    BEST=$PCT
  else
    echo "      kept $BEST_NET (ckpt-v3-${TOTAL} handcrafted ${PCT}% <= ${BEST}%)" >> "$PROGRESS"
  fi
  if awk "BEGIN{exit !($PCT < 0.6 * $BEST)}"; then
    BELOW=$((BELOW + 1))
  else
    BELOW=0
  fi
  if [[ "$BELOW" -ge 2 ]]; then
    echo "=== v3 curve run STOPPED EARLY $(date): two consecutive gates below 60% of the running best ${BEST}% ===" >> "$PROGRESS"
    exit 0
  fi
done
echo "=== v3 curve run complete $(date) ===" >> "$PROGRESS"
