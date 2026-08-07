#!/bin/zsh
# Learning-curve run: chunks of self-play with a gate after each, so
# "is it getting better?" has an answer at every step. Continues from
# the smoke-trained weights.
set -e
cd "$(dirname "$0")"
PROGRESS=progress.txt
echo "=== curve run $(date) | starting from smoke net (3000 games) ===" >> "$PROGRESS"
TOTAL=3000
for CHUNK in 1 2 3 4 5 6; do
  python3 trainer.py --games 2000 --init penta_net.npz \
    --out penta_net.npz --epsilon-start 0.10 --epsilon-final 0.03 \
    > "chunk-${CHUNK}.log" 2>&1
  TOTAL=$((TOTAL + 2000))
  RAND=$(python3 gate.py --net penta_net.npz --games 60 \
    --opponents random 2>/dev/null | tail -1)
  HC=$(python3 gate.py --net penta_net.npz --games 120 \
    --opponents handcrafted 2>/dev/null | tail -1)
  echo "$(date +%H:%M) cumulative-games=$TOTAL | random: $RAND | handcrafted: $HC" >> "$PROGRESS"
done
echo "=== curve run complete $(date) ===" >> "$PROGRESS"
