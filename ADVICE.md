# Why search is worth nothing on protocol 29 — recommendation

Read the whole search path (`spz-core/src/mcts.rs`, `az.rs`, `mcts_runner.rs`,
`det_runner.rs`, `pybridge.rs`, `az_train.py`), the vendor patches, and the
commit history. This is what I'd do next, in order.

## The framing is right, the suspect list is wrong-shaped

Your own data already argues **against** the value head being the bottleneck:
after retraining, the p29 value head separates siblings *better* than the p22
one (0.029/26% vs 0.022/29%), and search still adds nothing. A leaf evaluator
that ranks siblings fine but a search that gains nothing from it means the
**tree between root and leaf** is broken, not the leaf. That tree is:
determinize → descend our nodes → in-tree opponent model → apply → leaf.
Every step of that has a silent failure mode, and none of them is counted.

Two facts from git that constrain the story:

1. **The loss is at the port, not the speed patches.** `57012e7` (before
   `f900af0` / `b9d2a7e` rewrote the ply loop) already measured deploy_v1 on
   p29 at 32 sims = 42.0% vs 54.3% on p22. So enumerate-once /
   `apply_enumerated` / node cache are not the cause. (Re-measuring p22 with
   *today's* search code is still worth one gate — see step 0.)
2. `apply_enumerated` is sound: protocol expansion only rewrites
   `ChooseDecision`, which still goes through full `is_legal_action`. I
   checked; don't re-derive it.

## Silent failure modes in `mcts.rs` — instrument these FIRST

None of these is logged. Any one of them, if it fires on p29 and not p22,
produces exactly "search = prior":

| where | what happens on failure | line |
|---|---|---|
| `determinize()` | `from_observation_json(...).ok()` — reconstruction error is discarded, the **iteration is skipped**. 32 sims with 50% rejects is 16 sims; 90% rejects is 3. | `mcts.rs:710` |
| `run()` loop | `if let Some(world) = determinize(..)` — silently does fewer iterations than `cfg.iters` | `mcts.rs:817-823` |
| opponent ply | `opp.choose_action` returns None or `apply_enumerated` fails → **descent stops and leaf-evals the pre-opponent state**. Search then never sees the opponent's turn: no blocks, no burn, no combat consequence. That is where +15 came from on p22. | `mcts.rs:508-514` |
| our ply | `let _ = world.apply_enumerated(...)` — error dropped, world does not advance, the node is expanded and the **unchanged state is scored as the afterstate** | `mcts.rs:576, 613` |
| forced ply / avail==1 | same `let _ =` | `mcts.rs:463, 521` |

Add per-search counters (thread-local, like the `prof` module already there):
`det_ok`, `det_err{by error string}`, `opp_apply_fail`, `our_apply_fail`,
`max_depth_hits`, `terminal_leaves`, `plies_per_iter`, `our_nodes_per_iter`,
and **fraction of iterations that reach an opponent decision at all**. Print
them per gate. This is ~1 hour and turns the rest of this list from guesses
into a lookup.

The reconstruction's own rejection reasons are in
`vendor/penta/src/game/state_checkpoint.rs:631-984` and
`bot_game.rs:100-125` ("rebuilt a different legal-action list", "rebuilt a
different public observation field: X"). Log the string — the field name
tells you what p29 added that our hidden-state sampler doesn't satisfy.

## Three experiments that split the hypotheses (each is one 120–300 game gate)

Run them on deploy_p29 (the 43.3% net), gate at 1 sim vs 16 sims, same
seeds. The gate is deterministic, so compute the **paired** difference from
the per-game `per` vector `az_gate` already returns — the SE of a paired
difference on correlated games is much smaller than the ±4.5 you've been
quoting, and it is the number that answers "is search worth anything".

**A. Oracle determinization.** In `Ismcts::search` you hold `real: &BotGame`.
Add a debug env (`AZ_ORACLE_WORLD=1`) that makes `determinize()` return
`real.core_game().clone()` instead of sampling. This is the upper bound on
determinization quality — the true hidden state.
- If oracle search is worth +10 or more → determinization / reconstruction is
  the problem (your leading candidate), go to C.
- If oracle search is still ~0 → determinization is NOT the problem; it's the
  opponent model or the tree (B), or the counters above already told you.

**B. Rollout leaf, with the guard lifted.** `AZ_GATE_PLAYOUT=1` was tried
and returned 0W/60L because the wall-clock guard capped every game
(`5c1cae6`). Re-run with `AZ_GATE_SECS=100000`, 60 games, 8 sims. It isolates
the value head: if a rollout leaf makes search worth something, the value
head is it; if not, it isn't. This diagnostic was designed for exactly this
question and has never actually produced a number.

**C. True-deck determinization.** `az_gate(..., classify=False)` is already a
knob (`mcts_runner.rs:431`). It hands the sampler the opponent's real
decklist instead of the classification (which is 0% accurate on turn one and
falls back to *our own deck* via `unwrap_or(my_deck)`, `mcts_runner.rs:433`).
It's a cheaper, weaker version of A; run it if A says determinization.

Also note what the sampler ignores (`det_runner.rs:28-43`): `last_seen_hand`
(p29 reveals opponent-hand cards after some effects) and cards on the stack.
If A wins and C doesn't, the gap is these plus library composition.

## Step 0: confirm the p22 baseline on today's code

README says the p22 numbers cannot be re-measured. They can:
`git checkout 62948bf~1 -- penta-bot/vendor/penta && cargo build --release`.
Do it once, with the counters from above compiled in, and gate deploy_v1 at
1/32 sims. You get two things: (a) proof that today's search code still
produces +15 on p22 (closing the speed-patch bisect for good), and (b) the
**baseline counter distribution** — plies/iter, fraction of iterations
reaching the opponent's turn, det_err rate — to diff against p29. The diff
in those counters is very likely the answer by itself.

## Things that are wrong regardless of what the counters say

- **Self-play and the gate run different searches.** `az_stream_episodes`
  uses `opponent: Greedy` + `use_dominance: false` (`pybridge.rs:1130-1138`);
  `az_gate` uses `opponent: Handcrafted` (`pybridge.rs:591`). The teacher the
  loop distills is not the search the gate measures. Add `AZ_GATE_OPP=greedy`
  to `az_gate` and measure both; if search is worth +X with one model and 0
  with the other on p29, that's your answer too (the handcrafted bot changed
  substantially across the 173 vendor commits — X-spell scoring, Disk sweep
  scoring, "as many options as beneficial").
- **Q=0 for unvisited actions on a [0,1] value scale** (`mcts.rs:667`). It's
  the worst possible score, so with a trained (peaked) prior, an action with
  P=0.02 needs `1.5·0.02·√N` to beat a visited sibling's Q≈0.45 — it never
  will at 16 sims. That makes 16-sim search a 1-ply argmax whenever the prior
  is confident. Use first-play-urgency = parent's mean Q (or the value of the
  node itself). This is not the p22/p29 differentiator but it caps how much
  search can ever be worth at your budget. Log root visit entropy in the
  *gate* (no noise there), not just in self-play.
- **Make failures errors, not fallbacks.** You already learned this with
  `search_obs` (action 0 at 2.1%). `let _ = apply_enumerated` and
  `determinize().ok()` are the same shape. At minimum count them; better,
  panic in debug builds.

## What NOT to do until the above is done

- Don't touch the representation (deep-sets) yet. It invalidates every
  checkpoint and it won't fix a tree that never reaches the opponent's turn.
- Don't run more value-only retraining. 300 rounds plateaued at 43.3% and
  the sibling-spread numbers say the value head is already adequate.
- Don't quote a search-value number from fewer than 300 paired games.

## Order of operations, concretely

1. Counters in `mcts.rs` (1h). Gate deploy_p29 at 16 sims on p29, read them.
2. Oracle-world gate (A). 1 line in `determinize`, one gate.
3. If A ≫ 0: diff `det_err` strings and `seen_defs` coverage vs p29's
   observation; fix the sampler; re-gate.
   If A ≈ 0: rollout-leaf gate (B) and `AZ_GATE_OPP=greedy`; whichever moves
   the number is the component to fix.
4. Restore the p22 vendor once, with counters, for the baseline diff.
5. Only then resume training — with the gate and self-play running the same
   search config, and FPU fixed.
