# Brief: where penta-bot stands

Read `RESULTS.md` for the findings record, `ROADMAP.md` for the plan,
`AGENTS.md` for the traps. This is the short version and the open question.

## The state

* Search **is** a real teacher. Mirror match, alternating seats, same seeds,
  both seats our own net, greedy in-tree opponent — exactly what self-play
  trains on — 32 sims vs the raw 1-sim policy: **55.8% ± 3.5** over 207
  games.
* The policy **never absorbs it**. 180 rounds, ~8600 self-play games,
  promotion gated on that same mirror match: **0 promotions**, 2 reverts,
  best net byte-identical to the warm start. The mirror hovers at 50%.
* The policy never fits its targets either: `pol_ent` 1.38–1.45 against
  `tgt_ent` ~0.92, flat across all 180 rounds.
* Deployed and playing on the public server at 512 sims/move.

## What the last round of advice settled

| question | answer |
|---|---|
| Is search a weaker teacher than the student? | **No** — that measurement was against the handcrafted bot, the wrong opponent model. Withdrawn. |
| Is the ratchet discarding genuinely-improved nets? | **No** — promoting on the mirror instead changed nothing. |
| Is FPU why the 16/32-sim threshold exists? | **No** — +0.0 ± 3.7 at 16 sims; −4.2 ± 3.6 at c_puct 2.5. |
| Is truncation-as-loss poisoning the labels? | **Mostly no** — clock timeouts were already dropped; only decision-cap games score as losses. |

## The open question

The teacher is good and the student does not learn, so the loss is between
the search and the gradient — the targets, or the fit. Three measurements
split it, none needing a training run:

1. `KL(visit target ‖ policy prior)` per searched decision, and how often
   search's argmax differs from the prior's. Small KL + rare disagreement =
   the gradient is mostly noise.
2. Re-run the mirror h2h with `root_noise=0.25` on the searching seat. The
   targets carry 25% Dirichlet (α=1 over ~7 actions) plus add-k=1 (~18%
   more uniform on 32 visits). If noisy search ≈ prior, the policy is being
   trained toward a flattened version of itself.
3. Train on a fixed buffer for 20 epochs and re-gate the mirror. If
   overfitting the targets still cannot beat the warm start, the targets are
   the problem and not the optimiser.

## Constraints worth knowing before advising

* No handcrafted play knowledge in training. Evaluation helpers are fine.
* The gate is EXACTLY deterministic, so comparing two nets on it is paired
  and exact; ±SE only matters for generalising to other opponents.
* 50% is parity. The gate reports our own head-to-head win rate.
* Wall-clock guards have silently decided four separate measurements.
* Protocol-22 numbers are historical and are not worth restoring the p22
  vendor to reproduce.
