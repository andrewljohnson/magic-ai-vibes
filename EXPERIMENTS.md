# Bot Experiment Notebook

This is the running lab notebook for bot-strength work. Record meaningful
experiments here even when they fail. A result is not accepted from one lucky
run: promising changes advance to the fixed multi-seed panel and paired,
seat-balanced benchmarks.

## Evaluation protocol

- Use `--benchmark` for direct policy comparisons. It pairs every unordered
  deck matchup, swaps policy assignments, and balances play/draw.
- Use seed `424242` as the development screen, then validate on seeds
  `101, 202, 303, 404, 505, 606, 707, 808`.
- Report aggregate wins, the Wilson 95% confidence interval, and results for
  each challenger deck.
- Learned Value must not use card IDs, card names, `handcrafted_card_value`,
  or Handcrafted as a teacher or rollout opponent. Its training opponents and
  search opponents are Learned Value bots.
- A candidate is accepted only if it beats Handcrafted in aggregate and in
  every deck slice across the validation panel. The final aggregate result
  must have a 95% lower confidence bound above 50%.

## Baselines

### Random-outcome value model, 200 training games

Learned Value used a 22-feature, 16-hidden-unit MLP trained from random game
outcomes, then selected the legal successor with the highest value.

- Versus Monte Carlo: 644-156 (80.5%).
- Versus Deep Monte Carlo: 575-225 (71.9%).
- Versus Handcrafted: 390-410 (48.8%).
- Decision: useful learner, but not stronger than Handcrafted.

### Two fitted self-play generations, 800 training games

Increasing the random-play corpus to 800 and adding two fitted self-play
generations removed model collapses seen at 200 and 400 games.

Mixed-field win rates across the fixed eight-seed panel always preserved:

`Random < Monte Carlo < Deep Monte Carlo < Learned Value < Handcrafted`

Learned Value ranged from 57.5% to 63.8%, but Handcrafted ranged from 64.6% to
74.2%.

- Decision: accept 800 as the stable training baseline, but continue tuning.

### Four-turn neural-guided mirror lookahead

Each legal action received its immediate neural value plus short Learned-vs-
Learned continuations. One rollout per action produced 415-385 (51.9%) versus
Handcrafted at seed 424242. Two produced 421-379 (52.6%). All four deck slices
were at least tied or ahead with two rollouts, but the aggregate 95% interval
still crossed 50%.

- Decision: keep two rollouts as the current search baseline.

## Rejected experiments

### Four fitted self-play generations

Doubling fitted self-play generations from two to four reduced the direct
Handcrafted benchmark to about 47%.

- Likely cause: repeatedly fitting the same policy distribution amplified
  value errors.
- Decision: reverted to two generations.

### Full-game learned continuation

Replacing the four-turn horizon with a complete Learned-vs-Learned game
reduced the Handcrafted screen to about 45%.

- Likely cause: model errors compound over long deterministic continuations;
  the shallow value prior was more useful than the noisy full trajectory.
- Decision: restored the four-turn horizon.

### Handcrafted curriculum and opponent-aware search

The learner sparred against Handcrafted during training and used the actual
opponent policy inside search. At seed 424242 it scored 411-389 (51.4%), but
Green and Red remained behind.

- Methodological problem: this indirectly transfers hand-written Handcrafted
  card policy into Learned Value.
- Decision: rejected and fully removed. Learned training and search are
  mirror-only.

### Larger generic feature network

Expanded the model from 22/16 to 42/32 using aggregate hand and battlefield
properties, without card IDs. With 800 training games it scored 193-207
(48.2%) in a 400-game screen. Raising training to 2,000 scored 97-103 (48.5%)
in a 200-game screen.

- Likely cause: the larger model needs a different optimizer or substantially
  more diverse self-play; merely increasing trajectories did not help.
- Decision: reverted to the stable 22/16 network.

### Continuation-only action ranking

Removed the immediate value prior and ranked actions only by two four-turn
continuations. It scored 192-208 (48.0%) in a 400-game screen.

- Likely cause: the immediate value estimate stabilizes sparse/noisy short
  continuations.
- Decision: restored equal weighting of the prior and continuations.

## Current experiment

### Enumerated neural minimax combat

Hypothesis: the old Learned combat policy missed important attack subsets and
averaged over random blocks even though it assumes a rational mirror
opponent. Enumerate small legal attack/block spaces, sample bounded candidates
for large spaces, and use the learned value network for max/min selection.
This uses rules-level legal move generation but no card-specific preferences.

Status: implementation in progress; development screen and validation panel
not yet run.

First development screen:

