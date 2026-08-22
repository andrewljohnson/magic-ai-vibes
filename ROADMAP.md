# Roadmap

What to try next, best bets first. Numbers and reasoning behind each are
in `RESULTS.md`.

The one-line strategy: **hyperparameters are exhausted (§2), so the next
gain has to be structural.** The missing structure is a value function.

---

## 1. Train a value function — DONE (head built), needs wiring into search

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

## 3. Keep scaling what already works — IN FLIGHT

The curve has never plateaued (§1). This is the boring, reliable lever.

- Two from-scratch runs are up on seed 33, a paired comparison isolating
  the decklist mode: `cls_scratch` (classified — **the deployable line**,
  since the server discloses no deck) and `od_scratch` (open — research,
  deployable only if #6 lands). Both target 200k games.
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

## 6. Upstream PR: expose the opponent's deck as a flag — IN FLIGHT

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

Status: a patch and PR text are being prepared against a scratch clone of
upstream. It cannot be pushed from here — the only GitHub credential on
this box is a deploy key scoped to *this* repo, which is deliberate.
A human pushes it.

## 7. Deploy

`hosted_bot.py` is the client that plays on lacker's server. Once a
champion clears a large-sample gate, ship it and confirm the hosted path
scores what the local gate says it should.

---

## 8. Make self-play bootstrap work

Pure self-play from random **fails outright**: `--selfplay-frac 1.0`
reached 10.9% on its first evaluation and then *declined* to ~5.7%, worse
than flailing. The mechanism is measured, not guessed: **43.3% of its
episodes hit the 600-decision cap**, versus 14.1% for the 50/50 run at the
same stage. Two clueless actors cannot close a game, so games never end.

And `compute_gae` opens with `if result is None: return []` — a capped
game is **discarded entirely**. So the learner sees terminal signal from
barely half its games, and only from the unrepresentative ones that
happened to finish. It never learns to close, so more games never end.

Ideas, best first. (1) is close to a prerequisite for the rest — shaping
and curricula are moot while the episodes are being thrown away.

**1. Bootstrap truncated episodes instead of discarding them.** Hitting a
decision limit is *truncation*, not *termination*; the standard treatment
(Pardo et al., "Time Limits in Reinforcement Learning") is to bootstrap
the critic's value at the cut rather than drop the episode or score it as
a loss. The GAE loop already computes `v_next = z if t == T-1 else V[t+1]`
— for a truncated episode it should use `V(s_T)` in place of `z`. Cheap,
principled, and it returns 43% of self-play experience *with the right
sign*. This also helps the 50/50 runs, which still discard ~6%, and those
are systematically the LONG games.

**2. End stalled games as draws instead of running them to the cap.**
Detect no progress (no life change, no board change over N turns) and
score 0.5. Converts a discarded episode into a scored one, and shortens
games, which raises throughput at the same time.

**3. Anneal the curriculum rather than fixing it at 50/50.** Start at ~100%
handcrafted, where games actually end, and decay toward self-play as the
actor learns to close. Gets the bootstrapping early and reduces long-run
exposure to the very opponent we score against. The current 0.5 was a
design choice, never tuned — 0.75 measured worse, 1.0 fails.

**4. Start-state curriculum: seed self-play from mid/late-game positions.**
Sample positions out of handcrafted (or finished self-play) games and
start self-play there, so terminal signal is dense from the first
decision. This is the classic answer to sparse terminal reward, and it
directly targets "neither side knows how to finish".

**5. Warm-start from a heuristic prior.** An actor that can already close
games makes self-play produce terminating games immediately. Note the
scar: imitating a *perfect-information* teacher got WORSE with data here.
A first_bot-style observation-only ordering does not have that defect,
so it is not the same experiment.

**6. Shape toward ending the game.** A small per-decision penalty, or
reward for reducing opponent life. Listed last because it invites reward
hacking (a bot that concedes fast scores well on a length penalty), and
because shaping does nothing while the episodes carrying it are dropped.

## Architectures already tried

So nobody re-proposes one of these as if it were new. Details and numbers
in `RESULTS.md`.

| approach | outcome |
|---|---|
| TD(λ) self-play value net, greedy afterstate eval (the SPZ lineage) | the 31.6%-era bot; superseded |
| Determinized search / ISMCTS over a value+policy blend | the old C++ 57.7% reference came from here, with decklists known |
| Winner-imitation policy head + DAgger | imitating a perfect-information teacher got WORSE with more data |
| **Asymmetric actor-critic, PPO + GAE** | **current, ~51%** |
| Belief features (unseen-pool counts) | kept; feature-level, not architectural |
| Width ladder 128 → 256 → 512 | **512 was ~5 points WORSE than 256** at equal games |
| AAC actor as an MCTS leaf evaluator | failed — the actor is not a value function (§5 of RESULTS) |

**Read the width result before proposing a bigger model.** 512 losing to
256 at equal games is evidence that *capacity is not the current
bottleneck*. Whatever is limiting us, "more parameters" has already been
tested once and made things worse. That should temper any proposal whose
pitch is a larger or more expressive network.

## Maybe — architectures worth considering

None of these are scheduled. Each lists why it might help, what it costs,
and **what evidence would justify starting it**, so they can be argued
about on merit rather than novelty.

### One deck, many opponents — the limitation nobody wrote down

We always pilot **Sligh**; only the opponent rotates over the other 14
archetypes. So we are training a Sligh specialist that has seen the whole
field, not a general player. That matches deployment (a bot registers one
deck), but it caps the ceiling and it means every result here is
"Sligh vs the field", not "good at this game".

Options: a net per deck (simple, N× the training), or a **deck-conditioned
net** (one net, our decklist as input — the belief block already encodes
it, so the plumbing largely exists). *Justified when:* we want a second
registered bot, or the Sligh line plateaus and we suspect the deck rather
than the method.

### Learned opponent/belief model, instead of best-overlap classification

Today the opponent's archetype is guessed by multiset overlap against
revealed cards: 76.6% accurate, **0% on turn one**. A small learned model
over the revealed sequence would give a *distribution* over archetypes and
over their hidden hand, rather than one hard guess that is simply wrong a
quarter of the time.

*Cheap, targets a measured weakness, and needs no new game generation —
the data is already in our trajectories.* Probably the best value on this
list. *Justified when:* someone has a spare day; honestly it might belong
in the numbered roadmap rather than here.

### Set / graph encoder over the battlefield

The battlefield is a *set* of permanents and the hand is a *set* of cards,
but we hand the net a flat 1081-vector of per-card counts. A
permutation-invariant encoder (DeepSets, or a GNN over
attacker/blocker/target relations) matches the actual structure and would
stop the net having to relearn "these 128 slots are all the same kind of
thing".

*Moderate cost, and it replaces hand-engineered features with learned
ones.* *Justified when:* feature ablations show the hand-engineered blocks
are the limit — e.g. if removing the castability/race block barely hurts,
the net is not using it well.

### The poker approach: CFR / Deep CFR / ReBeL

This is an imperfect-information game, and the poker line (counterfactual
regret minimization, and ReBeL's marriage of belief-state search with RL)
is the family with actual theoretical guarantees here — self-play PPO has
none, and can cycle.

**ReBeL is the interesting one for us specifically**, because it operates
on *belief states* and we already compute belief features. It would also
subsume ROADMAP #1 and #2 rather than competing with them.

*Cost is the problem.* These methods are sample-hungry and our binding
constraint is game generation at ~8 games/sec on CPU. Deep CFR in
particular assumes far more traversals than we can afford, and MTG's
information sets are vastly larger than poker's. *Justified when:* we have
a working value function (#1) and search (#2) and they plateau — ReBeL is
then the principled next step rather than a leap.

### Transformer over card/zone tokens

Encode the game as a sequence of typed tokens (cards, zones, stack, recent
actions) and let attention learn the representation. Handles variable-size
zones natively and could transfer across decks, which pairs with the
one-deck limitation above.

*Most expensive item here, and the width-512 result argues against it:*
our bottleneck is CPU game generation, not model capacity, and the last
time we added capacity it got worse. A transformer also costs far more per
inference, and inference sits inside the hot loop — every afterstate of
every decision. *Justified when:* generation is much faster (ROADMAP #5),
or evidence appears that the flat feature vector is genuinely losing
information.

### League / population play instead of pure self-play

Currently ~50% self-play against the *current* actor and ~50% against the
built-in bot. Self-play against only your latest self can cycle and
overfit. A frozen-snapshot league (AlphaStar-style, or PSRO) plays against
a population of past selves.

*Cheap to try — mostly scheduling, no new nets.* *Justified when:* we see
non-transitive results (a new actor beating the old one but doing worse on
the gate), which is the classic cycling signature. **Watch for it; we are
not currently checking.**

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
