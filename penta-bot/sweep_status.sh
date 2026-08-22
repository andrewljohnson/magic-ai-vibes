#!/bin/bash
# Tabulate every AAC sweep arm: games, latest gate, best gate, entropy,
# throughput, and the knob that distinguishes the arm.
#
# ENTROPY is the leading indicator: a run whose entropy decays toward
# ~0.15 while its gates flatten is converging prematurely -- that is what
# stalled the first h512 run, and raising --entropy-beta reversed it.
# Gates are 400 games, so the standard error is ~2.5 points; do not read
# a single gate as a trend.
cd "$(dirname "$0")"
printf "%-14s %7s %7s %7s %8s %9s  %s\n" RUN GAMES LAST BEST ENTROPY G/S KNOB
for f in belief_native belief512e sw_ent sw_lam sw_self sw_lr sw_ep8 sw_batch sw_temp sw_ctrl; do
  log="$f.log"; [ -f "$log" ] || continue
  line=$(grep "GATE @" "$log" | tail -1)
  [ -z "$line" ] && continue
  games=$(sed -E 's/.*GATE @([0-9]+) games.*/\1/' <<<"$line")
  # two formats: "@0 games: ... = 42.5%" and "@N games: 45.5% | ..."
  last=$(grep -oE '=[ ]*[0-9.]+%|games: [0-9.]+%' <<<"$line" | grep -oE '[0-9.]+' | tail -1)
  ent=$(grep -oE 'entropy=[0-9.]+' <<<"$line" | cut -d= -f2)
  gps=$(grep -oE '\[[0-9.]+ g/s\]' <<<"$line" | grep -oE '[0-9.]+')
  best=$(grep -oE 'new best [0-9.]+' "$log" | awk '{print $3}' | sort -gr | head -1)
  s=$(grep -m1 "PAR-AAC start" "$log")
  knob=$(grep -oE 'hidden=[0-9]+|a_lr=[0-9.e-]+|ppo_epochs=[0-9]+|ent=[0-9.]+|gae_lam=[0-9.]+|round_ep=[0-9]+' <<<"$s" | tr '\n' ' ')
  printf "%-14s %7s %6s%% %6s%% %8s %9s  %s\n" "$f" "$games" "${last:--}" "${best:--}" "${ent:--}" "${gps:--}" "$knob"
done