```sh
./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 101-99 (50.5%), 95% interval 43.6%-57.4%.
- Learned deck slices: Green 38%, Red 32%, Blue 62%, White 70%.
- Corresponding Handcrafted slices: Green 34%, Red 42%, Blue 58%, White 64%.
- Efficiency: 156.1 learned rollouts/game.
- Decision: not accepted. Green, Blue, and White led in this small screen,
  but Red lost by ten points and the aggregate interval is inconclusive.
- Next: diagnose the learned priority/action evaluator on the Red deck before
  spending a full validation panel on this candidate.

### Decision-state training traces

Hypothesis: training only on start-of-turn snapshots creates distribution
shift because the runtime policy evaluates post-spell and nonempty-stack
states. Record every priority state in random play and Learned self-play so
Bolt, Counterspell, and post-action positions receive terminal-outcome labels.
This changes data collection only and remains mirror-only/card-agnostic.

Status: implementation complete; development screen not yet run.

First development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 106-94 (53.0%).
- Learned/Handcrafted by challenger deck: Green 38%/26%, Red 40%/44%,
  Blue 66%/54%, White 68%/64%.
- Red spell rate rose from 7.9 to 9.1 per game and now matches Handcrafted's
  9.0; damage is nearly equal at 14.7/14.8.
- Runtime: 27.57 seconds.
- Decision: promising behavior change, not accepted because the small Red
  slice still trails. Advance to the 800-game development benchmark.

Full development benchmark:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 20 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 452-348 (56.5%), 95% interval 53.0%-59.9%.
- Learned/Handcrafted by challenger deck: Green 42.5%/23.0%,
  Red 41.0%/36.5%, Blue 67.0%/51.0%, White 75.5%/63.5%.
- Runtime: 90.98 seconds for training plus 800 paired games.
- Decision: passes the development Handcrafted gate for aggregate confidence
  and all four deck slices. Advance to the all-policy, multi-seed panel.

All-policy one-seed smoke panel:

```sh
/usr/bin/time -p ./build/alpha-sim --stability --stability-runs 1 \
  --games 3 --seed 424141 --rollouts 2 --deep-rollouts 8 \
  --train-games 800
