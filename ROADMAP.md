# Roadmap

What to try next, best bets first. Numbers and reasoning behind each are
in `RESULTS.md`.

The one-line strategy: **hyperparameters are exhausted (§2), so the next
gain has to be structural.** The missing structure is a value function.

---

## 1. Train a value function — the blocking gap

```
policy  π(a|s)   ✓  the actor, ~50%
value   V(s)     ✗  MISSING
```

Everything past 1-ply is blocked on this. The actor cannot substitute:
it is trained only on a softmax over one decision's siblings, so its
score is a *relative ranking* with no absolute scale, and it carries
~0.003 nats about who actually wins (§5).

Two candidates, and they are not exclusive:

- **An observation-only value head** — single-seat redacted observation → P(win),
  trained on the returns the trainer already computes. Nearly free: one
  more small MLP per round, no new data. This is the AlphaZero-shaped
  answer and should be built first.
- **The privileged critic** — already trained on real value targets, and
  now checkpointed (`<prefix>_critic.npz`; every run before 2026-08-22
  discarded it). It is privileged (both seats' observations) so it can
  never deploy as an actor — but inside a *determinized* world the
  hypothesis fixes both hands by construction, so using it there stays
  allowed to see. That makes it the natural leaf evaluator for search.

**Done when:** a value net's win-probability prediction beats base rate by
a real margin (the actor manages 0.003 nats; anything worth using should
be far above that).

## 2. Search, again — but only after #1

Do not repeat the experiment in §5 with a better prompt; repeat it with a
better evaluator. Before trusting any number from it:

- **Fix the deck oracle.** `mcts_runner.rs` reads `decks.get(d1)` /
  `decks.get(d2)` directly. Now that open decklists are the architecture
  this is no longer a disclosure problem, but it must match whatever the AAC path
  does or the comparison is meaningless.
- **Fix the cost.** ~5000x per game is not fundamental. `determinize()`
  calls `from_observation_json`, rebuilding an entire game from JSON per
  move — the same inefficiency already deleted from the generation path.
  1-ply already evaluates ~17 afterstates per decision, so 16 MCTS
  iterations is *comparable* engine work; the gap is implementation.
- **Then gate it** against 1-ply on identical games and seeds.

## 3. Keep scaling what already works

The curve has never plateaued (§1). This is the boring, reliable lever.

- The from-scratch open-decklist run (`od_scratch`) to 200k+ games.
- Then more games again. Every arm improved overnight purely from volume.
- **Fill the box with concurrent runs, not one wide one** — a single
  process saturates near 3 cores (§4c).

## 4. Automate the entropy floor

Entropy collapse causes plateaus and raising `--entropy-beta` reverses it
(§3) — but we have now diagnosed it twice and fixed it by hand-restarting
runs both times. That is not a strategy. Add an entropy floor or adaptive
target to `ppo_update_fast`: if measured entropy drops below a target,
raise the coefficient automatically.

## 5. Recover the missing 3x throughput

Unresolved (§4c): one process ~8.5 g/s, four processes ~26 g/s aggregate.
Four hypotheses tested and rejected. Next step is to **profile with
`perf`** rather than guess a fifth time. Lower priority than it looks,
because the concurrent-runs workaround already recovers the throughput —
this only buys tidiness.

## 6. Upstream PR: expose the opponent's deck as a flag

Worth proposing to [penta](https://github.com/lacker/penta). Today the bot
protocol discloses no deck metadata — registration and the heartbeat both
report *your own* deck, and the observation has no archetype field — so a
bot must classify the opponent from revealed cards (76.6% accurate, 0% on
turn one).

That makes open decklists a research setting we cannot deploy: an actor
trained with the true decklist would be trained on information the server
never provides, and the mismatch is measurable (~2.5 points when the input
distribution is swapped at test time).

A per-room or per-registration flag — "both sides' archetypes are
disclosed" — would make it a real format rather than a lab condition.
There is precedent in paper Magic: open decklists are standard at
competitive tables, and the pool here is fifteen known archetypes anyway,
so it leaks far less than it sounds. It would also make our numbers
directly comparable to the older determinized-search results, which were
all measured with decklists known.

Shape of the proposal: an optional field on the bot registry entry and the
hosted room, surfaced in the observation as the opponent's archetype name
when both sides opted in. Default off, so nothing changes for existing bots.

## 7. Deploy

`hosted_bot.py` is the client that plays on lacker's server. Once a
champion clears a large-sample gate, ship it and confirm the hosted path
scores what the local gate says it should.

---

## Open questions worth an experiment

- **Does the belief block actually earn its 256 dimensions?** It was
  credited with 44% → 45%, which is inside the noise of a 200-game gate.
  Ablate it at 100k games with today's error bars.
- **Why is width 512 worse than 256** (−5 points at equal games)? If it is
  an optimization problem rather than a capacity one, the fix might be lr
  or warmup rather than abandoning width.
- **~5% of episodes hit the 600-decision cap** and are dropped by
  `compute_gae`. Long games are therefore systematically absent from
  training. Does raising the cap change anything?
- **Is the tempo shaping still helping?** It was tuned on a much weaker
  actor and has never been re-ablated.

## Not doing

- No C++ engine. No browser arena. No human-vs-bot play server. This repo
  trains a bot for lacker's server; that is all.