```

- Random: 109-11; all deck slices pass.
- Monte Carlo: 101-19; all deck slices pass.
- Deep Monte Carlo: 86-34; all deck slices pass.
- Handcrafted: 70-50; Green, Red, and Blue pass, White ties 22-22.
- Runtime: 54.75 seconds with one shared model across all baselines.
- Decision: the harness works and the weaker policies clear comfortably. The
  Handcrafted sample is too small for confidence and White; proceed to pooled
  multi-seed validation after performance work.

### Sparse feature arithmetic

Hypothesis: most of the 191 state features are zero. Skip zero-valued
multiply/update operations in prediction and SGD without changing numerical
results for nonzero inputs.

Status: implementation complete; timing validation pending.

Timing validation combined with parallel paired-game execution:

- The identical one-seed all-policy smoke panel retained byte-for-byte result
  totals.
- Wall time fell from 54.75 seconds to 15.39 seconds (3.6x faster).
- Decision: keep. Fixed-seed outcomes and reduction order remain
  deterministic.

### Mixed-field per-deck lift gate

New user-facing requirement: in `make run`, Learned must provide a larger
win-rate lift over Random than Monte Carlo, Deep Monte Carlo, and Handcrafted
for each of Green, Red, Blue, and White. Direct paired benchmarks remain
required but are no longer sufficient by themselves.

Status: reproduce the seeded default tournament with the current candidate,
then add this requirement to stability validation.

First common-seed/parallel run:

- Wall time fell from 31.28 seconds to 12.07 seconds.
- Learned was best overall (69.2%) and had the largest lift for Green and
  Red, but Handcrafted led Blue and White.
- The common seed accidentally gave each 25-game policy matrix one random
  starting player, creating badly imbalanced deck play/draw counts (for
  example Green was 50/250).
- Decision: performance change is promising, but the result is invalid for
  strength. Explicitly balance play/draw within each policy matrix and rerun.

Balanced common-seed run:

- Wall time: 11.61 seconds.
- Every deck had a 150/150 play/draw split.
- Learned was best overall (68.8%) and had the largest lift for Green and
  Red. Handcrafted still led Learned on Blue (75.0%/68.3%) and White
  (96.7%/91.7%).
- Decision: keep balanced common seeds and parallel execution. The
  Blue/White mixed-field gate genuinely needs a training improvement.

### Cross-deck metagame training

Hypothesis: training samples same-deck mirrors 25% of the time, but the
round-robin metagame and displayed lift contain only six distinct-deck
pairings. Sample ordered distinct deck pairs uniformly for initial random
play and fitted Learned-vs-Learned self-play.

Status: implementation complete; seeded mixed-field run pending.

Seeded mixed-field result:

- Learned remained best overall (68.3%).
- Largest lift: Green and Red passed.
- White nearly passed: Learned 95.0% versus Handcrafted 96.7%.
- Blue failed: Learned 68.3% versus Handcrafted 75.0%.
- Decision: cross-deck sampling improves the metagame fit, particularly Red,
  but is not sufficient for the all-deck lift gate.

### Targeted nonempty-stack replay

Hypothesis: the network has stack card planes but receives outcome labels
almost exclusively at turn starts. Record a training snapshot only when the
stack is nonempty and the priority player has more than pass available. This
targets Counterspell decisions without repeating every pass/priority state,
which previously collapsed the model.

Status: implementation complete; seeded mixed-field run pending.

Development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 105-95 (52.5%).
- Learned/Handcrafted by challenger deck: Green 34%/20%, Red 38%/46%,
  Blue 66%/58%, White 72%/66%.
- Runtime: 37.55 seconds.
- Decision: rejected and reverted. Search-guided self-play cost ten seconds
  and did not close the Red gap.

### Four deployed rollouts per action

Hypothesis: the zone-aware value model and stack-faithful continuation may
benefit from more independent determinizations even though the older,
stack-skipping model plateaued. Raise only deployed search from two to four
rollouts; training remains unchanged.

Status: implementation in progress.

Development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 107-93 (53.5%).
- Learned/Handcrafted by challenger deck: Green 36%/26%, Red 40%/46%,
  Blue 64%/52%, White 74%/62%.
- Learned work doubled from 158 to 320 rollouts/game and runtime rose from
  27.6 to 48.8 seconds.
- Decision: rejected and reverted to two rollouts. Extra determinizations did
  not improve Red.

### Exploratory fitted self-play

Hypothesis: greedy fitted self-play narrows its state/action distribution and
  reinforces early pass/cast errors. During training only, choose a uniformly
  random legal priority action 10% of the time in generation one and 5% in
  generation two. Deployment remains deterministic apart from search
  determinization. This is analogous to self-play exploration, not an
  external teacher.

Status: implementation complete; development screen not yet run.

Development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 106-94 (53.0%), 95% interval 46.1%-59.8%.
- Learned/Handcrafted by challenger deck: Green 32%/24%, Red 40%/46%,
  Blue 70%/56%, White 70%/62%.
- Red diagnostics: Learned cast 7.9 spells and dealt 14.5 damage/game;
  Handcrafted cast 9.2 and dealt 15.7.
- Runtime: 27.64 seconds. More assertive attacks shortened games enough to
  make this faster as well as slightly stronger for Red.
- Decision: keep expected-value attack search, but strict strength still
  fails on Red.

### Search-guided fitted self-play

Hypothesis: training self-play uses a zero-rollout policy while deployment
uses two rollouts. Add 100 games (for the 800-game default) of one-rollout
Learned-vs-Learned play after the two cheap fitted generations, then train on
the combined replay. This is a self-generated policy-improvement curriculum.

Status: implementation complete; development screen not yet run.

Development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 107-93 (53.5%), 95% interval 46.6%-60.3%.
- Learned/Handcrafted by challenger deck: Green 42%/20%, Red 36%/48%,
  Blue 68%/52%, White 68%/66%.
- Runtime: 50.65 seconds, down from roughly 90 seconds for the first
  zone-aware architecture.
- Decision: keep the speed changes and learned sparse path, but do not accept
  strength. Red still fails.

### Expected-value neural attack search

Hypothesis: maximizing the worst sampled block makes the attacker excessively
conservative when value estimates are imperfect. Keep enumerated/sampled legal
attack and block candidates, but rank attacks by mean learned value across
blocks. Defender choice remains value-maximizing.

Status: implementation complete; development screen not yet run.

Development screen:

```sh
./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 104-96 (52.0%), 95% interval 45.1%-58.8%.
- Learned/Handcrafted by challenger deck: Green 36%/26%, Red 38%/42%,
  Blue 72%/54%, White 62%/70%.
- Runtime was roughly 90 seconds including build/training and the 200-game
  benchmark.
- Decision: keep the correct information boundary, but do not accept this
  architecture. Red and White fail and training is too slow.

### Sparse linear value path and cheaper training

Hypothesis: per-card zone counts are sparse and should not have to propagate
through a larger nonlinear layer to learn a basic value. Add a trainable
linear skip path from every feature to the value logit, return the hidden
layer to 16 units, and shuffle replay indices rather than copying full
191-value examples on each training call.

Status: implementation complete; development screen not yet run.

Development screen:

```sh
./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 86-114 (43.0%), 95% interval 36.3%-49.9%.
- Learned/Handcrafted by challenger deck: Green 36%/36%, Red 24%/52%,
  Blue 46%/68%, White 66%/72%.
- Decision: rejected and reverted to four turns. The longer horizon compounds
  mirror-policy/value errors even after priority continuation was fixed.

### Private/public card-zone planes

Hypothesis: the network must know which cards are in its own hand and all
public zones, rather than only aggregate hand/board counts. Add neutral
per-card count planes for own hand, both battlefields (including tapped,
summoning-sick, and damage state), both graveyards, and stack objects by
controller. Do not expose opponent hand identities. The policy still has no
scripted card values; it must learn them from random play and mirror
self-play.

Status: implementation complete; development screen not yet run.

First development screen:

```sh
./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 108-92 (54.0%), 95% interval 47.1%-60.8%.
- Learned/Handcrafted by challenger deck: Green 40%/30%, Red 38%/38%,
  Blue 68%/52%, White 70%/64%.
- Efficiency: 155.3 learned rollouts/game.
- Decision: promising, not accepted. Advance to the 800-game development
  benchmark because Red only tied and the confidence interval crosses 50%.

Full development benchmark:

```sh
./build/alpha-sim --benchmark --games 20 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 468-332 (58.5%), 95% interval 55.1%-61.9%: pass.
- Learned/Handcrafted by challenger deck: Green 49.5%/24.0%,
  Red 37.0%/31.5%, Blue 68.5%/45.0%, White 79.0%/65.5%.
- Efficiency: 152.1 learned rollouts/game.
- Decision: passes the development gate and every pooled deck slice. Advance
  to the fixed eight-seed direct benchmark panel.

Eight-seed Handcrafted validation:

```sh
./build/alpha-sim --stability --stability-runs 8 --games 5 --seed 0 \
  --rollouts 2 --deep-rollouts 8 --train-games 800
```

- Seeds: seven wins and one loss (seed 505 scored 96-104).
- Pooled: 865-735 (54.1%), 95% interval 51.6%-56.5%.
- Pooled Learned/Handcrafted wins by Learned deck: Green 158/110,
  Red 148/154, Blue 265/191, White 294/280.
- Decision: rejected under the strict gate. Aggregate strength is real, but
  Red and one training/game seed fail.
- Next: add a minimal set of generic own-hand aggregates so the value model
  can distinguish future resources without knowing card identities.

### Minimal generic hand features

Hypothesis: representing only hand size aliases materially different future
resources, particularly direct damage versus lands. Add own-hand land count,
total creature power, total printed effect damage, and total mana value. Keep
the compact network, mirror-only data, stack-faithful search, and neural
minimax combat unchanged.

Status: implementation in progress.

Development screen:

```sh
./build/alpha-sim --benchmark --games 10 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 226-174 (56.5%), 95% interval 51.6%-61.3%.
- Learned/Handcrafted by challenger deck: Green 47%/30%, Red 35%/36%,
  Blue 76%/40%, White 68%/68%.
- Decision: rejected and reverted. Aggregate strength improved, but Red lost
  narrowly and White tied; the strict per-deck gate did not pass.

### Eight-turn stack-faithful search

Hypothesis: after fixing priority continuation, the prior full-game failure
does not rule out a moderately longer horizon. Increase the mirror search
from four to eight turns so burn/creature clocks are more visible without
compounding value errors through an entire game.

Status: implementation complete; development screen not yet run.

Development screen:

```sh
./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 79-121 (39.5%), 95% interval 33.0%-46.4%.
- Learned lost every deck slice.
- Likely cause: highly correlated pass/priority snapshots overwhelmed the
  balanced turn-start examples and biased the value fit.
- Decision: rejected and reverted. A future retry must subsample decision
  states or use a separate replay weighting scheme.

### Stack-faithful mirror rollout

Hypothesis: Learned search is systematically wrong because it force-resolves
the candidate and all stack objects without giving its Learned mirror
opponent priority. After forcing the candidate action, resume the real
priority state with Learned bots until the window ends, then continue the
bounded rollout.

This specifically tests stack/search correctness and does not use the
Handcrafted policy.

Status: implementation complete; development screen not yet run.

## Next candidates

1. Test decision-state traces together with neural minimax combat on the
   development benchmark.
2. If it fails, test a small value ensemble trained only from independent
   random/self-play seeds to reduce initialization variance.
3. Add a clean policy head trained from the learner's own improved search
   choices, then compare it with the value-only action selector.
4. Add temporal-difference targets alongside terminal outcomes, with a held-
   out calibration set to detect value collapse early.
