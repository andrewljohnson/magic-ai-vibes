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

## Search and representation experiments

### Enumerated neural minimax combat

Hypothesis: the old Learned combat policy missed important attack subsets and
averaged over random blocks even though it assumes a rational mirror
opponent. Enumerate small legal attack/block spaces, sample bounded candidates
for large spaces, and use the learned value network for max/min selection.
This uses rules-level legal move generation but no card-specific preferences.

Status: superseded by stack-faithful expected-value combat below.

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

Seeded mixed-field result at discount 0.99:

- Learned was best overall at 73.8%.
- Green: Learned 55.0%, next-best Handcrafted 28.3%.
- Red: Learned 65.0%, next-best Handcrafted 56.7%.
- Blue: Learned 81.7%, next-best Handcrafted 71.7%.
- White: Learned 93.3%, Handcrafted 95.0% (one-game miss).
- Decision: keep discounted targets. Test a stronger 0.98 time preference
  specifically to separate efficient dominant-deck wins.

Status: discount 0.98 development run pending.

Seeded mixed-field result at discount 0.98:

- White became strictly best: Learned 98.3% vs Handcrafted 96.7%.
- Green and Red remained best.
- Blue regressed: Learned 65.0% vs Handcrafted 71.7%.
- Decision: the stronger time target overcorrects. Test the midpoint 0.985;
  do not continue scalar tuning if it cannot clear both Blue and White.

Seeded mixed-field result at discount 0.985:

- Learned was best overall at 71.2%.
- Green, Red, and Blue were strictly best.
- White tied Handcrafted at 93.3%.
- Decision: retain 0.985 as the best balance so far. Direct all-policy and
  multi-seed validation still apply; White is co-best rather than strictly
  best in this 60-game slice.

## Deck evolution

### Balanced genetic-search MVP

Implemented `--evolve-deck` with:

- 40-card candidates using only the union of the 13 current metagame cards;
- the four current decks as initial elites;
- mutation/elitism across configurable generations and population;
- common evaluation seeds for candidate fairness;
- both candidate seats and both starting players against every metagame deck;
- parallel candidate evaluation and per-generation/final CLI reporting.

Smoke command:

```sh
./build/alpha-sim --evolve-deck --generations 3 --population 8 \
  --games 2 --seed 12345
```

The best fitness improved 75.0% -> 81.2% -> 87.5% and produced a legal
40-card list. Runtime was 0.14 seconds. This is an optimization-set estimate,
not a holdout claim.

## Full all-policy validation at the 0.985 candidate

Eight seeds, five paired repetitions, 1,600 games per baseline:

- Random: 1473-127; every seed and deck passes.
- Monte Carlo: 1289-311; every seed and deck passes.
- Deep Monte Carlo: 1164-436; every seed and deck passes.
- Handcrafted: 857-743 (53.6%, CI 51.1%-56.0%); Green/Blue pass,
  White is 293-292, Red fails 138-163, and seed 101 loses.
- Wall time after parallelization: 204.97 seconds.
- Decision: overall strength is confirmed, but strict stability is rejected.

### Two-member learned value ensemble

Hypothesis: the remaining seed/Red/White variance comes from one small
network's initialization and fitted self-play errors. Train two independent
networks on the same allowed random/mirror data and average their predictions
during self-play and deployment.

Status: implementation complete; targeted seed 101 and Red validation
pending.

Seed 101 screen:

- Aggregate improved from the single-model panel's 48.0% to 51.5%.
- Green and Blue passed; Red (36%/40%) and White (66%/70%) failed.
- Runtime: 51.06 seconds for training and 200 paired games.
- Decision: ensemble averaging helps the bad seed but does not pass the
  strict per-deck gate.

### Own-library composition plane

Hypothesis: a player knows its deck composition and remaining library
multiset, but the network receives only library size. Add per-card counts for
the learner's own library (never order and never opponent hidden contents) so
one shared network can condition plans on deck identity and remaining
resources, including evolved hybrid decks.

Status: implementation complete; seed 101 screen pending.

Seeded mixed-field result:

- Learned was best overall at 70.4%.
- Green: Learned 50.0%, next-best Handcrafted 30.0%: pass.
- Red: Learned 63.3%, next-best Handcrafted 60.0%: pass.
- Blue: Learned 75.0%, next-best Handcrafted 73.3%: pass.
- White: Learned 93.3%, Handcrafted 95.0%: fail by one game.
- Runtime: 26.23 seconds. Extra replay states increased training cost.
- Decision: keep targeted stack replay; it fixed the Blue lift gap. White is
  the sole remaining mixed-field miss.

### Targeted activated-ability replay

Hypothesis: as with Counterspell, Millstone activation states are absent from
turn-start replay. Record states when any activated ability is a legal
non-pass choice, so activation timing is learned from terminal outcomes.

Status: implementation complete; seeded mixed-field run pending.

Seeded mixed-field results:

- At 800 training games, Learned tied the best policy on White and Blue and
  led on Green and Red.
- Raising training to 1,200 made Blue strictly best but reduced White to
  91.7% versus Handcrafted's 96.7%.
- Decision: retain the 800-game default; more binary-outcome data does not
  solve dominant-deck action quality.

### Discounted terminal returns

Hypothesis: White wins most cross-deck self-play, so binary `1.0` labels give
no gradient between efficient and inefficient wins. Train toward
`0.5 +/- 0.5 * 0.99^turns`, preserving win/loss ordering while rewarding
faster wins and longer resistance. This adds no card knowledge.

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

## Training-stability experiments

### Stratified deck pairs and capped replay

The own-library plane seed-101 screen scored 102-98 overall. Green, Blue, and
White passed, while Red fell to 32% versus Handcrafted's 42%. No further
feature expansion is planned.

Hypothesis: random pair selection and variable-length traces overweight long
control games plus targeted stack/activation states. Cycle through all 12
ordered distinct-deck pairs in shuffled blocks and use at most 24 evenly
spaced states from each game. Every sampled state still supplies both player
perspectives.

Seed-101 result:

- Aggregate: 97-103 (48.5%).
- Only Green passed; Red, Blue, and White failed.
- Decision: rejected and reverted. Equalizing raw matchup counts and trace
  lengths removed useful control-decision replay without fixing Red.

### Stronger self-play exploration

Hypothesis: bad-seed Red models consistently cast too few spells. Increase
training-only legal-action exploration from 10%/5% to 20%/10% across the two
fitted generations. Deployment remains unchanged.

Seed-101 result:

- Aggregate: 102-98 (51.0%).
- Red remained poor at 30% versus 42%; White also failed.
- Decision: rejected and reverted to 10%/5%. More random actions do not
  improve the value fit.

### Dual-objective value ensemble

Hypothesis: train one member on `0.99^turns` (strong Blue) and the other on
`0.98^turns` (strong Red/White tempo), then average predictions.

Seed-101 result:

- Aggregate: 99-101 (49.5%).
- Blue was strong, but Red collapsed to 14% and White failed.
- Decision: rejected and reverted. Separately optimized scalar objectives
  create incompatible action rankings.

### Larger ensemble training corpus

Hypothesis: 800 games may be insufficient for two zone-aware members on the
bad seed. Test 1,600 initial games with proportional fitted self-play before
changing the default.

Seed-101 result:

- Aggregate: 101-99 (50.5%).
- Red remained behind 32%/36%; White regressed to 62%/76%.
- Runtime: 74.30 seconds.
- Decision: rejected. Keep the 800-game default. More outcome data does not
  correct the remaining action-policy weakness.

## Current honest status

The best clean configuration is zone-aware, mirror-only, uses a two-member
`0.985` value ensemble, targeted stack/activation replay, 10%/5% training
exploration, stack-faithful four-turn search, and expected-value combat.

- It is decisively best overall in the seeded mixed field.
- It beats Random, Monte Carlo, and Deep Monte Carlo for every deck across
  all eight validation seeds.
- It beats Handcrafted overall with a 95% lower bound above 50%.
- The strict Handcrafted gate is not fully solved: pooled Red and one training
  seed fail; pooled White is effectively tied.

Next strength work should add a learned action-policy head or actor-critic
objective. More rollout depth, scalar discount tuning, replay volume,
exploration, generic feature expansion, and dual-discount ensembling have all
been tested and rejected as solutions to this gap.

### Parallel ensemble fitting

The two ensemble members train on a read-only shared replay with disjoint
weights. Fit them concurrently at initial training and after each self-play
generation. Expected outcome totals are unchanged; only wall time should
change.

Timing check:

- Seeded output totals were identical before and after parallel fitting.
- Wall time fell from 41.34 seconds to 26.05 seconds.
- Decision: keep.

## Final verification for this pass

- `make -B test`: 31/31 tests passed plus CLI smoke run.
- AddressSanitizer + UndefinedBehaviorSanitizer: 31/31 tests passed with no
  diagnostics.
- Deterministic evolution smoke: three generations improved best observed
  fitness from 75.0% to 87.5%.

### Remaining-horizon discounted returns

Hypothesis: using the terminal turn number in every trace target suppresses
the learning signal for late tactical and stack states. Discount each trace
state by the remaining turns (`result.turns - state.turn_number`) so positions
near a known outcome receive a sharper target. This changes only mirror/random
terminal-outcome training and introduces no card-specific knowledge.

Development screen:

```sh
./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 99-101 (49.5%).
- Learned/Handcrafted by challenger deck: Green 36%/32%, Red 32%/40%,
  Blue 60%/60%, White 70%/70%.
- Decision: rejected and reverted. The theoretically consistent TD horizon
  weakened the aggregate model and left Red clearly behind.
- Next: preserve the stable value target and add an explicit learned action
  objective, rather than trying more scalar return transformations.

### Chosen resolved-afterstate replay

Hypothesis: deployment ranks resolved successor states, while replay contains
mostly turn starts and a few pre-action stack/activation states. Add the
resolved successor of each priority action actually selected in fitted
self-play, then fit it to the same terminal outcome. This is a narrow
on-policy action-value objective with no external teacher.

Development screen:

```sh
./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 96-104 (48.0%).
- Learned/Handcrafted by challenger deck: Green 38%/28%, Red 30%/48%,
  Blue 66%/52%, White 58%/80%.
- Decision: rejected and reverted. Replaying only the selected afterstate
  reinforced poor Red/White actions and shifted the model away from useful
  turn-start calibration.
- Next: action learning needs counterfactual legal-action comparisons or an
  advantage objective, not more outcome labels on the current greedy action.

### Optimistic value-ensemble uncertainty

Hypothesis: the average of two fitted value members may underrate
underexplored legal successors, contributing to Red's low casting rate.
Deploy an upper-confidence estimate `mean + 0.15 * standard_deviation` from
the learned ensemble. The uncertainty and values are fully learned; there is
no card-specific branch.

Development screen:

```sh
./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 102-98 (51.0%).
- Learned/Handcrafted by challenger deck: Green 38%/26%, Red 32%/46%,
  Blue 62%/54%, White 72%/70%.
- Decision: rejected and reverted. Optimism helped the aggregate and White
  slice, but did not make Red cast effectively enough to clear the required
  per-deck gate.
- Next: use counterfactual action outcomes to learn preference directly;
  ensemble confidence alone cannot identify which uncertain action is good.

### Counterfactual mirror-rollout value targets

Hypothesis: a sampled main-phase state can provide a direct comparison among
all legal actions. In every fourth fitted self-play game, sample one such
state, evaluate each action with two four-turn stack-faithful Learned-mirror
rollouts, and briefly fit the resolved successor to that counterfactual
target. Handcrafted is never queried.

Development screen:

```sh
./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 98-102 (49.0%).
- Learned/Handcrafted by challenger deck: Green 36%/30%, Red 34%/42%,
  Blue 58%/58%, White 68%/74%.
- Decision: rejected and reverted. Short mirror rollouts are useful at
  deployment when averaged with the calibrated value prior, but fitting
  their noisy scalar outcomes back into that same value model damages its
  calibration.
- Next: keep action preferences in a separate policy head so counterfactual
  supervision cannot overwrite the state-value baseline.

### Learned combat damage-assignment order

Hypothesis: Learned previously randomized the attacking player's legal
damage-assignment order when one attacker had multiple blockers. Enumerate
the joint blocker-order space up to 64 candidates (bounded random samples
beyond that), select the order with the highest learned state value, and use
the same ordering logic in combat search and execution. This uses only rules
and learned value.

Development screen:

```sh
./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 101-99 (50.5%).
- Learned/Handcrafted by challenger deck: Green 38%/24%, Red 32%/42%,
  Blue 58%/62%, White 74%/70%.
- Runtime: approximately 47 seconds for 200 paired games including training.
- Decision: rejected and reverted. Learned ordering helped some positions,
  but the changed combat values reduced Red and Blue enough to fail the
  development gate. Keep the deterministic engine regression proving that
  input block order controls multi-block damage assignment.
- Next: proceed with a separate learned policy head; do not add more
  value-only combat search.

### Search-distilled listwise policy head

Hypothesis: counterfactual mirror-search comparisons contain useful action
information, but fitting them into the value model destroys calibration. Keep
the value ensemble frozen and train a separate 16-hidden-unit policy head on
grouped resolved legal afterstates. Generate `training_games / 8` Learned
mirror games, use the existing immediate-plus-two-rollout score as the
listwise teacher, and add the learned action distribution to deployment with
a bounded `0.05` prior. No Handcrafted games or card-specific rules enter
training.

Development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 102-98 (51.0%).
- Learned/Handcrafted by challenger deck: Green 36%/22%, Red 32%/42%,
  Blue 64%/62%, White 72%/70%.
- Runtime: 91.24 seconds.
- Decision: rejected. The isolated policy prior improved or held Blue and
  White without damaging value calibration, but Red remained ten percentage
  points behind and training nearly doubled the practical screen cost.
- Next: test whether uncapped long control games dominated the listwise
  corpus before reverting the policy architecture.

Balanced bounded follow-up hypothesis: cycle the 12 ordered distinct-deck
pairs, halve teacher games to `training_games / 16`, and retain at most 24
evenly spaced decision groups from each game. If control-game replay
imbalance caused the failure, a slightly stronger `0.08` prior should improve
Red while preserving the three passing deck slices and materially reduce
runtime. Otherwise reject and revert the entire policy head.

Follow-up development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 104-96 (52.0%).
- Learned/Handcrafted by challenger deck: Green 36%/22%, Red 34%/42%,
  Blue 64%/58%, White 74%/70%.
- Runtime: 69.51 seconds.
- Decision: rejected and fully reverted. Balancing the policy corpus gained
  one Red game and reduced runtime by 22 seconds, but Red remained clearly
  behind. The distilled head cannot systematically outperform the same
  mirror-search teacher from which its labels come.
- Next: improve the self-play/search target itself or use a genuinely
  different learned improvement operator; do not tune this distillation
  prior further.

### Unified observation-action actor

Hypothesis: replacing all phase-specific Learned action evaluation with one
masked observation-action actor, bootstrapped from random outcomes and then
improved by Learned mirror self-play, will remove opponent-hand leakage while
raising Learned above Handcrafted in aggregate and in every deck slice at
seed 424242. In the balanced mixed field, its lift over Random must equal or
exceed every other policy's lift for Green, Red, Blue, and White. If it fails
either per-deck development gate, reject it before the fixed eight-seed
panel.

The candidate uses:

- the existing private/public state observation and frozen-separate value
  critic;
- one action-conditional neural actor for priority, attacker inclusion,
  blocker assignment, and damage order;
- factorized combat declarations followed by one rules-engine combat
  resolution, with no combat outcome scoring;
- outcome-advantage training from Random games and Learned-vs-Learned games
  only, balanced by player and decision kind;
- no deployment rollout or access to an opponent's real hidden cards.

Pre-benchmark verification:

- the project builds with all warnings treated as errors;
- 38/38 unit and integration tests pass;
- a new observation-invariance test proves that changing opponent hand and
  library identities at fixed sizes cannot change the learner's input, while
  own-hand and public-zone changes do;
- tournament tests now require zero Learned deployment rollouts.

Development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 67-133 (33.5%), 95% interval 27.3%-40.3%.
- Learned/Handcrafted by challenger deck: Green 20%/42%, Red 32%/70%,
  Blue 30%/84%, White 52%/70%.
- Learned used 19.7 decisions and zero rollouts per game. Runtime was 16.91
  seconds, substantially cheaper than the value-search candidate.
- Decision: rejected in this form. A single outcome-weighted actor satisfies
  the information boundary and speed goal but loses every deck slice.
- Likely cause: terminal REINFORCE credit is too weak to replace the
  calibrated value policy after only the existing random/mirror corpus.
- Next: retain the unified action representation and combat factorization,
  but test actor-critic policy improvement with denser value advantages or
  substantially more cheap mirror self-play before considering ISMCTS.

Actor-critic follow-up hypothesis: blending terminal advantage with a
backward temporal-difference advantage from the separate value critic, then
adding four cheap actor-only mirror generations, will raise the seed-424242
screen above 50% and recover at least three of four deck slices without
changing the observation boundary or reintroducing combat scoring. If it
does not meet both thresholds, reject additional terminal actor tuning and
move to information-set search.

Status: implementation complete; 38/38 correctness tests passed before the
development screen.

Follow-up development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 71-129 (35.5%), 95% interval 29.2%-42.3%.
- Learned/Handcrafted by challenger deck: Green 20%/42%, Red 40%/68%,
  Blue 30%/84%, White 52%/64%.
- Runtime: 27.26 seconds; Learned again used zero deployment rollouts.
- Decision: rejected. It improved Red by four games and the aggregate by two
  games, but missed the predeclared aggregate threshold and recovered no deck
  slice.
- Next: stop tuning terminal policy gradients. Keep the shared action
  representation as an information-safe policy prior and test
  information-set search using sampled opponent hands, never the actual
  hidden state.

### Common-world information-set priority search

Hypothesis: two root simulations per legal priority action, evaluated over
the same sampled information-set worlds and continued by zero-search Learned
actors through the priority window, will restore the seed-424242 result above
50% and win at least three deck slices without opponent-hand leakage. Combat
remains entirely actor-selected and is resolved once by the rules engine. If
the candidate misses either threshold, do not advance it to the fixed seed
panel.

The root sampler reconstructs the opponent hand from its known decklist,
public zones, and public hand size. It never reads the actual hidden card
identities. Pass count and main/instant phase context are now explicit policy
inputs so the actor can distinguish yielding priority from resolving a stack
object or ending a window.

Status: implementation complete; 39/39 correctness tests passed before the
development screen.

Development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 87-113 (43.5%), 95% interval 36.8%-50.4%.
- Learned/Handcrafted by challenger deck: Green 24%/38%, Red 36%/50%,
  Blue 58%/62%, White 56%/76%.
- Search used 157.8 information-set simulations per Learned game. Runtime was
  36.02 seconds.
- Decision: rejected. Common-world information-set search recovered ten
  aggregate percentage points over the actor-only candidate and made Blue
  competitive, but it remained below 50% and lost all four deck slices.
- Next: do not run the fixed panel. The remaining weakness is the actor used
  for combat and sampled continuations; increasing root determinizations
  cannot repair that policy by itself.

### Behavior-consistent mirror actor with search supervision

Hypothesis: eliminating the actor's behavior/loss mismatch and supervising
priority choices from its own information-set root search will raise the
seed-424242 screen above 50% and win at least three deck slices. If either
threshold fails, reject the candidate without tuning its learning rate or
running the fixed seed panel.

This bounded candidate:

- uses one shared actor head outside the two-member value ensemble;
- initializes generation-zero actor logits to exactly uniform;
- does not actor-train on Random games;
- freezes the actor for each complete mirror-self-play collection
  generation;
- samples combat from that shared actor at temperature 1.0, matching the
  softmax used by its one-epoch loss;
- treats common-world information-set root-search choices as positive
  cross-entropy targets for priority only;
- trains combat only from on-policy terminal/critic advantage, without
  enumerating or scoring combat candidates;
- keeps the value ensemble and its existing two fitted generations
  separate.

Status: implementation complete; 39/39 correctness tests passed before the
development screen.

Development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 83-117 (41.5%), 95% interval 34.9%-48.4%.
- Learned/Handcrafted by challenger deck: Green 22%/36%, Red 40%/42%,
  Blue 66%/64%, White 38%/92%.
- Learned used 171.2 information-set simulations per game. Runtime was 37.42
  seconds.
- Decision: rejected. The behavior-consistent actor recovered Blue and made
  Red nearly even, but missed the predeclared aggregate threshold and won
  only one deck slice. White regressed sharply because outcome-only combat
  learning did not learn a reliable Millstone/Moat control plan.
- Next: do not tune this candidate or run the fixed panel. A stronger combat
  policy needs denser self-generated supervision that still avoids bespoke
  combat outcome scoring.

### Behavior-consistent data-scaling check

Hypothesis: the corrected shared actor is undertrained rather than
architecturally limited. Holding the algorithm and learning rates fixed while
raising `--train-games` from 800 to 4,000 will exceed 50% aggregate against
Handcrafted and win at least three deck slices at seed 424242. This is a
one-shot scaling check, not a hyperparameter sweep; if either gate fails,
reject sparse terminal combat credit and change the learning signal.

Planned development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 4000
```

- Aggregate: 76-124 (38.0%), 95% interval 31.6%-44.9%.
- Learned/Handcrafted by challenger deck: Green 12%/44%, Red 40%/46%,
  Blue 54%/68%, White 46%/90%.
- Search used 195.1 information-set simulations per Learned game. Runtime was
  212.26 seconds.
- Decision: rejected. Five times the data made the aggregate and every deck
  slice worse, with Green collapsing most sharply. Sparse terminal combat
  credit is not merely undertrained, and more games are not the next step.
- Next: separate decision heads to prevent priority/combat gradient
  interference and replace noisy hard root labels with soft score
  distributions; do not scale this objective again.

### Isolated soft priority teacher with phase-faithful search

Hypothesis: White's regression is a priority-learning failure caused by
shared-head interference, noisy one-hot labels from two sampled worlds, and
bootstrapping Pass before its phase transition. A separate Priority network,
soft targets from all common-world root scores, a frozen critic teacher, and
candidate continuation through the real remaining phase sequence to the next
turn observation will recover White and Blue without bespoke card or combat
knowledge.

The priority target is
`0.9 * softmax((q - max(q)) / 0.10) + 0.1 / legal_actions`, weighted equally
per actor-game. Priority receives no terminal policy-gradient loss. Attack,
block, and damage-order learning remain isolated, on-policy, and
card-agnostic. The search budget remains two common sampled worlds per legal
action.

Development gate: at seed 424242 with five paired repetitions and 800
training games, White and Blue must each reach at least 60% and aggregate
Learned must reach at least 45%. Otherwise reject this teacher/horizon and do
not scale its data.

Planned command:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

Status: implementation complete; 40/40 correctness tests passed before the
development screen. The critic was fit once from Random traces and remained
frozen through all four actor generations.

Development screen:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

- Aggregate: 80-120 (40.0%), 95% interval 33.5%-46.9%.
- Learned/Handcrafted by challenger deck: Green 22%/40%, Red 32%/56%,
  Blue 52%/76%, White 54%/68%.
- Learned used 154.9 information-set simulations per game. Runtime was
  29.05 seconds.
- Decision: rejected. White missed its 60% gate by six points, Blue missed
  its 60% gate by eight points, and aggregate missed its 45% gate by five
  points. The isolated soft teacher recovered White relative to the prior
  behavior-consistent actor, but lost Blue and did not improve aggregate.
- Next: do not tune or scale this candidate and do not run the fixed seed
  panel. Before another actor objective, measure teacher quality offline by
  comparing the K=2 root rankings on held-out decision states with much
  larger common-world estimates; this will distinguish an under-sampled
  teacher from critic-horizon bias without changing the deployed bot.

### White lock-plan teacher diagnostic

Hypothesis: the two-world search teacher is too noisy, but its critic and
next-turn horizon contain the right underlying preference. In a valid held-out
White first-main state with one Moat, one untapped Millstone, four Plains, and
a redundant Moat in hand against a ground attacker, a 64-world reference
must rank milling the opponent above casting the redundant Moat. Repeated
two-world rankings should agree with that reference less than 80% of the
time; that combination would justify improving teacher sampling. If the
64-world reference itself prefers the redundant Moat, the critic/continuation
is biased and more root samples cannot solve White.

This is an evaluation-only fixture and must not feed card-specific labels or
features into Learned training.

The diagnostic uses the same seed-derived model as the paired benchmark. It
holds the model fixed, evaluates every legal root action over 64 shared
information-set worlds, then creates 32 independent rankings from two shared
worlds apiece. Actor continuations remain Learned mirrors at training
temperature, and the fixture/result API is not called by training or deployed
policy code.

Status: implementation complete; 41/41 correctness tests passed. The fixture
test checks exact deck conservation, the four expected legal actions, the
ready ground creature, and that Moat rejects its attack.

Planned diagnostic:

```sh
/usr/bin/time -p ./build/alpha-sim --diagnose-white-plan \
  --seed 424242 --train-games 800
```

Diagnostic result:

- K=64 scores: Pass 0.503323, redundant Moat 0.522587, self-mill
  0.446958, and opponent mill 0.577475.
- The reference best action was opponent mill. It exceeded redundant Moat by
  0.054888, so the frozen critic and next-turn horizon contain the desired
  lock-plan preference.
- Across 32 independent K=2 rankings, opponent mill was best 25 times and
  Pass was best seven times. Redundant Moat and self-mill were never best.
  Reference-best agreement was 25/32 (78.1%), just below the predeclared 80%
  threshold.
- In the narrower plan comparison, opponent mill beat redundant Moat in all
  32 K=2 trials. The seven reference disagreements were therefore
  mill-versus-Pass errors, not redundant-Moat errors.
- Runtime: 28.44 seconds including 800-game model training.
- Interpretation: the preregistered sampling-noise condition passed, so a
  higher-sample teacher is justified as a bounded next experiment. However,
  this fixture does not support sampling noise as the direct cause of
  redundant-Moat play: even K=2 preserved the correct pairwise plan ordering
  100% of the time. State coverage or policy distillation may still be the
  White bottleneck.
- Next: do not tune this diagnostic. If continuing, predeclare a
  teacher-sampling experiment that raises collection K while holding the
  critic, actor loss, fixture-independent features, and deployment search
  fixed; separately track Pass-versus-action labels so a gain cannot be
  misattributed to the Moat comparison.

### Independent training/evaluation seed variance study

Process correction: prior CLI benchmarks derived the Learned training seed
from the evaluation seed, and the stability panel trained a new model for
each evaluation run. That confounds model-generation variance with game
sampling variance. Training and evaluation seeds must now be independent,
and a fixed trained model must be reused across every evaluation seed and
baseline in a panel.

Hypothesis: model-generation variance is the larger source of the observed
Learned instability. In a 3x3 Learned-versus-Handcrafted matrix with training
seeds `424242, 101, 707` and evaluation seeds `424242, 101, 707`, the mean of
the three column spans (variation across training seeds at fixed evaluation)
will exceed the mean of the three row spans (variation across evaluation
seeds for one fixed model). This is an evaluation-process experiment only;
no policy result will be accepted or rejected from it.

Planned command:

```sh
/usr/bin/time -p ./build/alpha-sim --variance-study \
  --games 5 --train-games 800
```

Status: implementation complete; 42/42 correctness tests passed. The new
regression test trains one explicit model, reuses it for repeated and
different evaluation seeds, and verifies that the reported training seed
does not change. Benchmark, White diagnostic, and stability CLI paths now
pretrain exactly once from `--train-seed` (default `424242`); the stability
panel reuses that model across every evaluation seed and baseline.

Study result (Learned win rate against Handcrafted):

| training seed \ evaluation seed | 424242 | 101 | 707 | row span |
| --- | ---: | ---: | ---: | ---: |
| 424242 | 37.5% | 41.5% | 44.5% | 7.0 pp |
| 101 | 43.0% | 43.5% | 49.5% | 6.5 pp |
| 707 | 40.0% | 43.0% | 48.5% | 8.5 pp |

- Column spans across training seeds were 5.5 points at evaluation seed
  424242, 2.0 points at 101, and 5.0 points at 707.
- Mean row span (evaluation variance with a fixed model) was 7.3 points.
  Mean column span (training variance at a fixed evaluation seed) was 4.2
  points.
- Runtime: 105.86 seconds for three 800-game training runs and nine
  200-game paired evaluations.
- Decision: the preregistered hypothesis was not supported. Training seed
  changed results materially, but evaluation-seed variation was larger in
  this panel. Evaluation seed 707 was consistently favorable and 424242
  consistently unfavorable across all three fixed models.
- Next: keep training/evaluation seeds independent permanently. Bot-strength
  claims should pool the fixed evaluation-seed panel against one explicitly
  named fixed model, then repeat that complete panel across training seeds
  only when measuring model-generation robustness. Do not select either seed
  from this matrix or tune the policy to it.

## Evaluation-process correction

Three reported reruns of the identical current binary/configuration, changing
only the coupled CLI seed, produced 40.0% at seed 424242, 44.0% at seed 101,
and 50.5% at seed 707 over 200 paired games each. Near 50%, 200 games have a
rough 95% sampling margin of seven percentage points and each 50-game deck
slice has a margin near fourteen points. These screens cannot support the
two-to-four-point accept/reject decisions repeatedly made above.

The spread combines two sources because `--seed` currently controls both
model training and benchmark games: training-procedure variance and finite
evaluation variance. It therefore proves that the end-to-end decision
procedure is unstable, but does not by itself identify which source
dominates.

Decision: change the research workflow before further policy tuning.

- Add a separate training seed and hold one trained model fixed across
  evaluation seeds.
- Use a factorial train-seed/evaluation-seed study to measure both variance
  components.
- Keep 200-game runs only as large-regression smoke tests; use held-out
  decision/value metrics for iteration and at least 2,000 paired games for
  milestone claims around three percentage points.
- Restore the strongest clean value-search architecture as the working
  champion, but replace its old opponent-hand-peeking rollout with
  information-set sampling before treating it as valid.
- Generalize the White diagnostic into deck-balanced held-out probes,
  including Red burn targeting/holding decisions.
- Train future policy/value generations by iterated search supervision and
  compare each frozen generation against prior frozen generations before
  consulting Handcrafted at a milestone.

### Information-safe restoration of the value-search champion

Hypothesis: the historical value-search architecture remains a substantially
stronger working baseline than the unified actor after its hidden-state
rollouts are replaced with common information-set determinizations and its
priority continuation uses the current phase-aware engine. Restoring it as a
separate `learned-value` variant, while retaining the actor as
`learned-actor`, should beat the actor by at least eight percentage points
when one frozen model of each is pooled over evaluation seeds
`424242, 101, 707`.

This is a champion-restoration experiment, not a claim that the historical
54.1% estimate transfers unchanged. That estimate retrained a model for each
evaluation seed. The restored variant must:

- never inspect the real opponent hand/library partition or real future
  library order;
- use identical sampled worlds for every candidate action;
- preserve the MVP engine's stack/priority loop in every currently modeled
  window and continue through the remaining supported turn phases;
- keep its public-board learned-value combat search isolated from the
  actor-only combat research path;
- use an explicit training seed and frozen model across evaluation seeds;
- expose `learned-value` and `learned-actor` separately in CLI output and
  permit a direct paired benchmark between them.

The 600-game three-seed pool is only a large-regression/champion-restoration
check. No few-point strength claim will be accepted from it; milestone claims
still require the held-out probes and 2,000+ paired games.

Restoration result (one frozen model, training seed 424242, 200 paired games
per evaluation seed):

```sh
./build/alpha-sim --benchmark --games 5 --seed <eval> --train-seed 424242 \
  --challenger learned-value --baseline handcrafted --train-games 800
./build/alpha-sim --benchmark --games 5 --seed <eval> --train-seed 424242 \
  --challenger learned-value --baseline learned-actor --train-games 800
```

- Versus Handcrafted: 40.5% (424242), 45.0% (101), 44.5% (707); pooled
  260-340 (43.3%).
- Pooled deck slices, learned-value/Handcrafted: Green 35.3%/28.0%,
  Red 31.3%/42.0%, Blue 45.3%/74.7%, White 61.3%/82.0%.
- Versus learned-actor: 48.5% (424242), 50.0% (101), 50.0% (707); pooled
  297-303 (49.5%).
- Decision: the hypothesis is refuted. The information-safe value-search
  variant is statistically indistinguishable from the actor and roughly two
  points above it against Handcrafted, far below the predeclared
  eight-point gap.
- Interpretation: Blue, historically the champion's best slice at 66-76%,
  is now its worst at 45.3%. Counterspell decisions are exactly where
  knowing the opponent's real hand helps most, so a large share of the
  historical 54.1% estimate was probably hidden-information leakage in the
  old rollout, possibly compounded by per-seed retraining. The historical
  figure should be treated as contaminated, not as a restoration target.
- Next: there is no clean champion to restore. Keep `learned-value` and
  `learned-actor` as two honest ~41-43% baselines, wire the probe corpus
  into a model-scoring CLI, and pursue iterated search supervision as the
  first candidate measured under the corrected workflow.

The engine does not yet open priority after attacker or blocker declaration.
That is a pre-existing rules-engine limitation, not a property hidden by this
experiment.

#### Restoration implementation and three-seed regression result

Implementation tested:

- `LearnedVariant::ValueSearchChampion` is the default `learned` /
  `learned-value`; `LearnedVariant::UnifiedActor` is explicitly selected as
  `learned-actor` and is excluded from the default mixed field.
- Each Learned seat can carry its own frozen, variant-tagged model. The direct
  paired harness therefore supports Actor-versus-Champion and frozen
  generation-versus-generation comparisons without a shared-model fallback.
- The Champion trains its value ensemble from Random traces followed by two
  generations of value-only Champion self-play. It does not use the Actor,
  Handcrafted, Handcrafted labels, or card-specific policy weights.
- Champion root search samples common determinizations from the acting
  player's information set, applies every candidate to identical worlds,
  follows the real pass/priority/stack machinery through all currently
  modeled windows, resumes from the correct current phase, and continues for
  a bounded four-turn Champion-mirror horizon. Nested search depth is
  explicitly zero. The one-ply prior is the mean over all common worlds.
- Champion combat restores the historical public-board value enumeration;
  the Actor combat path remains separate.

Correctness gates before the final panel:

- `make test`: 45/45 engine tests, including hidden-identity invariance,
  phase-sensitive stack continuation, bounded rollout accounting, distinct
  frozen Actor/Champion model routing, and equal-budget frozen generation
  comparisons.
- Held-out probe suite: 10/10 groups across 16 fixtures.
- Representative CLI simulation passed.

Exact final commands (training seed `424242`, 800 training games, two
rollouts per root action, 200 paired games per evaluation seed):

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 \
  --seed 424242 --train-seed 424242 --train-games 800 \
  --challenger learned-actor --baseline learned-value
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 \
  --seed 101 --train-seed 424242 --train-games 800 \
  --challenger learned-actor --baseline learned-value
/usr/bin/time -p ./build/alpha-sim --benchmark --games 5 \
  --seed 707 --train-seed 424242 --train-games 800 \
  --challenger learned-actor --baseline learned-value
```

The deterministic training procedure produced the same frozen model for each
separate process; no evaluation seed entered model training.

| evaluation seed | Actor | Champion | Actor rate | runtime |
| ---: | ---: | ---: | ---: | ---: |
| 424242 | 103 | 97 | 51.5% | 51.75 s |
| 101 | 100 | 100 | 50.0% | 51.14 s |
| 707 | 100 | 100 | 50.0% | 50.06 s |
| **pooled** | **303** | **297** | **50.5%** | **152.95 s** |

The pooled Actor Wilson 95% interval was 46.5% to 54.5%. Pooled records by
the policy's own deck were:

| deck | Actor | Champion | higher record |
| --- | ---: | ---: | --- |
| Green | 47-103 (31.3%) | 61-89 (40.7%) | Champion |
| Red | 59-91 (39.3%) | 63-87 (42.0%) | Champion |
| Blue | 100-50 (66.7%) | 68-82 (45.3%) | Actor |
| White | 97-53 (64.7%) | 105-45 (70.0%) | Champion |

Average work per game was 29.7 decisions and 159.7 root continuations for
Actor versus 26.1 decisions and 140.2 root continuations for Champion.

An audit after the first three-seed execution found that the shallow prior
used only the first common world. That first execution was therefore marked
superseded even though it also produced Actor 303-297 Champion (runtimes
47.74, 53.11, and 47.75 seconds). After changing the prior to its common-world
mean, all three final records and per-deck records reproduced exactly; the
table above is from the corrected binary.

Decision: reject the predeclared restoration hypothesis. Champion did not
beat Actor by eight points; it lost by one pooled point, with a confidence
interval spanning a material advantage for either policy. It did produce
better deck records on Green, Red, and White, but Actor's large Blue advantage
outweighed them. This is not a strength milestone, does not justify a
Handcrafted gate, and does not satisfy the project's per-deck definition of a
stronger policy. `learned-value` remains the explicit restored incumbent
architecture, not a newly validated strength claim; `learned-actor` remains a
challenger.

Known restoration limitations retained for a separate falsifiable
experiment:

- The rules engine currently provides priority in First Main, Begin Combat,
  End Combat, and Second Main, but not after declaring attackers or blockers.
  “All modeled priority windows” must not be described as every Magic
  priority window.
- Champion attack scoring averages the enumerated legal block assignments,
  while an actual Champion defender chooses its highest-valued assignment.
  The search response is therefore not yet the exact deployed mirror.
- Multi-block order participates in candidate scoring, but the deployed
  Champion attacker currently executes a random damage order. This is an
  agency/model mismatch, not a hidden-information leak.

Next: predeclare a public-board combat-consistency experiment that makes
rollout blocking match the deployed Champion mirror and gives the attacking
Champion its legal damage-order choice. Check the held-out per-deck probes
first, with Blue counter/creature timing called out separately, then use the
same 600-game Actor comparison only as a large-regression screen. Do not run
the Handcrafted milestone gate until a challenger clears offline probes and
the frozen-generation gates.

### Deck-balanced deep-reference instrumentation

Hypothesis: on `probe-dev-v1`, the Actor's existing two-world search should
rank actions more like a deeper Actor-mirror reference than its raw policy
head does. With one frozen Actor trained from seed `424242` on 800 games,
two-world search must improve pooled top-one agreement or mean regret, and
must not worsen any deck's mean regret by more than 0.01.

Reference configuration:

- 128 common information-set worlds per candidate;
- a 12-turn Actor-mirror horizon with nested root search disabled;
- terminal outcome when reached, otherwise the frozen critic bootstrap;
- no shallow-prior blend in the reference Q value;
- stable seeds derived from corpus ID, probe ID, and world index rather than
  probe iteration order;
- aligned per-action samples so paired uncertainty is measured directly.

Low-margin best-versus-action pairs will be rerun with 512 worlds and/or a
24-turn horizon before being called stable. A best set includes actions
within `max(0.01, paired 95% uncertainty)` of the best. A stable pair requires
an absolute Q difference of at least 0.03 and a paired 95% interval excluding
zero.

The report must include, pooled and per deck:

- tie-aware top-one agreement;
- stable-pair ranking agreement;
- mean reference regret;
- critic MSE/Brier, soft-label log loss, signed bias, and pooled five-bin
  calibration error;
- hidden-repartition invariance of raw Q samples, predictions, and metrics.

This 16-position development corpus is instrumentation, not evidence of
playing strength: each deck has only four positions. It can reject broken
learning/search changes quickly, but it cannot accept a new champion. A
separate sealed corpus of at least 200 snapshotted decisions is required
before offline metrics support a milestone claim.

Independent review requested a Handcrafted probe score as a measure of
headroom. We will report its one-time policy agreement/regret during this
instrumentation milestone, but not call it an optimal-policy ceiling: the
reference itself is generated by the frozen Actor and can be wrong where
Handcrafted is right. Handcrafted will not contribute labels, critic targets,
search responses, training data, or per-generation model selection. After
this diagnostic, it remains reserved for declared milestones as required by
`AGENTS.md`.

The 2026-07-24 23:09 independent review also identified continuation-policy
bias as a distinct risk that additional common worlds cannot remove. Before
running the preregistered probe experiment, add a diagnostic cross-check using
the separately frozen Value model trained with the same seed and game budget.
It will rescore all 16 probes with the same world count, horizon, seed
derivation, and disabled shallow prior, changing only to the Value model and
Value-mirror continuation. Report:

- Actor-stable action pairs whose point-estimate ordering reverses under the
  Value reference;
- the stricter subset whose opposite ordering is stable under both
  references; and
- pooled and per-deck counts, without replacing the preregistered Actor
  labels or using this diagnostic to select a model.

This is best described as *reference sensitivity*, not proof that the
continuation policy alone caused a disagreement: the current variants require
separately trained, variant-tagged critics, so this cross-check changes both
the continuation policy and its bootstrap model. A reversal is therefore a
warning that a label is not robust to the learned reference architecture.
The cached primary labels remain Actor-derived and the Value cross-check is
recomputed as a diagnostic.

#### Pre-run audit corrections

An independent implementation audit found two interpretation errors before
the canonical cache was generated. They are corrections to the measurement,
not post-result changes:

- A deployed-policy row must follow the policy actually used for each decision
  kind. Actor uses K=2/H=0 search for Priority and its raw binary head for
  Attack. Value uses K=2/H=4 plus its shallow-prior blend for Priority and its
  public-board whole-attack-set evaluator for Attack. The deep binary Attack
  scorer remains valid as a *teacher/reference*, but must not be mislabeled as
  either bot's deployed combat policy.
- The critic predicts the value of following a policy from the root state,
  \(V_\pi(s)\). Comparing it with `max_a Q_reference(s,a)` confounds value
  calibration with policy-improvement headroom. Critic Brier/MSE/log loss,
  signed bias, and ECE will instead use the reference Q of the action selected
  by the evaluated policy; exact score ties use the uniform mean Q, matching
  deployed uniform tie-breaking. Maximum reference Q remains the target for
  regret and best-set metrics.

Before the K=128/H=12 run, also require:

- bit-identical hidden-repartition predictions and identical final metrics for
  Actor raw/deployed, Value deployed/deep, and the Handcrafted diagnostic, in
  addition to the already-required raw reference Q invariance;
- cache metadata binding the fixed reference seed, an explicit labeler
  semantic revision, the exact Actor model fingerprint, and the corpus
  information-set fingerprint; and
- an actionable list of low-margin best-versus-action pairs. K=128 results for
  those pairs are provisional; do not call their ordering stable until the
  preregistered K=512 and/or H=24 escalation is run.

#### Canonical `probe-dev-v1` result

The latest `REVIEW.md` entry was checked immediately before and after this
run. It remained the 2026-07-24 23:09 PDT review whose continuation-bias,
deployed-policy-fidelity, Handcrafted-diagnostic, and invariance requests are
implemented above.

Exact command:

```sh
/usr/bin/time -p ./build/alpha-sim --score-probes \
  --probe-worlds 128 --probe-horizon 12 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v1.labels.tsv \
  --refresh-probe-cache
```

The run completed in 72.93 seconds. It generated 5,648 cache rows
(365,525 bytes) and bound them to:

- Actor fingerprint
  `26cb6bc9c0633b901da5d59bbeb924e06c0b61a580eca51af3cb104ab535031c`;
- Value diagnostic fingerprint
  `7fa5978ef57e8ccb903380f5c3d1c48480e5e6adc8af67973082a9dfe49980a3`;
- corpus information-set fingerprint `4373b6816d309cf8`;
- `actor-mirror-common-world-v2` /
  `probe-score-semantics-v2`; and
- fixed reference seed `0x50524f4245524546`.

All raw reference samples, all five policy views, both critics, and all final
metrics were bit-identical under hidden-zone repartition across all 16 probes.

Pooled results against the Actor-derived deep reference:

| policy view | top-one | stable-pair agreement | mean regret | selected-action critic Brier/MSE | log loss | bias |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Actor raw head | 87.5% | 66.7% | 0.0102 | 0.1557 | 0.6246 | +0.1037 |
| Actor deployed | 100.0% | 100.0% | 0.0001 | 0.1504 | 0.6210 | +0.0935 |
| Value deployed | 87.5% | 94.4% | 0.0028 | 0.1135 | 0.5441 | +0.0368 |
| Handcrafted agreement diagnostic | 87.5% | 91.7% | 0.0039 | n/a | n/a | n/a |
| Value deep-continuation diagnostic | 100.0% | 94.4% | 0.0011 | 0.1156 | 0.5418 | +0.0351 |

Actor raw/deployed deck slices:

| deck | raw/deployed top-one | stable pairs | raw/deployed pair agreement | raw/deployed regret |
| --- | ---: | ---: | ---: | ---: |
| Green | 100% / 100% | 0 | n/a | 0.0005 / 0.0005 |
| Red | 75% / 100% | 16 | 62.5% / 100% | 0.0335 / 0.0000 |
| Blue | 75% / 100% | 2 | 100% / 100% | 0.0071 / 0.0000 |
| White | 100% / 100% | 0 | n/a | 0.0000 / 0.0000 |

The preregistered instrumentation hypothesis is accepted on this development
corpus: K=2/H=0 deployed Actor search improved pooled top-one agreement and
mean regret, and no deck's regret worsened at all, let alone by 0.01. This is
not a playing-strength result or a champion promotion. The apparent 100%
top-one score is heavily qualified: only 18 action pairs were stable, 16 of
them Red and two Blue; Green and White had no stable pairs.

The continuation cross-check found one point-sign reversal among the 18
Actor-stable pairs:
`red.bolt-blocker.v1`, pass versus Bolt-self-player, Actor delta +0.0470
versus Value delta -0.0116. The Value pair was not stable, so no pair had a
stable opposite ordering under both references.

Seventeen best-versus-action pairs require escalation: Green 5, Red 3,
Blue 3, White 6. Several Green/White pairs had exactly zero delta and zero
sampling error, so more worlds alone cannot resolve them; horizon sensitivity
is the more relevant next check. The Actor critic also remains badly
miscalibrated on Green (selected-action bias +0.4902), despite good action
ranking on these four fixtures.

Decision: accept the CLI/cache/metric harness and the narrow K=2-over-raw
diagnostic result. Do not claim that the 16 probes validate a stronger bot,
that Handcrafted is an objective ceiling, or that low-margin Green/White
plans are solved.

Next experiment, before fitting G1: compare the frozen Actor's proposed
K=8/H=4 teacher with K=2/H=0 using the cached K=128/H=12 labels. Because the
ranking metric is saturated, also report candidate-Q MAE/RMSE for search rows.
K=8/H=4 must retain 100% stable-pair agreement, worsen no deck's regret by
more than 0.01, and improve or tie pooled Q MAE. Separately rerun provisional
best-action pairs at H=24 and K=512; exact-zero pairs are primarily a horizon
test. Only then begin the preregistered frozen G0-to-G1 iteration.

Escalation commands are fixed before execution:

```sh
/usr/bin/time -p ./build/alpha-sim --score-probes \
  --probe-worlds 128 --probe-horizon 24 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v1-k128-h24.labels.tsv \
  --refresh-probe-cache
/usr/bin/time -p ./build/alpha-sim --score-probes \
  --probe-worlds 512 --probe-horizon 12 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v1-k512-h12.labels.tsv \
  --refresh-probe-cache
```

H=24 tests continuation-horizon sensitivity; the specific hypothesis is that
at least some exact-zero Green/White pairs acquire nonzero separation. K=512
tests sampling stability; point-estimate signs for the K=128 stable pairs
should be retained, while wide intervals should narrow. These are reference
diagnostics, not additional chances to choose whichever label favors a
candidate.

H=24 result:

- Exact command: the first escalation command above.
- Runtime: 80.51 seconds.
- Hidden-repartition invariance passed for all labels, policy views, critics,
  and metrics.
- Stable pairs increased from 18 to 19: Red 16, Blue 3, Green/White still
  zero.
- Low-margin pairs changed from 17 to 16: Green 5, Red 3, Blue 2, White 6.
- No Actor-stable pair reversed sign under the Value reference, versus one
  non-dual-stable Red reversal at H=12.
- Actor deployed remained 100% top-one / 100% stable-pair agreement, with
  pooled regret 0.0016. Per-deck regret was Green 0.0066 and zero for
  Red/Blue/White, so it remained within the preregistered no-0.01-regression
  diagnostic gate.

Decision: reject the H=24 exact-zero hypothesis. The exact-zero Green/White
pairs did not acquire useful separation. Green develop, both Green attack
fixtures, all four White plan fixtures, and Red face-lethal still contained
zero-delta best comparisons. Longer continuation did change other labels:
Green Tsunami's best gap moved to 0.0263; Blue Counterspell against lethal
Bolt changed from a small Counter advantage at H=12 (+0.0092) to an exact
tie; White's 0.0168 redundant-Moat gap collapsed to zero. The reference is
therefore horizon-sensitive on some low-margin pairs and simply signal-free
on others. More worlds cannot fix exact zero with zero paired variance.

Next: run the preregistered K=512/H=12 sampling check, then use the
candidate-Q comparison to decide whether K=8/H=4 is a credible G1 teacher.
Treat H=12/H=24 disagreements as an explicit uncertainty slice rather than
choosing the horizon whose answer looks strategically preferable.

### Continuation-healing audit (predeclared)

The 2026-07-24 23:38 PDT independent review reproduced the canonical run and
identified a more fundamental interpretation problem: after a forced root
Pass, the same Actor continuation can immediately take the action that the
probe meant to contrast. That makes several labels measure "do this now
versus one priority window later," not "do this versus decline the plan."

This supersedes the queued K=512/H=12 sampling escalation and pauses the
K=8/H=4 teacher gate. More worlds cannot repair a counterfactual whose two
branches reconverge by construction.

Before any G1 training:

1. unit-trace `red.bolt-face-lethal.v1` through the first and second main
   priority windows and prove whether the Pass branch casts the same Bolt
   later;
2. unit-trace `blue.counter-lethal-bolt.v1`, whose Pass is already
   irreversible because it resolves a lethal stack object, and distinguish a
   candidate-application error from a genuine all-world loss under the
   continuation;
3. implement either root-irreversible fixture semantics or an explicitly
   named persistent-deviation reference mode, without changing the deployed
   bot;
4. version the cache semantics and add legality, exact branch-state,
   determinism, and hidden-repartition regressions; and
5. regenerate only a small diagnostic sample first. The fix is accepted only
   if Red Pass and Bolt-to-opponent no longer produce identical branch
   trajectories, Blue Pass is an immediate terminal loss while Counterspell
   resolves legally, and hidden-repartition invariance remains exact.

No playing-strength claim will be made from this audit. The 16 hand-authored
probes remain a development corpus; real-game disagreement and Learned-loss
states are still required for an unbiased validation corpus.

Implementation choice: use the reviewer's root-irreversible-fixture option,
not an artificial persistent-Pass rule. Atomic action Q should keep real
Magic semantics: a player that passes First Main is genuinely allowed to act
again later. `probe-dev-v2` therefore preserves v1 for reproducibility, moves
the develop/plan timing fixtures to a final priority opportunity, taps out the
Green fixture's opponent, and removes the accidental summoning sickness from
the emergency-Moat attacker. This keeps any eventual G1 labels executable by
the ordinary engine rather than teaching a commitment the deployed action
does not make.

The exact first diagnostic command is:

```sh
/usr/bin/time -p ./build/alpha-sim --score-probes \
  --probe-worlds 8 --probe-horizon 4 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v2-k8-h4-audit.labels.tsv \
  --refresh-probe-cache
```

This is a semantic smoke test, not a model-selection screen. Acceptance
requires exact hidden-repartition invariance; Red Pass must no longer equal
the terminal Bolt-to-opponent line; Blue Pass must be zero while legal
Counterspell has nonzero value; and the emergency-Moat branches must no
longer reconverge before combat. Green/White long-plan ranking at K=8 is
recorded but is too small to accept a teacher.

First v2 diagnostic result:

- Runtime: 41.64 seconds; all five views and labels passed exact
  hidden-repartition invariance.
- The branch-transition tests passed, including lethal Blue Pass and legal
  Counterspell resolution.
- The label acceptance gate failed. Red Pass and Bolt still both scored 1:
  waiting one turn changed the trajectory but Red still won later. Blue Pass
  and Counterspell both scored 0 at H=4 because the defended line still lost.
  Emergency-Moat Pass and cast likewise both scored 0 because the single
  attacker no longer attacked while sick in v1, but after fixing sickness the
  Moat line still died to the continuation's burn.

This separates two defects: v1 continuation healing was real, but a merely
different branch can still collapse to the same terminal outcome. Before
repeating the same fixed command, the v2-only fixtures now give the delayed
choice a public terminal consequence: Green's Bear can block a visible lethal
Fire Elemental, Red's visible Water Elemental wins before a delayed Bolt,
Blue has a visible lethal Water Elemental after Counterspell, and emergency
Moat faces four visible Fire Elementals at 20 life. V1 remains unchanged.
The repeated command and acceptance criteria remain exactly those declared
above.

Second v2 K=8/H=4 result:

- Runtime: 44.28 seconds; exact hidden-repartition invariance again passed.
- Blue was repaired as a transition and as a label: Counterspell beat Pass
  by 0.25 (2/8 winning continuations), though the tiny-sample interval was
  wide.
- Green Bear gained a small +0.0271 signal and Red immediate Bolt gained
  +0.125, but neither was stable at K=8.
- Emergency Moat still tied at zero: its Pass branch loses in the current
  combat, while its cast branch survives the combat and then loses to burn
  within four continuation turns. Thus the remaining equality is not
  branch healing; it is long-horizon terminal-outcome saturation.

Decision: the semantic smoke test is only partially accepted. Candidate
application, root timing, lethal stack behavior, and hidden-information
isolation are correct. H=4 is rejected as the tactical reference horizon for
these probes because it erases a known defensive improvement. Do not add more
fixture power/life merely to make a long terminal rollout agree with the
fixture name.

The next exact diagnostic uses the same frozen model and worlds but bootstraps
immediately after the current turn:

```sh
/usr/bin/time -p ./build/alpha-sim --score-probes \
  --probe-worlds 8 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v2-k8-h0-audit.labels.tsv \
  --refresh-probe-cache
```

Hypothesis: H=0 preserves nonzero correct-direction separation for the four
public tactical consequences (Bear blocker, immediate Bolt, Counterspell,
and emergency Moat) that H=4 partially saturates. Exact hidden-repartition
invariance remains mandatory. This still cannot establish playing strength;
if accepted, it selects a short-horizon search teacher whose critic must then
improve through TD(lambda) training rather than pretending that a longer
terminal rollout is automatically a better label.

H=0 result:

- Runtime: 43.13 seconds; cache
  `data/probe-dev-v2-k8-h0-audit.labels.tsv`; exact hidden-repartition
  invariance passed for labels, five policy views, critics, and metrics.
- Green Bear beat Pass by +0.0179 with zero paired sampling error.
- Immediate Red Bolt scored 1.0000 versus Pass 0.4566, a +0.5434 gap.
- Blue Counterspell scored 0.1642 versus terminal Pass 0, a +0.1642 gap.
- Emergency Moat and Pass both scored 0.3713.

Decision: reject the literal four-of-four hypothesis, accept H=0 over H=4
for the first tactical teacher. It preserves three real action signals that
the longer rollout attenuated or saturated. The White failure is now
diagnostic rather than semantic: without Moat, one untapped, nonsick
Fire Elemental attacking an unblocked player at five life would end the
game, but the Actor continuation reaches the same nonterminal bootstrap as
the Moat branch. Therefore its raw Attack head skipped the free lethal
attack. G1 must search-supervise Attack as well as Priority; merely training
the critic or extending the horizon cannot repair that behavior.

This remains development evidence only. The next milestone is a frozen
G0-to-G1 iteration using card-agnostic, information-safe K=8/H=0 teacher
scores for every recorded Priority and binary Attack option, soft targets,
TD(lambda) critic returns, balanced self-play, immutable G0, and a
three-generation replay window. First compare G1 with G0 offline and directly;
do not touch Handcrafted until G1 clears that milestone.

Checkpoint audit:

- Independent code review found that the Value attack-set scorer's deployed
  exact-tie behavior (retain the first candidate) was being evaluated as a
  uniform tie. That could misstate top-one agreement, regret, and the
  selected-action critic target. `ProbePrediction` now carries an optional
  exact deployed selection; random-tie policies retain the uniform
  expectation. A tied-selection regression verifies the distinction.
- `make test` passes 51 engine, 12 corpus, 10 metric, and 9 runner tests plus
  CLI smoke under `-Werror`.
- AddressSanitizer/UndefinedBehaviorSanitizer pass all 51 engine tests and all
  9 probe-runner tests (`detect_leaks=0`, because macOS's sanitizer runtime
  rejects leak detection).

### Frozen G0 to G1 search-as-teacher generation (predeclared)

The first iterated candidate is fixed before implementation results:

- parent G0: `train_learned_actor_model(800, 424242)`, retained bit-for-bit;
- one generation, 24 self-play games in the exact balanced block of six
  unordered distinct-deck matchups, both deck-to-seat orientations, and both
  starting players;
- both seats use the frozen G0 mirror, never Handcrafted;
- every retained Priority and binary Attack root uses eight common
  information-set worlds, one continuation/world, H=0, no shallow prior;
- at most 24 searched roots per seat and decision kind per game;
- soft all-action targets use temperature 0.10 with 90% teacher mass;
- critic targets use TD(lambda), lambda=0.90, terminal win/draw/loss
  1/0.5/0, and frozen-parent next-state values;
- replay retains three immutable generation shards (one exists for G1);
- critic update: two epochs at learning rate 0.002 with independent ensemble
  shuffle seeds; Priority/Attack policy update: two epochs at 0.001;
- Block and DamageOrder heads are copied unchanged in this minimal G1.

Required implementation gates: G0 fingerprint and predictions remain
bit-identical after G1 training; G1 has a distinct fingerprint; fixed-seed
training is deterministic; schedule, seed-domain, TD(lambda), replay eviction,
hidden-repartition teacher targets, and same-variant distinct-model benchmark
routing have focused tests.

Offline rejection gate on `probe-dev-v2`: G1 must improve either pooled
deployed agreement or regret versus G0, may worsen no deck's mean regret by
more than 0.01, and must not regress search-supervised Attack decisions. The
16 fixtures can reject, not promote.

If offline gates pass, the exact large-regression screen is:

```sh
./build/alpha-sim --benchmark --games 15 --seed 101 \
  --train-games 800 --train-seed 424242 \
  --challenger learned-actor-g1 --baseline learned-actor-g0
```

This is 600 paired games and is only allowed to reject a large regression:
stop if G1 is below 45% aggregate or below 40% on any deck. A result near
50% is not an improvement claim. Any promotion or roughly three-point claim
requires at least 2,000 paired games followed by evaluation seeds
101/202/303/404/505/606/707/808.

Implementation and verification:

- The immutable update recursively clones both critic ensemble leaves and the
  outer policy heads. Parent fingerprint and sampled predictions stay
  bit-identical; critic-only and policy-only update tests prove there is no
  aliasing.
- Collection uses the exact 24-game schedule, indexed seed domains, an online
  per-seat/per-kind cap, K/H/no-prior configuration carried explicitly into
  both generic scorers, and frozen G0 for both seats and every continuation.
- Focused transition tests prove the searched choices control the real game:
  an Include search result declares and resolves a lethal attacker, and a
  Counterspell search result taps the real Islands and puts the real
  Counterspell above the lethal Bolt.
- `make test` passes 55 engine, 4 iteration, 12 corpus, 10 metric, and 10
  runner tests plus CLI smoke under `-Werror`.
- AddressSanitizer/UndefinedBehaviorSanitizer pass all 55 engine and all 10
  probe-runner tests with `ASAN_OPTIONS=detect_leaks=0`.
- Independent audit found no blocker for exact G1. It did find that the replay
  objects currently live inside a single generation call: this truthfully
  contains one shard for G1, but does **not** yet retain G1 when fitting G2.
  Multi-generation training must fix that before claiming a three-generation
  sliding window.

Exact G0 offline baseline command:

```sh
/usr/bin/time -p ./build/alpha-sim --score-probes \
  --actor-generation 0 --probe-worlds 8 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v2-k8-h0-audit.labels.tsv
```

Runtime 45.32 seconds. G0 fingerprint:
`26cb6bc9c0633b901da5d59bbeb924e06c0b61a580eca51af3cb104ab535031c`.
The cache loaded without relabeling and hidden-repartition invariance passed.

G0 deployed metrics:

| slice | top-one | stable-pair agreement | regret | critic Brier | critic bias |
| --- | ---: | ---: | ---: | ---: | ---: |
| pooled | 93.75% | 96.30% | 0.0078 | 0.0604 | -0.0599 |
| Green | 75.00% | 50.00% | 0.0311 | 0.0096 | +0.0595 |
| Red | 100.00% | 100.00% | 0.0000 | 0.1558 | -0.3176 |
| Blue | 100.00% | 100.00% | 0.0000 | 0.0562 | +0.0833 |
| White | 100.00% | 100.00% | 0.0000 | 0.0201 | -0.0646 |

Exact G1 offline candidate command:

```sh
/usr/bin/time -p ./build/alpha-sim --score-probes \
  --actor-generation 1 --probe-worlds 8 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v2-k8-h0-audit.labels.tsv
```

Runtime 42.86 seconds. G0 training took 28.37 seconds and the actual balanced
G1 collection/update took 0.98 seconds:

- Priority: 993 searched roots, 20,768 rollout evaluations, 993 soft examples;
- Attack: 162 searched roots, 2,592 rollout evaluations, 162 soft examples;
- critic: 810 deduplicated TD(lambda) examples;
- G1 fingerprint:
  `441edc8bd49814922526716c52fd54abdb43fc5112576412516ba1068feffd32`.

G1 deployed metrics:

| slice | top-one | stable-pair agreement | regret | critic Brier | critic bias |
| --- | ---: | ---: | ---: | ---: | ---: |
| pooled | 93.75% | 96.30% | 0.0078 | 0.0668 | -0.0568 |
| Green | 75.00% | 50.00% | 0.0311 | 0.0062 | +0.0261 |
| Red | 100.00% | 100.00% | 0.0000 | 0.1774 | -0.3484 |
| Blue | 100.00% | 100.00% | 0.0000 | 0.0644 | +0.1218 |
| White | 100.00% | 100.00% | 0.0000 | 0.0191 | -0.0267 |

Decision: reject G1 as a strength candidate. Raw-head and deployed top-one,
pair agreement, and regret were numerically unchanged on every deck, so it did
not satisfy the preregistered improvement gate. The critic calibration change
was mixed: Green and White improved, while Red and Blue worsened. The 600-game
screen was therefore not run. This is the intended use of the cheap offline
rejection gate, not evidence that G1 is equal in playing strength.

### Search-distillation fit diagnostic (predeclared)

Before changing learning rate, epochs, targets, or corpus size, instrument the
already-fixed G1 update on its collected examples. Report separately for
Priority and Attack:

- frozen-parent versus teacher top-one agreement;
- candidate versus teacher top-one agreement;
- weighted soft-target cross-entropy before and after;
- number of examples whose deployed argmax changes; and
- frozen-parent versus candidate TD-target MSE for the critic.

No model/configuration change is allowed in this diagnostic. Hypothesis: the
unchanged development-probe ranking is caused either by an already-agreeing
teacher or by an update too small to distill its disagreements. A healthy
operator must reduce soft-target cross-entropy for both trained heads and
critic MSE overall. If it changes no policy argmax despite material parent
disagreement, the next candidate will increase optimization strength; if
parent agreement is already saturated, the next candidate must improve the
teacher/trajectory diversity instead. Do not run the 600-game benchmark until
one of those mechanisms is demonstrated offline.

Diagnostic result:

```sh
/usr/bin/time -p ./build/alpha-sim --score-probes \
  --actor-generation 1 --probe-worlds 8 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v2-k8-h0-audit.labels.tsv
```

The unchanged G1 reproduced its exact prior fingerprints and probe metrics:
G0
`26cb6bc9c0633b901da5d59bbeb924e06c0b61a580eca51af3cb104ab535031c`,
G1
`441edc8bd49814922526716c52fd54abdb43fc5112576412516ba1068feffd32`,
and deployed pooled top-one 93.75%, stable-pair agreement 96.30%, regret
0.0078. Runtime was 42.39 seconds.

Fit on the exact collected training examples:

| head | examples / total weight | parent top-one | candidate top-one | teacher entropy | cross-entropy | excess CE/KL | changed argmax |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Priority | 993 / 48.0 | 93.27% | 93.27% | 0.877027 | 0.896762 -> 0.896587 | 0.019735 -> 0.019560 | 0 / 0.0 weight |
| Attack | 162 / 27.0 | 86.49% | 86.49% | 0.634728 | 0.681660 -> 0.681115 | 0.046932 -> 0.046387 | 0 / 0.0 weight |

The critic did learn from its 810 TD examples: MSE improved
0.066216 -> 0.058819 and BCE improved 0.607466 -> 0.590793 (target mean
0.507883, variance 0.105007).

Decision: the policy-update operator is underfitting this corpus. Teacher
disagreement is material, especially for Attack, but two epochs at 0.001
change no exact argmax and remove only 0.9% of Priority excess CE and 1.2% of
Attack excess CE. The critic result rules out a wholly inert generation.
Keep the original G1 rejected; do not reinterpret unchanged development
probes as teacher saturation.

Independent review also reports a promising but not yet adequately powered
Value-search challenger: eight bootstrapped generations with a true
last-three replay window and search-guided late collection reached 46.7%
against Handcrafted over 600 pooled games. It is a Value critic, not this
Unified Actor, and did not satisfy this repository's probe, direct-generation,
2,000-game, seed-panel, or sanitizer gates. Treat its scaling result as a
lead to reproduce cleanly, not an accepted champion.

### Actor policy optimization-strength diagnostic (predeclared)

Hold G0, all 24 trajectories, K=8/H=0 teacher scores, soft targets, example
weights, critic update, seeds, and replay contents fixed. Change only policy
optimization from two epochs at 0.001 to 16 epochs at 0.005. Expose the
generation policy epochs/rate in the CLI and print them so this remains
reproducible; the default two/0.001 recipe and its G1 fingerprint must remain
unchanged.

Hypothesis: the moderate update will reduce excess CE/KL by at least 20% for
both Priority and Attack, improve tie-aware teacher top-one for both heads,
and change nonzero argmax weight. This is a fit diagnostic, not a strength
claim. First use a one-repetition paired benchmark only to invoke training and
inspect fit; ignore its game result. If the fit hypothesis passes, run the
fixed `probe-dev-v2` offline rejection gate. If it changes decisions in the
wrong direction or remains inert, reject it and inspect per-example gradient
weighting before escalating farther. No Handcrafted strength screen is
allowed from this diagnostic alone.

Diagnostic command:

```sh
/usr/bin/time -p ./build/alpha-sim --benchmark --games 1 --seed 101 \
  --train-games 800 --train-seed 424242 \
  --challenger learned-actor-g1 --baseline learned-actor-g0 \
  --actor-policy-epochs 16 --actor-policy-rate 0.005
```

The 40 paired games were invocation smoke only and are not interpreted as
strength evidence. The fit result was:

| head | parent top-one | candidate top-one | excess CE/KL | changed argmax |
| --- | ---: | ---: | ---: | ---: |
| Priority | 93.27% | 92.27% | 0.019735 -> 0.016754 (-15.1%) | 105 examples / 9.80% weight |
| Attack | 86.49% | 86.49% | 0.046932 -> 0.039658 (-15.5%) | 0 examples / 0.00% weight |

G0 retained its exact fingerprint
`26cb6bc9c0633b901da5d59bbeb924e06c0b61a580eca51af3cb104ab535031c`.
Collection counts, teacher entropy, parent losses, and every critic metric
were exact matches for the default G1 diagnostic. The candidate fingerprint
was
`1865ec2391c812a034578f205999a8d1c34a3140727064cf464280d1b4c28c9f`.
Runtime was 29.77 seconds.

Decision: reject 16/0.005. It failed every conjunctive fit gate: neither head
removed 20% of excess CE, Priority teacher agreement regressed by one point,
and Attack remained argmax-inert. The probe and Handcrafted screens are not
run. This is useful evidence that simply turning up the same per-example SGD
does not give a healthy actor improvement operator; lower CE can coexist with
worse decision ranking.

Rather than spend another cycle escalating the same one-generation actor
update, pivot to a clean reproduction of the independent bootstrapped
multi-generation Value lead. That path has actual (though underpowered)
gameplay evidence of monotone scaling and directly couples critic improvement
to deployed value-search decisions. Preserve the actor fit CLI as a
diagnostic; it defaults to the original two/0.001 recipe and does not promote
the rejected stronger setting.

### Immutable bootstrapped Value G8 reproduction (predeclared)

Reproduce the independent 46.7% lead as a separate canonical challenger,
without altering the accepted behavior of `learned`, `learned-value`, or the
legacy Value trainer. This is not "legacy G0 plus eight generations": start
from the same initial random-play corpus/base fit, then replace the legacy
terminal-only two-generation loop with exactly:

- 800 initial random-play games at the canonical run size;
- eight generations of `max(1, training_games / 4)` Value-mirror self-play
  games;
- for each recorded state with another trace state four positions later,
  target `0.5 * terminal + 0.5 * frozen_parent_value(trace[i + 4])`;
  use the terminal target alone for the final four trace positions;
- keep the initial random corpus as a fixed anchor and fit it together with
  the newest three immutable self-play shards;
- exploration 0.10 for zero-based generations 0-1 and 0.05 for 2-7;
- collect generations 0-3 with raw Value priority and generations 4-7 with
  the existing information-safe K=1/H=4 Value search; combat remains the
  existing public-board Value enumeration;
- clone the frozen parent recursively, then fit every independent critic leaf
  for three epochs at 0.006 using separate deterministic member seeds.

Expose this only as `learned-value-g8` and `--value-generation 8`; G0 remains
the default everywhere. Preserve all nine immutable checkpoints and report
per generation: examples, replay occupancy, whether search actually ran,
rollout evaluations, and parent/candidate fingerprints. The Actor G0 remains
the sole owner of probe labels/cache metadata; Value G8 is only an additional
scoring candidate.

Mechanism gates before any strength run:

1. exact four-state bootstrap and replay-window unit tests pass;
2. the legacy Value trainer's fixed-seed fingerprint/predictions remain
   bit-identical;
3. every older checkpoint remains bit-identical after all later generations;
4. same seed reproduces all reports/fingerprints and a different seed changes
   the final fingerprint;
5. reports show replay occupancy `1,2,3,3,3,3,3,3`, no collection rollouts in
   generations 1-4, and nonzero K=1/H=4 rollouts in generations 5-8;
6. benchmark routing retains distinct explicit G0/G8 pointers and balances
   the same 40-game tiny fixture;
7. opponent-hidden-zone repartition invariance and Actor-owned probe-cache
   reuse pass for the new Value row;
8. all existing tests, strict warnings, and sanitizers pass.

Hypothesis: search-guided bootstrapped replay produces a deterministic G8
whose fixed `probe-dev-v2` row has no deck regret regression greater than
0.01 versus legacy Value G0 and whose direct 600-game paired screen against
legacy G0 is at least 52.5% aggregate with no deck below 45%. The 16 probes
can reject but not promote. The exact offline command will use the existing
K=8/H=0 Actor-owned cache with training seed 424242. The exact direct screen
is:

```sh
./build/alpha-sim --benchmark --games 15 --seed 101 \
  --train-games 800 --train-seed 424242 \
  --challenger learned-value-g8 --baseline learned-value-g0
```

Do not interpret 600 games as proof of a roughly three-point gain. If the
screen clears, run at least 2,000 direct paired games on a separately
predeclared evaluation seed before touching Handcrafted, then the fixed
101/202/303/404/505/606/707/808 panel for any promotion claim.

Implementation verification:

- the legacy one-game Value fingerprint remains fixed at
  `f43617f58d2f03394eec79e2a9c6964339c93a00d9d0d663e157056df3b1eb11`;
- the canonical trainer retains nine immutable checkpoints, exact last-three
  replay occupancy, and actual zero/nonzero search-rollout accounting;
- same-seed reports/fingerprints reproduce and hidden-zone repartition is
  exact;
- distinct G0/G8 benchmark and probe routing have CLI/cache tests;
- `make -j4 test` passes 61 engine, 5 iteration, 12 corpus, 10 metric, and
  10 probe-runner tests plus CLI/simulator smoke;
- post-integration AddressSanitizer/UndefinedBehaviorSanitizer pass all 61
  engine and all 10 probe-runner tests.

Exact offline command:

```sh
/usr/bin/time -p ./build/alpha-sim --score-probes \
  --actor-generation 0 --value-generation 8 \
  --probe-worlds 8 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v2-k8-h0-audit.labels.tsv
```

The Actor-owned cache loaded without relabeling and all six views were
bit-identical under hidden-zone repartition. Legacy fingerprints reproduced:
Actor G0
`26cb6bc9c0633b901da5d59bbeb924e06c0b61a580eca51af3cb104ab535031c`
and Value G0
`7fa5978ef57e8ccb903380f5c3d1c48480e5e6adc8af67973082a9dfe49980a3`.

Canonical G8 took 210.23 seconds to train (251.59 seconds end to end).
The initial 75,220-example random anchor fingerprint was
`1099b6b6f62e0dcc28d093f47961823dbbb0205db268f8cd5415b58909fc1dc2`.
Replay occupancy was exactly `1,2,3,3,3,3,3,3`; generations 1-4 used zero
rollouts and generations 5-8 used respectively
29,730/30,194/36,431/41,158 rollout evaluations. Final G8 fingerprint:
`70480c43652a7532247aa320a76f70939e4863c14f5fc26a6a8af2149a1f0fde`.

Offline deployed Value comparison:

| slice | G0 top-one / pair / regret | G8 top-one / pair / regret | G0 Brier | G8 Brier |
| --- | ---: | ---: | ---: | ---: |
| pooled | 87.50% / 100.00% / 0.0018 | 75.00% / 85.19% / 0.0138 | 0.0662 | 0.1145 |
| Green | 75.00% / 100.00% / 0.0045 | 75.00% / 100.00% / 0.0045 | 0.0088 | 0.0188 |
| Red | 100.00% / 100.00% / 0.0000 | 75.00% / 88.24% / 0.0321 | 0.1352 | 0.2621 |
| Blue | 75.00% / 100.00% / 0.0029 | 75.00% / 100.00% / 0.0029 | 0.0638 | 0.0612 |
| White | 100.00% / 100.00% / 0.0000 | 75.00% / 60.00% / 0.0157 | 0.0569 | 0.1158 |

Decision: reject G8 under its preregistered offline gate and do not run the
600-game screen. Red regret regressed by 0.0321 and White by 0.0157, both
above the allowed 0.01. This does not refute the independent underpowered
46.7% gameplay result; the small Actor-referenced corpus is known to miss
some real-game advantages. It does show that the recipe loses specific
Red/White tactical rankings and materially worsens critic calibration, which
must be understood before promotion.

### Value G8 checkpoint/disagreement attribution (predeclared)

Make no training or policy change. Extend the offline report so one canonical
G8 fit scores immutable G1 through G8 alongside legacy G0, and prints each
policy's deployed selected action, reference-best set, per-probe regret, and
critic error for every nonzero-regret or G0-versus-generation disagreement.

Hypothesis: the Red and White regressions first appear at a specific
generation boundary. If they appear before search-on collection, the
bootstrapped target/replay fit is responsible; if they first appear at G5,
search-guided trajectory distribution is responsible. A White disagreement
only on the already reference-sensitive redundant-Moat fixture is weak
evidence, but a lethal Bolt, Counterspell, emergency-Moat, or mill-timing
regression is actionable. Use the same frozen cache/seed/K/H command. This is
measurement-only and cannot reopen the rejected gameplay screen by itself.

Attribution result:

```sh
/usr/bin/time -p ./build/alpha-sim --score-probes \
  --actor-generation 0 --value-generation 8 \
  --probe-worlds 8 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v2-k8-h0-audit.labels.tsv
```

The instrumentation preserved legacy Value G0 as a separate reference and
then scored the ordered G8 base and G1 through G8 checkpoints. All 14 policy
views, including hidden-zone repartition clones, were bit-identical.

| checkpoint | pooled top-one | stable-pair agreement | regret | critic Brier | Green regret | Red regret | Blue regret | White regret |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| legacy Value G0 | 87.50% | 100.00% | 0.0018 | 0.0662 | 0.0045 | 0.0000 | 0.0029 | 0.0000 |
| G8 base | 93.75% | 96.30% | 0.0010 | 0.0585 | 0.0010 | 0.0000 | 0.0029 | 0.0000 |
| G1 | 89.58% | 96.30% | 0.0064 | 0.0595 | 0.0000 | 0.0228 | 0.0029 | 0.0000 |
| G2 | 87.50% | 100.00% | 0.0018 | 0.0585 | 0.0045 | 0.0000 | 0.0029 | 0.0000 |
| G3 | **93.75%** | **100.00%** | **0.0007** | **0.0522** | **0.0000** | **0.0000** | 0.0029 | **0.0000** |
| G4 | 87.50% | 100.00% | 0.0021 | 0.0531 | 0.0055 | 0.0000 | 0.0029 | 0.0000 |
| G5 | 83.33% | 88.89% | 0.0116 | 0.0630 | 0.0010 | 0.0228 | 0.0000 | 0.0224 |
| G6 | 81.25% | 85.19% | 0.0132 | 0.0762 | 0.0045 | 0.0321 | 0.0000 | 0.0164 |
| G7 | 75.00% | 85.19% | 0.0138 | 0.0997 | 0.0045 | 0.0321 | 0.0029 | 0.0157 |
| G8 | 75.00% | 85.19% | 0.0138 | 0.1145 | 0.0045 | 0.0321 | 0.0029 | 0.0157 |

The random-anchor-only base is already better than legacy G0 on this corpus,
so the proposed legacy-G0 warm-start explanation is refuted here. The first
generation has a temporary Red regression, but G2 recovers it and G3 is the
best checkpoint on every pooled offline criterion. The decisive collapse is
exactly G4 -> G5, when collection switches from raw Value to K=1/H=4 search:

- `red.bolt-blocker.v2` changes from the reference-best Bolt target to a
  three-way exact tie, producing 0.0914 per-probe regret;
- `white.establish-millstone.v2` changes from Millstone to Pass, producing
  0.0243 regret;
- `white.avoid-redundant-moat.v2` changes from Millstone to redundant Moat,
  producing 0.0654 regret, though this fixture is reference-sensitive; and
- pooled critic Brier then worsens monotonically from 0.0630 at G5 to 0.1145
  at G8.

Decision: the attribution hypothesis supports a late search-distribution
failure, not a bad starting checkpoint. Keep final G8 rejected. Preserve G3
as the current offline checkpoint lead, and make the next training repair a
single-axis reduction of search-guided late-generation collection rather than
changing bootstrap targets, model width, or the starting parent.

The same run also published a versioned, crash-durable, bit-exact artifact
containing the report and all nine checkpoints at
`build/model-cache/value-g8-v1-t800-s424242.bin`. The fresh run took 246.80
seconds end to end, of which 204.62 seconds was G8 training. Repeating the
exact command loaded the bundle in 0.05 seconds, reproduced every fingerprint,
metric, and decision row, and completed in 42.25 seconds: an 83% end-to-end
reduction and effectively all G8 retraining removed. The format fails closed
on corruption/mismatch, syncs both file and containing directory, and the CLI
requires explicit `--refresh-value-g8-cache` to replace it.

Post-integration verification passes 63 engine, 5 iteration, 12 probe-corpus,
10 probe-metric, and 12 probe-runner tests, the cache lifecycle/CLI suite,
simulator smoke, strict warnings, and the 63-test AddressSanitizer/
UndefinedBehaviorSanitizer engine run.

### Value G3 early-stop gameplay screen (predeclared)

Expose immutable checkpoints G1 through G7 as benchmark-only Value selections
loaded from the same validated G8 bundle; this must not retrain, alter, or
reinterpret any checkpoint. `learned-value-g3` must resolve to fingerprint
`fecf1f9908b2674bc6dc1e58372f2c7029a24feab12aca2e206a54548c7f76e1`.
Legacy `learned`, `learned-value`, and `learned-value-g0` remain unchanged.

Run one frozen 600-game paired screen:

```sh
./build/alpha-sim --benchmark --games 15 --seed 101 \
  --train-games 800 --train-seed 424242 \
  --challenger learned-value-g3 --baseline learned-value-g0
```

Hypothesis: the offline-leading G3 checkpoint scores at least 52.5% aggregate
against legacy Value G0 with no deck below 45%. This is an independent
gameplay screen after selecting G3 on the development probes, not proof of a
roughly three-point gain. If it clears, predeclare and run at least 2,000
paired games on a new evaluation seed before any Handcrafted comparison. If
it fails, reject early stopping as a sufficient repair and proceed directly
to the 50/50 raw/search late-collection experiment indicated by the G5
boundary.

Screen result:

```text
Overall: 308-292-0, 51.3% (approximate 95% CI 47.3% to 55.3%)
Green:   41.3% (62-88)
Red:     40.0% (60-90)
Blue:    54.7% (82-68)
White:   69.3% (104-46)
```

The exact G3 fingerprint loaded from the bundle and the 600 games completed
in 39.83 seconds. The baseline's corresponding deck win rates were Green
40.0%, Red 38.7%, Blue 38.7%, and White 77.3%.

Decision: reject G3 early stopping as a sufficient repair. It missed the
52.5% pooled gate and both the Green and Red 45% floors. The large Blue gain
and White loss also demonstrate why the 16-position probe corpus is
rejection/diagnostic evidence rather than a strength oracle: G3 led every
pooled offline criterion, yet its gameplay effect was highly deck-specific.
Do not escalate G3 to 2,000 games or Handcrafted.

### Canonical G8 versus G3 late-search diagnostic (predeclared)

The G3 screen is new evidence that the development probes do not reliably rank
gameplay strength, while the independent challenger reported that enabling
late search collection improved its Handcrafted screen. Before choosing how
much late search Mix50 should retain, directly isolate canonical G5-G8's net
gameplay effect against its own G3 checkpoint:

```sh
./build/alpha-sim --benchmark --games 15 --seed 303 \
  --train-games 800 --train-seed 424242 \
  --challenger learned-value-g8 --baseline learned-value-g3
```

This is a measurement-only 600-game paired diagnostic using two checkpoints
from one already-frozen bundle; it cannot promote either policy. The
predeclared minimum detectable signal is eight percentage points: >=54%
supports a large late-stage gameplay benefit, <=46% supports a large
regression, and anything between is inconclusive. Per-deck slices have only
150 games and will be interpreted descriptively unless they differ by at
least ten points. Regardless of the result, keep final G8 rejected under its
original gate and continue the already-predeclared Mix50 experiment; use this
diagnostic only to interpret whether Mix50 is preserving a useful gameplay
effect that the probes miss.

Diagnostic result:

```text
Overall: 298-302-0, 49.7% (approximate 95% CI 45.7% to 53.7%)
Green:   G8 32.7% vs G3 38.0%
Red:     G8 40.7% vs G3 38.7%
Blue:    G8 43.3% vs G3 59.3%
White:   G8 82.0% vs G3 65.3%
```

The run completed in 29.37 seconds. Pooled performance is inside the
predeclared 46-54% inconclusive region: canonical late search is not a clear
net gameplay win or loss over G3. The deck slices do show two large,
opposing effects above the descriptive ten-point threshold: G8 loses 16.0
points with Blue and gains 16.7 points with White. Red improves only 2.0
points and Green loses 5.3.

Interpretation: the late search distribution does contain useful White
gameplay signal that the Actor-referenced probes call a regression, but it
simultaneously erases G3's large Blue advantage. This directly supports the
Mix50 design goal: retain some search-guided late data rather than deleting
it, while preventing it from fully replacing the raw-Value trajectory
distribution. It does not rescue or promote canonical G8.

### Value G8 Late-Mix50 collection repair (predeclared)

Change one root cause isolated by the checkpoint attribution: for G5 through
G8, replace all-search collection with an exact 50/50 mixture by games. Keep
the base, G1-G4, frozen-parent bootstrap targets, anchor, last-three replay,
exploration, model shape, optimizer, training seeds, deck-selection sequence,
and terminal discount unchanged.

At the canonical 200 games per generation, assign each consecutive pair
without consuming RNG:

- even zero-based game index: both seats use raw Value, depth 0/rollouts 0;
- odd game index: both seats use information-safe K=1/H=4 Value search.

Require an even generation game count rather than silently approximating the
mixture. Report raw/search game counts and raw/search example counts
separately; the mixture is exactly 50/50 by games, not necessarily by
examples. Search scores choose behavior only; targets remain the same
four-record frozen-parent bootstrap.

This is a distinct challenger and artifact recipe:
`learned-value-mix50-g8` and
`build/model-cache/value-g8-mix50-v1-t800-s424242.bin`. The canonical G8
entry point, recipe ID, cache path/bytes, fingerprints, and CLI behavior must
remain bit-identical. The Mix50 cache must reject a canonical artifact and
vice versa. Probe labels remain owned by frozen Actor G0.

Mechanism and offline rejection gates:

1. canonical small-run and 800-game base/G1-G4 fingerprints remain exact;
2. Mix50 base/G1-G4 equal canonical and divergence first occurs at G5;
3. every G5-G8 report has 100 raw and 100 search games, with nonzero search
   rollouts and internally consistent example totals;
4. same-seed reports/fingerprints reproduce, a different seed changes G8,
   all checkpoints remain immutable, and hidden-zone repartition passes;
5. at G5, Red regret is at most 0.0114 and White regret at most 0.0112,
   halving the canonical G5 regressions;
6. final per-deck regret is no more than legacy G0 + 0.01: Green <= 0.0145,
   Red <= 0.0100, Blue <= 0.0129, White <= 0.0100;
7. final pooled regret is <= 0.0110 and critic Brier <= 0.0916, each at least
   20% better than canonical G8; and
8. strict tests, artifact corruption/mismatch checks, Actor-cache reuse, and
   hidden-repartition invariance pass.

The development probes can reject but cannot promote Mix50. Only if every
offline gate passes, run this new frozen 600-game screen:

```sh
./build/alpha-sim --benchmark --games 15 --seed 202 \
  --train-games 800 --train-seed 424242 \
  --challenger learned-value-mix50-g8 --baseline learned-value-g0
```

Require at least 52.5% aggregate and every deck at least 45%. A pass still
requires a separately predeclared 2,000-game evaluation on another seed
before any Handcrafted comparison.

### Value G8 Late-Mix50 result: rejected offline

The implementation passed its strict mechanism checks before the canonical
run:

- the canonical T800 artifact remained 10,424,027 bytes with SHA-256
  `d2883a661609cc0115ae209d0df8381a53791fae8b2fac21993230a6b49ab110`;
- canonical and Mix50 artifacts have distinct magic, recipe, and cache
  validation and reject cross-family loads;
- base and G1-G4 remain byte/fingerprint-identical to canonical, and Mix50
  first diverges at G5;
- same-seed/different-seed, immutable-checkpoint, exact even/odd assignment,
  accounting, corruption/refresh, Actor-owned probe-cache reuse, and
  hidden-repartition tests pass;
- the current strict suites pass 66 engine, 6 learned-iteration, 12 probe
  corpus, 10 probe-metric, and 12 probe-runner tests plus the full CLI
  lifecycle; and
- ASan/UBSan pass all 66 engine and 12 probe-runner tests with no diagnostic.

The exact preregistered rejection-only run was:

```sh
/usr/bin/time -p ./build/alpha-sim --score-probes \
  --actor-generation 0 --value-recipe mix50 --value-generation 8 \
  --probe-worlds 8 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/probe-dev-v2-k8-h0-audit.labels.tsv
```

Training took 129.04 seconds and the full command took 171.35 seconds. It
generated the versioned 10 MiB artifact
`build/model-cache/value-g8-mix50-v1-t800-s424242.bin`, SHA-256
`2bc9bae74059323686665356496b4ec7884e493baabb7a34778abf28dbe1d44e`,
with final model fingerprint
`302b48c119c746a920986ca932bfa18741ffdc2a283f71b9429872b67ee3a559`.
A separate frozen-artifact load reproduced the report/fingerprint in 0.05
seconds. The Actor G0, legacy Value G0, base, and G1-G4 fingerprints all
reproduced exactly. The existing Actor-owned labels loaded without rewrite,
and all 14 scored checkpoint/policy views were bit-identical under hidden
repartition.

Late collection accounting was exactly the declared 50/50 split by games:

| generation | raw games/examples | search games/examples | search rollouts |
| --- | ---: | ---: | ---: |
| G5 | 100 / 8,520 | 100 / 7,894 | 14,615 |
| G6 | 100 / 9,240 | 100 / 7,854 | 15,449 |
| G7 | 100 / 10,324 | 100 / 8,590 | 18,320 |
| G8 | 100 / 12,450 | 100 / 8,558 | 20,967 |

The offline metrics nevertheless failed:

| checkpoint | pooled top-one | stable-pair agreement | regret | Brier | Green / Red / Blue / White regret |
| --- | ---: | ---: | ---: | ---: | ---: |
| canonical/Mix G4 | 87.50% | 100.00% | 0.0021 | 0.0531 | 0.0055 / 0 / 0.0029 / 0 |
| Mix G5 | 75.00% | 85.19% | 0.0146 | 0.0621 | 0.0010 / 0.0321 / 0.0029 / 0.0224 |
| Mix G6 | 75.00% | 85.19% | 0.0146 | 0.0684 | 0.0010 / 0.0321 / 0.0029 / 0.0224 |
| Mix G7 | 75.00% | 92.59% | 0.0114 | 0.0816 | 0.0045 / 0.0321 / 0.0029 / 0.0061 |
| Mix G8 | 68.75% | 81.48% | 0.0203 | 0.0725 | 0.0045 / 0.0321 / 0.0283 / 0.0164 |

Gate decisions:

1. canonical preservation: pass;
2. exact G1-G4 equality and first divergence at G5: pass;
3. exact 100/100 late game split and accounting: pass;
4. determinism, immutability, and hidden-information invariance: pass;
5. G5 Red <= 0.0114 and White <= 0.0112: **fail** at 0.0321 and
   0.0224;
6. final per-deck limits: Green passes, **Red, Blue, and White fail**;
7. final regret <= 0.0110: **fail** at 0.0203; Brier <= 0.0916 passes at
   0.0725; and
8. strict/cache/hidden verification: pass.

Decision: reject Late-Mix50 and do not run its 600-game screen. Halving the
searched trajectories did improve final calibration relative to canonical
G8 (Brier 0.0725 versus 0.1145), but it made final action regret worse
(0.0203 versus 0.0138) and did not prevent the sharp G4-to-G5 Red/White
collapse. Therefore the evidence no longer supports search-trajectory share
as the isolated root cause.

Next experiment: isolate whether *any* fifth fitted update causes the
collapse. Continue the exact canonical G4 checkpoint with an all-raw G5
collection while keeping the anchor, last-three replay, exploration,
bootstrap targets, optimizer, seed/deck sequence, and 200-game corpus fixed.
Score only that diagnostic G5 first. If raw G5 remains bad, collection search
is exonerated and the next causal test is replay-window eviction/update
stability; if raw G5 retains G4's action quality, then the response to search
share is nonlinear and only then is a smaller searched fraction justified.
The exact artifact/CLI recipe and numerical gate will be preregistered before
implementation or execution.

## 2026-07-25: Old School Magic scope expansion (declared before results)

The user explicitly broadened the project from Alpha-only to **Old School
Magic**, so Arabian Nights cards such as Flying Men are now in scope. This is
an intentional environment/model-schema boundary, not a tuning result. All
Alpha-era experiments above remain historical evidence at commit `1f02b63`
and must not be rewritten.

The first new deck is a 40-card blue-red aggro curve:

- 10 Mountain;
- 8 Island;
- 4 Flying Men;
- 4 Ironclaw Orcs;
- 4 Gray Ogre;
- 3 Hill Giant;
- 4 Lightning Bolt; and
- 3 Disintegrate.

The user also requested Giant Growth in Green. Its superseding 40-card list is
18 Forest, 9 Grizzly Bears, 8 Ironroot Treefolk, 4 Giant Growth, and 1
Tsunami. Giant Growth is `G`, instant, targets a creature, and gives +3/+3
until cleanup. The bonus is public, uses the stack, is counterable, changes
combat/lethal-damage calculations, and disappears with marked damage during
cleanup.

This gives the learner repeated one-, two-, three-, and four-mana development
choices plus a variable-X mana sink. It also adds real flying/block legality,
colored-land sequencing, instant versus sorcery timing, damage targeting, and
stack/counterplay without introducing a Handcrafted teacher.

Rules contract before implementation:

- Flying Men is `U`, 1/1, flying.
- Ironclaw Orcs is `1R`, 2/2, and cannot block a creature with power 2 or
  greater.
- Gray Ogre is `2R`, 2/2.
- Hill Giant is `3R`, 3/3.
- Disintegrate is `XR`, sorcery, targets a creature or player, and deals X
  damage. A creature it damages cannot regenerate that turn and is exiled if
  it dies that turn. Regeneration has no implementation yet, so the
  no-regeneration clause is presently vacuous; exile-on-death is observable
  and must be implemented.
- Flying attackers can be blocked only by flying creatures in the current
  card pool. Moat continues to permit flying attackers.

Engineering gates:

1. every new card has an exact definition/cost/stat test;
2. Disintegrate enumerates every affordable X/target legal action, is
   sorcery-speed, uses the stack, is counterable, spends exactly `X + R`,
   damages players/creatures, and exiles a creature that dies later in the
   same turn;
3. flying and Ironclaw blocking restrictions are enforced by legal bot
   choices, sampled search candidates, and final combat validation;
4. determinization/card conservation include the public exile zone;
5. the learned observation includes own/opponent exile and all new public
   card identities while still excluding opponent hand identities;
6. the RU deck is exactly 40 cards with the declared counts and completes
   seeded random and learned games;
7. strict tests, CLI integration, ASan, and UBSan pass.

Adding card identities and a public zone changes the learned feature
dimensions. Therefore old model artifacts must never be silently interpreted
under the new schema. Bump the artifact/cache schema/path (or provide an
explicit, tested migration); fail closed on old dimensions. No pre-expansion
fingerprint is expected to remain identical in the new binary.

The user subsequently made the transition policy explicit: there is no
requirement to preserve old executable aliases, card IDs, namespaces,
fingerprints, label caches, or deterministic seed streams. Prefer the simpler
clean Old School representation, invalidate prior artifacts, and compare new
models only within the new environment.

The user then promoted RU Aggro from an additive diagnostic to the fifth full
metagame deck. Superseding the earlier diagnostic boundary above, all new
Learned training schedules, paired benchmarks, mixed-field lift verdicts,
stability panels, and deck evolution must be balanced across Green, Red, Blue,
White, and RU Aggro. “Learned is king” now means it passes on all five decks.
Historical four-deck measurements remain notebook history but cannot establish
strength in the new environment.

Before any comparative five-deck output, freeze the Handcrafted Policy's new
card heuristics so RU is not evaluated against an accidentally inert baseline:

- Flying Men: 350;
- Ironclaw Orcs and Gray Ogre: 400 each;
- Hill Giant: 550; and
- Disintegrate: 900 when another spell evaluates it as a target.
- Giant Growth: 650 when another spell evaluates it as a target.

For casting Disintegrate, Handcrafted must reject its own player and own
creatures; score lethal damage to the opponent at 10,000; otherwise score
opponent-player damage as `700 + 150*X + 10*(20-life)`; score lethal damage to
an opposing creature as `2,000 + target-card-value`; and score nonlethal
creature damage as `300 + 100*X`. These are fixed integration baselines, not
tuned evidence. Any later change is a bot experiment and must be separately
declared.

Handcrafted casts Giant Growth only on its own creatures. It scores a response
that prevents otherwise-lethal pending damage at 9,000, a pump that makes the
active player's available attack power lethal at 9,500, and other legal
targets as `1,200 + 100*effective-power + 200*marked-damage`. Opposing
creatures score -10,000. These rules are likewise frozen integration
baselines, not strength evidence.

Do not make a bot-strength claim from the engine integration. After all rules
and schema gates pass, preregister a frozen five-deck
generation-to-generation benchmark before running it.

### Five-deck engine, balance, and Handcrafted integration check

Declared before the working tree's local verification runs. This is an
engine/deck-policy integration check, not a Learned-strength experiment and
cannot promote a Learned model.

Hypotheses and gates:

1. The strict build, all 121 current unit/integration tests, CLI lifecycle,
   and ASan/UBSan checks pass with the Old School schema.
2. A 30,000-game-per-matchup Random round robin at seed `424242` completes
   all ten pairings. "Somewhat balanced" means each deck's decisive-game win
   share in every pairing is between 40% and 60%; draws are reported
   separately. If a pairing misses, adjust only deck counts, preserve all
   user-requested cards and 40-card deck sizes, then validate a candidate on
   the separate seed `707`.
3. Handcrafted beats two-rollout Monte Carlo under the fully balanced
   five-deck paired harness at seed `424242`: aggregate Wilson 95% lower
   bound above 50%, and strictly more wins for Handcrafted on each of Green,
   Red, Blue, White, and RU Aggro. Its exact Giant Growth/Disintegrate scores
   remain the previously frozen integration rules above.
4. The seeded default mixed run prints every one of the ten matchups, full
   statistics for all five decks including RU Aggro and Green's four Giant
   Growths, and an explicit five-deck Learned lift gate. This output is a
   descriptive single-seed report, never a Learned-is-king verdict.

Planned commands:

```sh
make test
./build/old-school-sim --games 30000 --seed 424242 --bots random
./build/old-school-sim --benchmark --games 5 --seed 424242 \
  --challenger handcrafted --baseline monte-carlo --rollouts 2
make run
```

The 09:47 independent review requested the all-raw G5 causal experiment before
the feature/schema migration. The user explicitly superseded that sequence by
requesting a clean Old School break with no compatibility requirement, so the
old-schema raw-G5 run is intentionally not being attempted in this tree. Its
unrun declaration remains historical; any analogous experiment must be
preregistered against a fresh five-deck artifact.

### Handcrafted RU/Giant Growth policy repair

Declared after a read-only audit and before changing or comparatively running
Handcrafted. The frozen card-value constants and high-level Giant Growth /
Disintegrate formulas above remain unchanged; this repair removes four
unambiguously wasteful edge cases:

- treat an attacker with no legal blockers as favorable, so Flying Men attacks
  into ground-only boards;
- score Disintegrate with `X=0` below Pass because regeneration is not
  implemented and it has no effect in this engine;
- pass the current priority phase into Handcrafted scoring, hold Giant Growth
  outside the active player's Begin Combat unless it saves a creature from a
  pending Bolt/Disintegrate, and require the lethal-pump target itself to be
  untapped, nonsick, and Moat-legal;
- break Mountain/Island land ties with a generic "newly castable cards plus
  colored demand in hand" score so RU develops its curve intentionally.

The Giant Growth fallback remains a precombat pump because the current MVP
does not open priority after attacker or blocker declaration. That limitation
will be stated directly rather than claiming full post-block combat-trick
timing. Adding those windows requires retaining declared combat state through
information-set search and is a separate rules milestone.

Required regressions: Flying Men attacks through a Bear-only defense in both
the deployed and diagnostic Handcrafted paths; X=0 loses to Pass; Growth saves
a Bolt target, pumps only a real lethal attacker, and is otherwise held until
own Begin Combat; the land scorer chooses a color that unlocks a spell. The
already-declared five-deck Handcrafted-versus-Monte-Carlo run is the integration
gate; no numerical tuning will be performed against that result.

The predeclared Random balance run may expose a policy-dependent conflict:
uniform Random frequently aims burn at itself and uniformly samples every
Disintegrate X/target combination, so optimizing only that matrix can reward
removing the tactical cards that make RU useful for learning. If the Random
40–60 gate fails, measure the unchanged lists in a 30,000-game-per-matchup
Handcrafted round robin at the same seed before editing counts:

```sh
./build/old-school-sim --games 30000 --seed 424242 --bots handcrafted
```

Prefer a count-only candidate that improves the worst pairing under both
policies while retaining at least three Flying Men, three Lightning Bolts,
two Disintegrates, and a visible one-through-four/X curve. Do not accept a
nominally balanced Random list that collapses the requested tactical content
to singleton cards. Validate any count change at Random seed `707` and with an
unchanged Handcrafted policy; otherwise keep the richer list and report the
policy-dependent imbalance directly.

Count-candidate C1 was selected by a bounded 1,800-list Random development
search (600 games against each incumbent deck, seed family rooted at
`424242`) under those content floors. Its RU list is 13 Mountain, 4 Island,
3 Flying Men, 5 Ironclaw Orcs, 2 Gray Ogre, 8 Hill Giant, 3 Lightning Bolt,
and 2 Disintegrate. The small development estimate for RU against
Green/Red/Blue/White was 34.0%/50.3%/34.7%/59.7%, improving the original
17.8%/43.0%/26.0%/64.6% pattern but still missing the 40% floor against
Green and Blue.

C1 therefore also makes two deliberately small incumbent power reductions:
Green becomes 20 Forest, 8 Bears, 7 Treefolk, 4 Giant Growth, 1 Tsunami;
Blue becomes 20 Island, 12 Counterspell, 8 Water Elemental. Red and White
remain unchanged. Hypothesis: on the untouched Random validation seed `707`,
all ten pairings fall inside the original 40–60 gate. Reject C1 if any misses;
do not adjust it against seed `707`.

If C1 fails, a separate C2 may operationalize the user's deliberately softer
"somewhat balanced" request without pretending the 40–60 gate passed. C2
restores Green and Blue to their pre-C1 Old School counts and retains only
C1's richer RU count change. The bounded constrained search indicates that
the requested minimum tactical content cannot reach 40% against both Green
and Blue; lists that do so remove those cards to singletons.

C2's separately predeclared descriptive gate is therefore every Random
pairing between 30% and 70% on development seed `202`, followed by the same
gate on untouched seed `303`. This does not replace or pass the stricter
40–60 experiment: C1 remains rejected if it misses. C2 is accepted only as a
material "somewhat balanced while still strategically rich" improvement if
both new seeds pass, RU's worst pairing is at least ten points better than
the original 17.8% floor, and the Handcrafted five-deck integration gate
still passes without further heuristic changes.

### Five-deck integration results

The initial exact Random balance command completed 300,000 games in 2.53
seconds:

```sh
./build/old-school-sim --games 30000 --seed 424242 --bots random
```

The original RU list scored 17.8% against Green, 43.0% against Red, 26.0%
against Blue, and 64.6% against White. Green/Red/Blue/White remained in the
44.5–57.0% range. Decision: the preregistered 40–60 hypothesis failed badly
for RU. A bounded 1,800-list search confirmed that meeting it with RU counts
alone rewards singleton Flying Men/Bolt/Disintegrate lists; those were
rejected as contrary to the requested strategically rich environment.

The unchanged-deck Handcrafted diagnostic also demonstrated that "deck
balance" is policy-dependent:

```sh
./build/old-school-sim --games 30000 --seed 424242 --bots handcrafted
```

Its ten pairing rates ranged from Green's 1.3% against White to White's 98.7%;
the original RU list was 34.1%/42.7%/18.6%/83.7% against
Green/Red/Blue/White. This was measurement only. Handcrafted exploits hard
locks and Counterspell far more consistently than Random, so no count-only
five-deck list can be near 50% under both policies without removing the
strategic distinctions the learner is meant to face.

C1 was then validated exactly as declared:

```sh
./build/old-school-sim --games 30000 --seed 707 --bots random
```

Four pairings missed 40–60: Green/Blue was 38.4%/61.6%, Green/RU was
67.0%/33.0%, Blue/RU was 69.0%/31.0%, and White/RU was 37.9%/62.1%.
Decision: reject C1 under its original strict gate; do not describe it as a
40–60-balanced environment.

C2 restored the proven Green/Blue counts and kept only the richer RU count
repair. Its two separately declared 300,000-game runs were:

```sh
./build/old-school-sim --games 30000 --seed 202 --bots random
./build/old-school-sim --games 30000 --seed 303 --bots random
```

- Seed 202: all ten pairings were 31.6–68.4%.
- Untouched seed 303: all ten pairings were 31.8–68.2%.
- RU was 31.6%/49.6%/34.1%/61.4% against
  Green/Red/Blue/White at seed 202 and
  31.8%/49.4%/34.0%/61.8% at seed 303.

Decision: accept C2 only for the explicitly softer "somewhat balanced while
strategically rich" requirement. It improves RU's worst Random matchup by
14 points while retaining three Flying Men, three Bolts, two Disintegrates,
and a one/two/three/four/X curve. The strict 40–60 experiment remains a
recorded failure. A deterministic 30,000-game seed-303 regression now enforces
the declared 30–70 band.

The Handcrafted repair regressions pass. They cover no-legal-blocker Flying
Men attacks through a Bear board, no-effect `X=0` Disintegrate losing to Pass,
Bolt-saving Giant Growth, lethal-pump target eligibility, holding Growth
outside own Begin Combat, and colored land sequencing. Final integration:

```sh
./build/old-school-sim --benchmark --games 5 --seed 424242 \
  --challenger handcrafted --baseline monte-carlo --rollouts 2
```

Handcrafted won 253–47 (84.3%, 95% interval 79.8–88.0%) and passed every
challenger-deck slice: Green 50–10, Red 53–7, Blue 51–9, White 54–6, and
RU Aggro 45–15. This confirms the requested new-card baseline works; it is
not a Learned-strength result.

Final seeded user-facing report:

```sh
make run
```

`make run` now fixes evaluation seed `4242`, ran 1,000 mixed-field games in
22.42 seconds, printed all ten matchups and all five full deck/stat rows, and
reported the all-five lift gate. Learned was 66.2% overall versus
Handcrafted's 70.0%. Per-deck lift passed White and Red, but failed RU,
Green, and Blue: **2/5, overall gate FAIL**. This is the correct current
verdict; no Learned-is-king claim is made. RU still lacks held-out probes, and
Giant Growth tactical probes are also owed before the next learning change.

Strict verification after the final source/deck state:

- `make test`: 78 engine tests, 6 learned-iteration tests, 14 probe tests over
  16 fixtures, 11 probe-metric tests, 13 probe-runner tests, CLI lifecycle,
  and representative simulation all pass (122 C++ tests total).
- strict C++20 build is clean under
  `-Wall -Wextra -Wpedantic -Werror`.
- Apple clang 17 ASan/UBSan verification compiled the engine and probe-runner
  suites at `-O1 -g` with the same strict warnings plus
  `-fsanitize=address,undefined -fno-omit-frame-pointer`. With
  `ASAN_OPTIONS=halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1`, the
  engine passed 78/78 and the probe runner passed 13/13 with zero sanitizer
  findings. The binaries lived in a dedicated temporary directory that was
  removed after the run.

The newest independent review visible before recording these conclusions is
still timestamped 10:12. Its 45–55 regression-guard and 3/5 mixed-lift
statements describe a stale pre-repair count set and are not supported by the
current exact runs. The current source intentionally carries the honest 30–70
guard and 2/5 lift result above. Its separate finding that a 16-generation,
K=8 challenger crossed 50% in the old four-deck environment is promising, but
must be ported and revalidated across all five decks before it can affect the
current verdict.

## Five-deck probe-dev-v3 hard cut

Declared against frozen Old School environment commit `c64b80c`, after reading
the 2026-07-25 10:45 independent review and before changing probe code or
generating any new reference labels. This is measurement infrastructure, not a
Learned-strength experiment.

Hypothesis: a 20-position corpus with exactly four root decisions for each of
Green, Red, Blue, White, and RU Aggro can cover the new instant/X-spell/curve
decisions without continuation healing, while retaining exact legality,
physical-card conservation, and hidden-repartition invariance. The corpus will
make a hard cache/schema cut; no v2 label is reusable and no old result needs
compatibility.

The v3 composition is fixed before implementation:

- Green keeps the root-irreversible second-main Bear development fixture and
  replaces the other three positions with (1) Giant Growth responding to Bolt
  on a Bear, (2) Begin Combat Growth target choice between a lethal eligible
  attacker and a summoning-sick Bear, and (3) Second Main hold-versus-waste
  Growth.
- Red keeps its four tactical fixtures, but the blocker-clearing fixture moves
  to Begin Combat and the damaged-Water-Elemental fixture moves to the
  opponent's final priority before cleanup so Pass cannot heal later.
- Blue and White keep their four v2 positions with v3 identities.
- RU adds (1) irreversible Mountain/Island sequencing that can unlock Flying
  Men, (2) Ironclaw Orcs versus Gray Ogre blocker development, (3) Flying Men
  attacking through Moat, and (4) every affordable player-targeted
  Disintegrate size from X=0 through X=3.

Candidate descriptors remain factual and carry no preferred-action labels.
Reference labels remain Actor-mirror/common-world samples; Handcrafted remains
diagnostic-only and cannot enter labels, training, cache identity, or
acceptance.

Predeclared engineering gates:

1. exactly 20 unique fixture IDs/categories and exactly four root probes for
   each of all five decks;
2. exact original-deck card conservation, complete legal candidate sets,
   reachable public states, and fixed-seed hidden-clone determinization
   invariance for every fixture;
3. trace tests establish the rules consequences only: Growth saves the Bolt
   target, the eligible attacker alone can receive the lethal push, holding
   Growth retains it while wasting it loses the cleanup bonus, Island alone
   unlocks Flying Men, Gray Ogre but not Ironclaw Orcs can block a Bear,
   Flying Men attacks through Moat, and X=3 opponent-targeted Disintegrate is
   lethal while X=4 is unaffordable;
4. every probe metric/report array covers `kDeckCount`, including RU, and a
   legacy v2 cache fails closed under the new v3 schema/corpus identity;
5. strict build, full tests, and ASan/UBSan pass before v3 is used to compare
   Learned checkpoints.

No offline policy score will be used to tune these fixtures. After the gates
pass, generate one frozen v3 reference cache and score the existing G0–G8
ladder once to establish the pre-G16 diagnostic baseline.

### Manual interactive burn-sequencing observation

Recorded after reading the 2026-07-25 11:37 independent review. This is a
qualitative user observation, not a benchmark result and not evidence for a
card-specific Learned rule.

During manual interactive play, the user observed Learned cast Lightning Bolt
at the player's face instead of retaining it for a future creature. The exact
game seed and decision state were not recorded in the notebook, so this
particular instance is not yet a reproducible fixture. It suggests a
falsifiable failure mode: short-horizon value/search may over-rank immediate
player damage relative to the delayed option value of holding removal. The
reviewer's separately measured five-deck G16 transfer result makes RU Aggro
the weakest challenger slice at 31.9%, which raises the priority of this clue
but does not prove the cause.

The user also observed Learned cast Disintegrate with X=0. That action is
legally correct but spends red mana and has no game effect in this engine, so
it is a sharper generic action-ranking failure than the context-dependent
Bolt observation. The v3 RU X-sizing fixture already enumerates every legal
opponent-targeted X from zero through three and can measure whether deeper
search and later generations rank the no-effect branch below Pass without
adding card-specific policy knowledge.

Next research step: harvest real Red and RU priority states where Learned's
top action is Bolt-to-player and legal alternatives include Bolt-to-creature
or Pass/hold, plus RU states where it selects X=0 Disintegrate. Preserve the
state and hidden-information-safe determinization, label every legal action
with the deep common-world reference, and report the face/creature/hold and
X-sizing rankings and regret. Do not encode a Lightning-Bolt- or
Disintegrate-specific preference in Learned.

### Five-deck probe-dev-v3 engineering result

Recorded after reading the 2026-07-25 11:51 independent review. The review
reiterates that this 20-fixture development corpus may reject a candidate but
cannot promote one; that limitation is accepted and unchanged.

The hard-cut v3 engineering hypothesis passed:

- the corpus contains exactly 20 unique fixtures and exactly four for each of
  Green, Red, Blue, White, and RU Aggro;
- all candidate sets are complete and legal, exact original-deck card
  conservation holds, hidden repartitioning is invariant, and the public
  states have sufficient visible mana/history support;
- trace tests cover Giant Growth save/push/hold, the retimed irreversible Red
  and White roots, RU colored-land development, Ironclaw blocking, Flying Men
  through Moat, and Disintegrate X sizing;
- zero-pass healing regressions explicitly close the affected Red and White
  priority windows;
- metric/report arrays cover all five decks, the v3 cache/schema names are a
  clean break, and a v2 cache is rejected.

Exact verification on the final integrated source:

```sh
make test-probes
make test
```

Results: 18 probe-corpus tests across 20 fixtures, 11 probe-metric tests, 13
probe-runner tests, 83 engine/bot tests, 6 learned-iteration tests, the full
CLI lifecycle suite, and a representative simulation all pass under strict
C++20 `-Wall -Wextra -Wpedantic -Werror`. The integrated source also compiled
under AddressSanitizer and UndefinedBehaviorSanitizer; the engine suite,
probe-runner suite, and interactive normal/active-stack smokes reported zero
findings.

Decision: accept probe-dev-v3 as development instrumentation. No v3 reference
cache or policy score has been generated yet, so this is not a Learned
strength result. The next measurement remains one frozen v3 label cache,
G0-G8 plus Handcrafted diagnostic scoring, and then the five-deck G16 artifact
comparison. A separately harvested real-game validation corpus is still
required for any promotion claim.

### Frozen probe-dev-v3 K=8/H=0 baseline and X=0 audit

Recorded after rereading the newest independent review, timestamped 2026-07-25
11:51 PDT. This executes the already-declared first v3 measurement; it does
not change the probe corpus or promote a model.

Exact command:

```sh
./build/old-school-sim --score-probes \
  --actor-generation 0 --value-generation 8 \
  --probe-worlds 8 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/old-school-probe-dev-v3-k8-h0-audit.labels.tsv \
  --refresh-probe-cache
```

The frozen Actor G0 fingerprint was
`41a1be597a46b49cefcfe553f6b8758016a9fd7c3b385cb3e3820c5b6f811d9c`;
Value G0 was
`39e8d950a0ffcfa84180594432f197289cf5b41567dd57ae504166658e9b7646`;
and canonical Value G8 was
`ca85d00831b1a5c7da9de3a124f4d49154c380600c3c3edde0a65f11bc89284e`.
The generated v3 label cache SHA-256 was
`f04055d76272beced1a12371960627cf7acdc3fb003c4cf784a60c9d3e7b660c`.
All 14 policy views were bit-identical after hidden-zone repartitioning.

The compact Value ladder was not monotone on this small development corpus:

- Value G0: 100% pooled top-1, regret 0.0006, critic Brier 0.0752;
- G8 random-anchor base: 80% top-1, regret 0.0192, Brier 0.0527;
- G1: 85%, 0.0089, 0.0512;
- G2: 95%, 0.0027, 0.0572;
- G4 and G5: 100%, 0.0006, 0.0587/0.0624;
- G6: 90%, 0.0061, 0.0662;
- G8: 95%, 0.0013, 0.0748.

Handcrafted's diagnostic-only agreement was 95% top-1 with 0.0032 regret.
Actor G0's deployed policy was 100% top-1 with 0.0029 regret, while its raw
head was 80% with 0.0238 regret. There were six low-margin best-action pairs
and one reference-sensitive White pair, so the result remains a rejection
diagnostic only. The worsening late-generation critic calibration and
non-monotone probe scores are warnings, not causal attributions.

The RU lethal-X fixture does **not** close the user's observed X=0 failure.
Because the opponent is at three life, K=2 search reliably finds X=3 lethal
and can pass top-1 even while ranking X=0 above Pass in an ordinary nonlethal
state. A separate read-only diagnostic on the same fixture exposed the
underlying ranking:

- Value G0 trained for 100 games, raw: Pass 0.880711, X=0 opponent
  0.885661, X=3 opponent 0.880807, so raw selected X=0;
- the same model at K=2: Pass 0.870671, X=0 0.872321, X=3 0.960269;
- Value G0 trained for 800 games, raw: Pass 0.765471, X=0 0.795801,
  X=3 0.794311, again selecting X=0;
- the 800-game model at K=2: Pass 0.790917, X=0 0.801027, X=3
  0.931437.

For the 100-game model, the Pass-versus-X=0 gap under K=2 and K=8 is exactly
the bad shallow-prior gap divided by `K+1`: continuation assigns holding and
wasting the spell equal value. This supports a structural, falsifiable
explanation: deterministic parent-policy continuation can "heal" Pass by
wasting the retained card later. Merely increasing deployment rollouts cannot
teach option value.

Decision: retain X=0 as a legal Magic action and reject a Disintegrate-specific
mask. Add an independently harvested, nonlethal RU hold-versus-X=0 state to
the future validation corpus, explicitly report `Q(Pass)-Q(X=0)` with a paired
confidence interval, and test card-agnostic stochastic/recursive policy
improvement plus soft targets over every legal action. Keep the frozen
20-position v3 corpus unchanged. Interactive should use the strongest accepted
frozen checkpoint at its validated search budget once that checkpoint is
integrated; this improves observed play but does not replace the new
hold-versus-waste regression.

### Separate C16 recipe port with a true frozen G0 comparator (declared)

Declared after rereading the 2026-07-25 11:51 independent review and before
porting challenger code. The review says its branch's explicit
`learned-value-g0` pins the frozen Codex champion, but a read-only audit
falsified that statement: at train size 1 and seed 424242, main's true legacy
G0 fingerprint is
`b2eec9390d1c7edc358aa27220f9f25b1c31022627a4701e9590efa669e982ba`,
whereas the branch's explicit “G0” fingerprint is
`88528336069e681b4c4a54264a6fbdd4cd5d8613d6e6d2b8e46c578689adf817`.
The branch actually stops its new bootstrapped/sliding/search-on recipe at
generation two; it changes labels, self-play game allocation, search
collection, and replay semantics. Therefore the in-flight branch C16-vs-G0
experiment is C16 versus new-recipe G2 and cannot establish superiority over
legacy G0. This disagreement is recorded explicitly rather than editing
`REVIEW.md`.

Hypothesis: manually porting the clean card-agnostic challenger recipe as a
separate model family will preserve legacy G0 bit-for-bit, make C16 and G0
unambiguous CLI selections, and reproduce the branch's five-deck improvement
when both deploy with K=8. No Handcrafted/card-name targets or opponent hidden
cards will enter the new trainer.

Engineering gates before any strength run:

1. leave `train_learned_value_champion(T, seed)` behavior unchanged and pin a
   golden legacy fingerprint;
2. add a separately named challenger trainer with a positive explicit
   generation count, deterministic same-seed artifacts, and distinct
   fingerprints for different generation counts;
3. make model reuse/equality keys include recipe and generation, so C16 and
   explicit legacy G0 can never alias;
4. keep probe-dev-v3 labels and Value G0 continuation owned by true legacy G0,
   then add C16 only as a scoring candidate;
5. thread the validated deployment budget through benchmark, stability, and
   interactive routes without silently relabeling the default champion;
6. preserve five-deck-balanced Learned-mirror self-play, hidden-information
   isolation, strict build/tests, and ASan/UBSan.

After those gates, first score C16 and true G0 on the frozen v3 cache, then run
an equal-K=8 C16-vs-G0 paired comparison. Only if that passes the predeclared
screen should C16 be rerun against Handcrafted on the three existing
five-deck evaluation seeds, followed by a virgin-seed 2,000+ paired gate and
the fixed eight-seed panel. The current 720-game branch result does not
promote C16: RU loses 31.9% versus 39.6% and Green is tied.

### Separate C_N port and X=0 validation instrumentation result

Recorded after rereading the newest independent review, timestamped
2026-07-25 12:40 PDT. This closes engineering gates only; it is not a
strength or promotion result.

The separate challenger family is now explicit throughout training,
benchmark, stability, mixed-field, probe, and interactive routes.
`train_learned_value_champion(T, seed)` remains the legacy G0 implementation;
at `T=1`, seed `424242`, its golden fingerprint is still
`b2eec9390d1c7edc358aa27220f9f25b1c31022627a4701e9590efa669e982ba`.
The separately named C2 recipe at the same size and seed has golden
fingerprint
`88528336069e681b4c4a54264a6fbdd4cd5d8613d6e6d2b8e46c578689adf817`,
exactly reproducing the independent branch artifact and proving that C2 and
legacy G0 are not aliases. Model-family/generation keys prevent reuse across
those identities. `--learned-rollouts N` now reaches each Learned seat at the
same declared K.

Validation-v1 is a separate, non-promotable corpus containing one fixed-seed,
real-engine RU-versus-Green second-main state. Its exact information-set
fingerprint `e181051de454c79a`, turn 5, and priority-decision ordinal 10 are
golden-pinned. It has complete legal actions and explicitly compares Pass with
opponent-player Disintegrate X=0. It does **not** reproduce the user's exact
game or prove that a particular Learned checkpoint selected X=0; it is a
focused hold-versus-waste behavioral regression.

The probe report now keeps the cached Actor-owned reference estimate separate
from fresh common-world paired estimates for legacy G0 and every requested
Value candidate. Candidate K is controlled by `--learned-rollouts`, is excluded
from the reference-cache identity, and must be at least two so its paired
standard error is defined. This fixes an audit defect in which the previous
candidate-pair display could repeat the cached Actor label instead of measuring
the requested C_N model.

A tiny `T=1`, K=2/3 instrumentation smoke confirmed that the diagnostic catches
the failure: the Actor reference estimated `Q(Pass)-Q(X=0)=-0.0224` (paired
SE `0.0080`, 95% interval `[-0.0381,-0.0066]`), while legacy Value G0 estimated
approximately `-0.0026` at K=2 and `-0.0019` at K=3. These deliberately tiny
models are not strength evidence. They show that Actor top-1 is not a
correctness oracle here and that the explicit per-Value pair sign is the useful
regression.

Strict verification on the integrated source passed 85 engine/bot tests,
6 learned-iteration tests, 20 probe-corpus tests, 11 probe-metric tests, and
17 probe-runner tests. The full pre-final-patch ASan/UBSan matrix reported zero
findings, and the focused final probe-runner sanitizer passed 17/17. The
user-observed legal X=0 action remains legal; no card-specific mask, handcrafted
label, opponent hidden card, or combat score was added to Learned.

### C16 versus legacy G0 and nonlethal X=0 screen (declared)

Declared after rereading the 2026-07-25 12:40 independent review and before
training the ported C16 at `T=800`. Training seed and evaluation seed remain
independent. This is a large-regression screen, not the final Learned-is-king
gate.

Hypothesis 1: the independently reproduced C16 recipe deployed at K=8 will
improve the focused nonlethal option-value defect relative to true legacy G0.
On validation-v1, C16's fresh `Q(Pass)-Q(X=0)` point estimate must be positive
and no worse than G0's. Because the corpus has one state and only eight
candidate worlds, this can reject a behaviorally broken checkpoint but cannot
accept or promote one. Exact command:

```sh
./build/old-school-sim --score-probes \
  --probe-corpus validation-v1 \
  --probe-worlds 128 --probe-horizon 0 \
  --learned-generations 16 --learned-rollouts 8 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/old-school-probe-validation-v1-k128-h0-t800-s424242.labels.tsv \
  --refresh-probe-cache
```

Hypothesis 2: at equal K=8, C16 is a materially stronger five-deck policy than
legacy G0. The preregistered 600-paired-game screen uses evaluation seed `909`
and passes only if C16 exceeds 55% aggregate and has more wins than G0 for each
of Green, Red, Blue, White, and RU Aggro. With 120 games per deck, slice results
are large-regression guards rather than few-point estimates. Exact command:

```sh
./build/old-school-sim --benchmark --games 10 --seed 909 \
  --challenger learned-value-c16 --baseline learned-value-g0 \
  --learned-rollouts 8 \
  --train-games 800 --train-seed 424242
```

If this screen passes, the next preregistered milestone will reproduce the
three existing Handcrafted evaluation seeds and then use at least 2,000 paired
games on a virgin seed before the fixed eight-seed panel. If either screen
fails, do not tune against this one-state corpus; use the result to choose a
card-agnostic policy-improvement experiment.

### C16 versus legacy G0 and nonlethal X=0 screen (result)

Recorded after rereading the newest independent review, timestamped
2026-07-25 13:40 PDT. Both preregistered hypotheses passed their stated
screens, with an important uncertainty caveat on the one-state behavioral
result.

The validation-v1 command trained these frozen artifacts:

- Actor G0:
  `41a1be597a46b49cefcfe553f6b8758016a9fd7c3b385cb3e3820c5b6f811d9c`;
- legacy Value G0:
  `39e8d950a0ffcfa84180594432f197289cf5b41567dd57ae504166658e9b7646`;
- Value Challenger C16:
  `e4e9cc8869a9a501a68ba2c0e904acf43847564c935d032aa697562553d8c145`.

At K=8, legacy G0 estimated
`Q(Pass)-Q(X=0)=-0.0095` (paired SE `0.0042`, 95% interval
`[-0.0178,-0.0011]`). C16 estimated `+0.0006` (paired SE `0.0005`,
95% interval `[-0.0005,+0.0016]`). Thus C16 passed the preregistered point-sign
and no-worse-than-G0 screen, but the margin is statistically compatible with
zero. Decision: accept the directional correction as useful diagnostic
evidence, not as proof that X=0 is solved in natural games. The scripted
validation state still does not claim that either deployed policy selected
X=0 there.

The exact seed-909, 600-paired-game C16-versus-legacy-G0 command produced:

- aggregate C16 **334-266 (55.7%)**, Wilson 95% interval
  **51.7%-59.6%**;
- Green 56/120 versus G0 30/120;
- Red 60/120 versus G0 52/120;
- Blue 80/120 versus G0 53/120;
- White 88/120 versus G0 85/120;
- RU Aggro 50/120 versus G0 46/120.

This passes the predeclared `>55%` aggregate bar, the aggregate lower-bound
gate, and all five deck-slice guards. The independent reviewer ran the exact
same command from the same working tree and reported the exact same 334-266
total and every slice, providing a live bit-for-bit determinism replication.
Distinct fingerprints prove that the comparison did not alias the two model
families.

Decision: accept C16 as the new clean milestone challenger over legacy G0.
Do not yet make a Learned-is-king or default-policy promotion claim. Existing
independent C16-versus-Handcrafted evidence still shows RU Aggro behind at the
2,040-game seed-404 milestone, and the mixed-field all-five lift gate has not
yet been rerun at the validated K=8 deployment.

### C16 K=8 five-deck mixed-lift screen (declared)

Declared after rereading the 2026-07-25 13:40 independent review and before
rerunning the user-facing five-deck mixed field with C16. Hypothesis: replacing
legacy G0/K=2 with the frozen C16/K=8 challenger will make Learned's lift over
Random at least as large as every other policy's lift on each of Green, Red,
Blue, White, and RU Aggro in the fixed seed-4242 report.

Exact command:

```sh
./build/old-school-sim --games 100 --seed 4242 --bots mixed \
  --learned-generations 16 --learned-rollouts 8 \
  --train-games 800 --train-seed 424242
```

This 1,000-game report has only 60 games per deck/policy cell, so it is a
large-gap screen and cannot resolve a few points. Pass requires the existing
literal all-five lift gate (ties allowed); aggregate win rate alone cannot
pass it. Failure on any deck selects the next card-agnostic learning
experiment and does not authorize tuning to the cell.

### Frozen C_N artifact cache engineering result

Recorded after rereading the newest independent review, timestamped
2026-07-25 13:40 PDT. This is a runtime/integrity improvement, not a bot
strength result.

The separate Challenger family now has an immutable, versioned cache at
`build/model-cache/old-school-value-challenger-v1-cN-tT-sSEED.bin`.
Its payload identity binds the Old School engine/observation schema, exact
challenger trainer recipe, all model dimensions, training size, training seed,
generation count, and final model fingerprint. A first matching route trains
and atomically publishes the artifact; later interactive, benchmark,
learned-only, mixed, stability, and probe routes load the bit-exact model.
`--refresh-value-challenger-cache` is the only automatic regeneration path.
Corrupt, stale, mismatched, trailing-byte, or cross-family artifacts fail
closed rather than silently retraining.

A safety audit found that an early writer API could wrap an arbitrary Value
model with caller-supplied Challenger metadata. That version was rejected.
The final API uses a private-provenance artifact object constructible only by
the Challenger trainer or its validated loader, so legacy G0/G8 cannot be
relabelled C_N. Tests also prove both canonical and Mix50 G8 loaders reject
Challenger artifacts and vice versa.

Verification on the final cache source:

- strict C++20 `-Wall -Wextra -Wpedantic -Werror` engine and simulator builds;
- 86/86 engine/bot tests, including the legacy G0 and C2 golden fingerprints,
  bit-exact artifact round-trip, metadata/corruption failures, generation-key
  separation, and opaque provenance;
- the complete CLI lifecycle, including generate/load identity, deterministic
  record reuse, corrupt fail-closed behavior, atomic refresh, and every
  advertised route;
- 20/20 probe-corpus, 11/11 probe-metric, and 17/17 probe-runner tests;
- ASan/UBSan 86/86 with zero findings.

Decision: accept the cache. It does not alter model weights, rollout policy, or
benchmark results; it removes approximately five minutes of repeated C16
training from each matching follow-up command and makes frozen-model reuse
visible in CLI output.

### C16 K=8 five-deck mixed-lift screen (result)

Recorded after rereading the newest independent review. The review independently
reproduced this fixed report and identifies the same single remaining failure.

The exact preregistered command loaded C16 fingerprint
`e4e9cc8869a9a501a68ba2c0e904acf43847564c935d032aa697562553d8c145`
from the hardened artifact cache in `0.01s`, then completed 1,000 mixed-field
games. C16 won 283-117 across its 400 seat-games (**70.8%**) versus
Handcrafted's 274-126 (**68.5%**). Directly within the policy matrix, Learned
beat Handcrafted 42-38.

The per-deck lift-over-Random gate was:

- Green: Learned `+38.8 pp`, best rival Handcrafted `+31.2 pp` — PASS;
- Red: Learned `+41.2 pp`, best rival Handcrafted `+40.0 pp` — PASS;
- Blue: Learned `+48.8 pp`, best rival Handcrafted `+45.0 pp` — PASS;
- White: Learned `+55.0 pp`, best rival Handcrafted `+51.2 pp` — PASS;
- RU Aggro: Learned `+47.5 pp`, Handcrafted `+52.5 pp` — **FAIL by 5.0 pp**.

The preregistration incorrectly described 60 games per deck/policy cell; the
actual fixed `--games 100` policy rotation yields 80. This correction is
recorded here rather than rewriting the declaration. Eighty games still cannot
resolve a five-point difference reliably, but the separate 2,040-game direct
Handcrafted milestone also has RU behind, so the weakness is not dismissed as
cell noise.

Decision: reject the all-five hypothesis at **4/5**, the best lift result in
project history. Blue's old 17.5-point gap is eliminated; RU Aggro is now the
only deck between Learned and the all-five crown. Per preregistration, do not
tune to this cell or hardcode Disintegrate/Bolt preferences. The next change
must be a separately measured, card-agnostic learning/search experiment.

### Continuation-only epsilon experiment (declared)

Declared after rereading the newest independent review and before changing
search behavior. The treatment is a single predeclared value,
`epsilon=0.05`; it will not be swept or tuned against RU or validation-v1.

Hypothesis: C16's deterministic depth-zero mirror continuation is
self-confirming. After Pass retains an option, that same policy can waste the
card later, causing the Pass and waste branches to heal together. Allowing
both seats in the **inner continuation only** to choose a uniformly random
legal action with probability 0.05 will represent some futures where a held
option survives long enough to matter. The outer deployed root remains greedy.
The same frozen C16 model, hidden-information worlds, and candidate-paired
seeds are used; this adds no card names, handcrafted scores, opponent hidden
cards, combat scoring, or new reward.

Engineering gates:

1. default continuation epsilon zero is bit-identical, including legacy G0 and
   C2 golden fingerprints and fixed-seed benchmark records;
2. epsilon must be finite and in `[0,1]`;
3. only the two depth-zero Value-mirror continuation seats explore; the root
   action remains greedy and Actor/Monte-Carlo policies are unchanged;
4. common-world pairing, hidden-repartition invariance, rollout accounting,
   and fixed-seed determinism remain exact;
5. benchmark same-policy identity includes continuation epsilon, permitting a
   causal same-model/K comparison without model aliasing;
6. strict tests and ASan/UBSan pass.

The CLI treatment name is
`--value-continuation-epsilon 0.05`. First compare standard C16 and the
treatment on validation-v1 at equal candidate K=128. The existing Actor cache
must load unchanged because candidate epsilon cannot enter teacher-label cache
identity:

```sh
./build/old-school-sim --score-probes \
  --probe-corpus validation-v1 \
  --probe-worlds 128 --probe-horizon 0 \
  --learned-generations 16 --learned-rollouts 128 \
  --value-continuation-epsilon 0 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/old-school-probe-validation-v1-k128-h0-t800-s424242.labels.tsv

./build/old-school-sim --score-probes \
  --probe-corpus validation-v1 \
  --probe-worlds 128 --probe-horizon 0 \
  --learned-generations 16 --learned-rollouts 128 \
  --value-continuation-epsilon 0.05 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/old-school-probe-validation-v1-k128-h0-t800-s424242.labels.tsv
```

The behavioral treatment passes only if its C16
`Q(Pass)-Q(X=0)` lower 95% bound is above zero and its point estimate is
strictly higher than standard C16. This one state can reject but never promote.
If it passes, run the causal five-deck screen using the identical frozen model
at equal K=8:

```sh
./build/old-school-sim --benchmark --games 10 --seed 919 \
  --challenger learned-value-c16 --baseline learned-value-c16 \
  --learned-rollouts 8 --value-continuation-epsilon 0.05 \
  --train-games 800 --train-seed 424242
```

That 600-paired-game screen passes only above 52.5% aggregate with the
treatment ahead on every challenger deck. It is still a large-effect screen,
not promotion evidence. A failure rejects epsilon 0.05 without trying nearby
values; a pass permits a separately preregistered mixed-lift rerun.

### Continuation-only epsilon experiment (result)

Recorded after rereading the newest independent review, timestamped
2026-07-25 14:16 PDT. Both commands in the declaration were run exactly as
written against the same cached Actor labels and frozen C16 artifact. The
cache remained an Actor-reference cache: changing candidate continuation
epsilon neither regenerated it nor changed its identity. Hidden-zone
repartition invariance passed bit-for-bit in both runs.

At epsilon zero, C16 estimated
`Q(Pass)-Q(X=0)=-0.0011` (paired SE `0.0002`, 95% interval
`[-0.0014,-0.0008]`) from 128 common-world samples per action. The more
precise K=128 estimate therefore corrected the earlier K=8 point estimate:
standard C16 slightly but significantly preferred the nonlethal X=0 action in
this state.

At the single predeclared treatment `epsilon=0.05`, C16 estimated
`Q(Pass)-Q(X=0)=-0.0129` (paired SE `0.0053`, 95% interval
`[-0.0233,-0.0025]`). The treatment moved the point estimate `-0.0118` in
the wrong direction, and its entire interval remained below zero. Candidate
pair estimates became noisier because stochastic continuations added genuine
within-policy outcome variance; common-world seeds and rollout accounting
remained paired and deterministic.

Decision: **reject continuation epsilon 0.05**. Per preregistration, do not
try nearby epsilon values and do not run the conditional seed-919 five-deck
benchmark or a mixed-lift rerun. The falsified hypothesis is informative:
rare uniformly random priority choices do not reveal useful option value to
this frozen critic; in this fixture they make the hold branch look worse.
Keep the explicit zero-default ablation available for reproducibility, but it
is not part of the promoted C16 policy.

Engineering verification for the rejected ablation remains green: strict
C++20 `-Wall -Wextra -Wpedantic -Werror` builds, 88/88 engine/bot tests,
17/17 probe-runner tests, and the full CLI suite. The tests cover a bit-exact
zero default, finite `[0,1]` validation, root isolation, both continuation
seats, fixed-seed repeatability, hidden-repartition invariance, unchanged
world/evaluation accounting, same-model benchmark identity, challenger-only
CLI treatment, and Actor-cache exclusion. A final strict ASan/UBSan build ran
the 88 engine tests, 17 probe-runner tests, and an epsilon-0.05 validation-v1
smoke with zero findings; the smoke also preserved hidden-repartition
invariance.

### C16 Counterspell probe audit (declared)

Declared after rereading the newest independent review and in response to the
user asking whether Counterspell is tested for use and mana discipline.
Hypothesis: frozen C16 at deployed K=8 selects an Actor-reference-best action
on all four existing Blue stack fixtures:

- use a two-mana Counterspell on a five-mana Fire Elemental;
- decide whether to spend it against a one-mana Bolt threatening a five-mana
  Water Elemental;
- counter a lethal Bolt;
- in a counter war, target the opponent's Counterspell rather than its own
  Water Elemental.

Exact command:

```sh
./build/old-school-sim --score-probes \
  --probe-worlds 8 --probe-horizon 0 \
  --learned-generations 16 --learned-rollouts 8 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/old-school-probe-dev-v3-k8-h0-audit.labels.tsv
```

Pass requires C16 top-1 agreement on all four Blue fixtures and zero Blue
regret. The one-mana-Bolt pair has only a tiny cached reference margin, so
failure there is diagnostic rather than a reason to reject C16. This frozen
four-state development slice can expose a Counterspell regression; it cannot
promote a policy or establish a general mana-value threshold.

### C16 Counterspell probe audit (result)

Recorded after rereading the newest independent review, timestamped
2026-07-25 14:16 PDT. The exact command loaded the immutable v3 labels and
frozen C16 artifact. All six policy views and their hidden-zone repartition
clones were bit-identical.

The cached deep-reference action values support the intended semantics:

- countering five-mana Fire Elemental with two-mana Counterspell:
  `0.694699` versus Pass `0.616914`, delta `+0.077785`;
- countering one-mana Bolt to preserve five-mana Water Elemental:
  `0.743886` versus Pass `0.741592`, delta `+0.002294`;
- countering lethal Bolt: `0.116456` versus terminal Pass `0`;
- countering the opponent's Counterspell in the counter war: `0.670836`
  versus Pass `0.602954` and incorrectly countering the protected Water
  Elemental `0.591295`.

C16 at K=8 achieved **100% top-1 agreement across all four Blue probes** and
**100% agreement on all three statistically stable Blue action pairs**.
Therefore it selected a reference-best action in the expensive-spell,
protect-the-threat, lethal, and counter-war fixtures. Hidden-information
invariance passed.

The stricter declaration technically failed because C16's mean Blue regret
was `0.0006`, not exactly zero. The discrepancy is compatible with selecting
within a reference-best uncertainty set rather than the single maximum; the
report still counted all four top-1 choices as reference-best. Two labels are
weak diagnostics: the protect-Water margin is only `0.0023`, and lethal
Counterspell has paired SE `0.1165` at K=8 because only one sampled world
found the visible winning continuation.

Decision: accept the narrow answer that C16 uses Counterspell correctly in
these four fixtures, including the clean two-mana-for-five-mana trade and the
correct counter-war target. Do **not** claim general mana discipline or use
this result for promotion. A future mana-discipline family should hold the
information set fixed while varying incoming spell cost/public consequence
and test monotonic action preference with more common worlds.

### Markov-context alias audit (declared)

Declared after rereading the newest independent review and before changing
the Value observation or training recipe. This is a structural diagnostic,
not a bot treatment.

Hypothesis: the deployed Value critic aliases legally different decision
states because its input is only `GameState`. In particular, hold one public
stack state, root player, perspective, own hand, and complete legal action set
fixed while changing only `consecutive_passes` from zero to one. The current
critic feature vector should remain bit-identical even though choosing Pass
has a different rules transition: the zero-pass context yields priority with
the spell still on the stack, while the one-pass context resolves that spell.
A lethal visible spell makes the consequence unambiguous. The already
existing neutral policy/action encoder should distinguish the two contexts,
showing that the missing information is representable without card-specific
knowledge.

Also hold one empty-stack state fixed across First Main and Second Main. The
critic should again alias the contexts even though passing ends into different
phase transitions, while the policy/action encoder should distinguish them.

The diagnostic CLI is:

```sh
./build/old-school-sim --diagnose-value-context
```

Acceptance requires exact legality in both paired contexts, bit-identical
current critic features, different encoded context features, and exact
rules-engine evidence that the pass-count pair produces different successor
states/results. Fixed-seed determinism, hidden-information isolation, strict
tests, and sanitizer cleanliness remain mandatory. If the collision is not
demonstrated, do not build a context-aware Value treatment. If demonstrated,
the next experiment may add neutral phase/relative-priority/pass/sorcery
context and dense decision-state traces, but must be separately
preregistered before retraining.

### Markov-context alias audit (result)

Recorded after rereading the independent 2026-07-25 15:10 PDT review. The
reviewer independently ran the same diagnostic and reproduced the result.

The exact declared command completed successfully. In the lethal-stack pair,
both contexts exposed the same two legal actions and the deployed critic
features were bit-identical. The neutral policy/action encoding differed. A
Pass with zero prior passes returned `Passed`, transferred priority to player
one, left one pass and one stack object, and left the root at three life. A
Pass with one prior pass returned `StackObjectResolved`, reset priority and
the pass count to player zero and zero, emptied the stack, and left the root at
zero life. The First Main versus Second Main pair likewise had the same legal
actions and identical critic features but different neutral policy/action
features. Substituting opponent hidden cards did not change either encoding.

Decision: accept the structural diagnosis. The Value critic cannot represent
the consequence of passing even though the rules engine and card-agnostic
policy/action encoder can. This diagnostic changes no deployed behavior. A
context-aware critic schema plus dense decision traces is now licensed as a
separately preregistered treatment; this deck/card implementation does not
silently begin that learning experiment.

### Exact Blue and Red deck expansion (environment result)

This was a user-directed environment change, not a bot-tuning experiment. The
new exact 40-card Blue list is 15 Island, one each of Mox Sapphire, Sol Ring,
Ancestral Recall, Time Walk, and Braingeyser, four Flying Men, four Force
Spike, eight Counterspell, and four Air Elemental. The exact Red list is 15
Mountain, nine Lightning Bolt, seven Ironclaw Orcs, four Gray Ogre, three Hill
Giant, and two Fire Elemental.

The seven newly implemented cards use the normal mana, priority, and stack
paths. Force Spike is legal only against spell stack objects and counters at
resolution unless the target controller pays one generic mana. The normal bot
game uses the deterministic MVP choice "pay if able"; the resolver also
exposes and tests an explicit decline choice. Mox Sapphire and Sol Ring use
implicit mana abilities and phase-local floating mana. Ancestral Recall and
Braingeyser target either player and failed draws lose the game; Time Walk
queues a real extra turn; Air Elemental is a 4/4 flyer for `3UU`. The learned
observation gained neutral card planes, public mana pools, and queued extra
turns without exposing the opponent's hidden cards. Model/cache schema and
paths were advanced to v2 so old artifacts fail closed.

The exact Random-vs-Random baseline command was:

```sh
./build/old-school-sim --games 30000 --seed 303 --bots random
```

It ran 300,000 games with no draws. First-deck win rates in matchup order were
Green/Red `62.7%`, Green/Blue `56.7%`, Green/White `55.0%`, Green/RU `68.2%`,
Red/Blue `48.6%`, Red/White `55.6%`, Red/RU `42.2%`, Blue/White `90.4%`,
Blue/RU `49.5%`, and White/RU `38.2%`. The old generic 30--70 balance guard
cannot truthfully describe the exact requested lists, so it was replaced by a
deterministic full-matrix regression within one percentage point. This records
the Blue/White imbalance rather than changing the requested cards to conceal
it.

The normal user-facing command:

```sh
make run
```

used evaluation seed `4242`, training seed `424242`, 800 G0 self-play games,
K=2, and 100 games per matchup. Deck records were Green `40.0%`, Red `47.5%`,
Blue `63.0%`, White `57.2%`, and RU `42.2%`. Blue averaged 9.2 spells cast and
2.8 spells countered per game. Learned went 288--112 across its 400 seat-games
(`72.0%`) and beat Handcrafted 42--38 directly, but the literal per-deck lift
gate passed only Green, White, and RU: **3/5, FAIL**. Red and Blue were behind
Handcrafted by 3.7 and 2.5 percentage points respectively in 80-game cells.
This small report is descriptive and cannot establish a new Learned champion;
all prior fingerprints and champion claims are noncomparable after the
environment/schema change.

A separate actual-policy integration run:

```sh
./build/old-school-sim --games 100 --seed 42 --bots handcrafted
```

gave Blue 324--76 (`81.0%`) over its 400 seat-games and 3.5 countered spells
per game. Aggregate simulation statistics intentionally combine Counterspell
and Force Spike, so Force Spike itself is covered by a dedicated live-stack
fixture: Blue holds Force Spike and one untapped Island while a tapped-out
Red player has Gray Ogre on the stack. The probe trace proves Pass resolves
the Ogre, while casting Force Spike spends blue mana, two passes resolve the
top object, the Ogre is countered, both graveyards are correct, and the
counter statistic increments. A joined regression asks the deployed
Handcrafted scorer to choose from the actual legal actions and proves it
selects and executes that Force Spike line. Paid, declined, Sol Ring payment,
missing-target, counter-war, and activated-ability exclusion cases are also
covered.

Verification at this point: strict `-Werror` builds, 98/98 engine/bot tests,
6/6 learned-iteration tests, 21/21 probe-corpus tests, 11/11 metric tests,
17/17 probe-runner tests, and the full CLI lifecycle all pass. The complete
98-test engine/bot suite also passed under AddressSanitizer and
UndefinedBehaviorSanitizer with halt-on-error enabled and zero findings.
Apple's runtime does not support LeakSanitizer's `detect_leaks` option; that
unsupported option aborted before testing, so the successful run omitted only
that option.

### Context-aware Value critic and decision-root replay (declared)

Declared after rereading the independent review timestamped 2026-07-25 15:10
PDT and before training a fresh control artifact in the exact expanded-card
environment. The review independently reproduced the Markov-context collision:
the current critic cannot distinguish a Pass that retains priority from a
Pass that resolves a lethal spell.

Hypothesis: C16's remaining hold-versus-waste defect is caused by the
combination of that missing rules context and sparse state collection. Adding
neutral decision context to the critic and fitting it on bounded, dense
priority-root traces will improve held-out pass-sensitive action ranking and
paired five-deck strength. This is tested as a staged 2x2 rather than bundling
both axes:

- S0: existing sparse trace, context masked (the current C16 control);
- S1: existing sparse trace, context live;
- D0: dense priority-root trace, context masked;
- D1: dense priority-root trace, context live.

All cells use `T=800`, 16 self-play generations, training seed `424242`, the
same deck/game seed stream, terminal/bootstrap targets, optimizer, replay
window, exploration schedule, Learned-mirror opponents, and K=8 deployment.
The only context inputs are a context-valid bit, a seven-way phase one-hot,
priority holder relative to the perspective (self/opponent), pass count
(zero/one), and the sorcery-action bit. They append to the critic only and
contain no card names, hand-written values, combat scores, Handcrafted labels,
or opponent hidden cards.

Dense collection records the real rules context at every priority callback,
including pass-one/nonempty-stack positions. It is deterministically capped at
64 retained roots per game with rules-context strata retained before an even
chronological fill, preventing long White games from dominating memory and
fit weight. The report must expose retained roots by deck, phase, pass count,
and stack status. Existing `run_with_trace` and S0 remain unchanged.

The treatment is a separate model/artifact family. Existing C16 remains
read-only and must be prediction/action-record bit-identical in S0. New
context weights append after the legacy state prefix and initialize to zero.
Artifact metadata binds the exact engine, ordered context schema, sparse/dense
trace mode and cap, recipe, training games, generations, seed, dimensions,
and fingerprint; cross-family loads fail closed. Actor-owned reference labels
remain candidate-independent and are never regenerated by a candidate model.

Before implementation, freeze the fresh exact-environment S0 artifact with:

```sh
./build/old-school-sim --benchmark --games 1 --seed 919190 \
  --challenger learned-value-c16 --baseline learned-value-g0 \
  --learned-rollouts 8 --train-games 800 --train-seed 424242 \
  --refresh-value-challenger-cache
```

This command is artifact generation and a deterministic smoke only; its 60
paired games cannot support a strength conclusion.

Engineering gates:

1. S0 loads the frozen control and reproduces its fingerprint, predictions,
   and fixed-seed game records exactly.
2. The contextual encoder differs between pass zero/pass one and First/Second
   Main while remaining bit-identical under opponent-hidden-zone
   repartition.
3. Every critic call site carries the live successor context: casting retains
   priority at pass zero; pass zero transfers priority at pass one; pass one
   resolves the stack or ends the window; next-turn bootstraps begin in First
   Main at pass zero; combat successors enter End Combat priority.
4. Dense traces are deterministic, bounded, chronological, and cover all five
   decks plus nonempty-stack/pass-one states. S0 sparse traces are unchanged.
5. Same seed reproduces every candidate fingerprint and gameplay record;
   separate family/schema/cache validation fails closed.
6. Strict tests, hidden-information invariance, CLI lifecycle, and
   ASan/UBSan pass.

Offline gates come before gameplay. Score S0 and each licensed treatment on
the deck-balanced dev-v3 corpus, validation-v1 RU Pass versus Disintegrate
X=0, the live Force Spike probe, its payable-tax control, and the existing
Counterspell fixtures using common worlds. Report per-deck top-1/stable-pair
agreement and regret plus critic Brier/log-loss/calibration. A treatment is
rejected if any deck's mean regret worsens by more than 0.01 or Counterspell
regresses. The live Force Spike candidate must select Spike at deployed K=8;
the payable control must not prefer wasting it. At K=256, the RU
`Q(Pass)-Q(X=0)` lower 95% bound must be above zero and its paired improvement
over S0 must be positive. These small targeted corpora can reject but never
promote.

Stage S1 first because it is the simpler treatment. If it clears every offline
gate, select it and defer dense collection. Otherwise train D0 and D1; D1 must
beat both D0 and S1 on pooled regret and critic loss, demonstrate improvement
in the pass-sensitive stratum, and respect every nonregression gate. Do not
choose a cell based on one gameplay seed.

The single offline-selected candidate enters this gameplay ladder:

1. a reject-only 600-paired-game screen versus frozen S0 at virgin evaluation
   seed `919191`; stop below 47.5% aggregate or below 40% on any deck;
2. 2,040 paired games versus S0 at virgin seed `271828`; require a Wilson 95%
   lower bound above 50% and more challenger wins on every deck;
3. only then, 2,040 paired games versus Handcrafted at separate virgin seed
   `314159`, requiring the repository's aggregate and all-five deck gates;
4. if that passes, freeze the artifact and run seeds
   `101,202,303,404,505,606,707,808`, followed by the mixed-field all-five
   lift gate.

No Handcrafted result is consulted during treatment selection. Promotion
still requires no losing validation seed, pooled lower confidence above 50%,
more wins on every challenger deck, the largest lift on all five decks, and
all correctness/sanitizer gates.

### Exact-environment S0 control artifact (result)

Recorded after rereading the independent review timestamped 2026-07-25 15:40
PDT. The exact preregistered command trained and atomically published the
state-only C16 control in 401.35 seconds:

- artifact:
  `build/model-cache/old-school-value-challenger-v2-c16-t800-s424242.bin`;
- fingerprint:
  `bda1ea4401388bac3f26cf773623bac8848482f68e73d45a968473105a6d8dbc`;
- training seed `424242`, initial games `800`, generations `16`.

The command then trained G0 fingerprint
`c900b03b9b66e788c5a0d1efadea038c526968c229b7ab626b3d603dc43496a0`
and ran the declared 60 paired games at evaluation seed `919190`. C16 went
23--37 (`38.3%`, approximate interval `27.1%--51.0%`), with deck records
Green 0--12, Red 4--8, Blue 8--4, White 5--7, and RU 6--6. The CLI exited one
because its verdict was correctly inconclusive.

Decision: accept only the frozen artifact identity and reject any strength
interpretation. Sixty paired games have essentially no power for this
comparison, and the preregistration explicitly designated it as an artifact
smoke. This result is not used to select a treatment. S0 is now the immutable
exact-environment control for offline metrics and equal-K paired comparisons.

### Exact-environment Force Spike offline audit (result)

Recorded after rereading the independent review timestamped
2026-07-25 15:40 PDT. The first attempt correctly failed closed because the
pre-expansion dev-v3 label cache was bound to a different Actor fingerprint.
The exact successful command was:

```sh
./build/old-school-sim --score-probes \
  --actor-generation 0 --value-generation 0 \
  --probe-worlds 8 --probe-horizon 0 \
  --train-games 800 --train-seed 424242 \
  --learned-rollouts 8 \
  --probe-cache data/old-school-probe-dev-v3-k8-h0-audit.labels.tsv \
  --refresh-probe-cache
```

It regenerated the 20-position labels with exact-environment Actor G0
fingerprint
`7639176465b7b7c240e9d0d0067d352b0cac052a7083b47e6504073206068a84`
and scored Value G0 fingerprint
`c900b03b9b66e788c5a0d1efadea038c526968c229b7ab626b3d603dc43496a0`.
On the tapped-out Gray Ogre fixture the Actor reference ranked Force Spike
above Pass by `0.0287` Q with paired standard error `0.0094` and 95% interval
`[0.0102, 0.0472]`. Value G0's deployed K=8 policy had 100% top-1 agreement
on all four Blue fixtures, including selecting Force Spike on this live
counter. Hidden-zone repartition was bit-identical for every reported policy.

The frozen exact-environment S0 treatment was then checked with:

```sh
./build/old-school-sim --score-probes \
  --probe-worlds 8 --probe-horizon 0 \
  --learned-generations 16 --learned-rollouts 8 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/old-school-probe-dev-v3-k8-h0-audit.labels.tsv
```

It loaded C16 fingerprint
`bda1ea4401388bac3f26cf773623bac8848482f68e73d45a968473105a6d8dbc`
and completed in 88.30 seconds including deterministic Actor/Value G0
retraining. C16 likewise had 100% Blue top-1 and stable-pair agreement with
zero regret, proving that its deployed selected key on the live fixture was
`force-spike-gray-ogre`.

Decision: the current deployed state-only learner does use Force Spike when
the target cannot pay, and the rules-level fixture proves that choice really
counters the spell. This remains a diagnostic rather than promotion evidence:
the Actor margin is small, and dev-v3 lacks the paired state where the target
controller can pay the tax. Add a supplemental live/payable two-state
behavior report outside the balanced corpus metrics, then require every
context candidate to select Force Spike only in the live state.

The supplemental deployed-policy report was then implemented without changing
the 20-probe corpus, its cache identity, or any balanced metric. The live
state has exactly the three tapped Mountains used for Gray Ogre; the payable
control has a fourth, untapped Mountain moved from Red's hidden library, so
both card conservation and the mana history are natural. The same exact C16
command above produced:

- Value G0: live `Pass 0.0695`, `Force Spike 0.0863` (select Spike, pass);
  payable `Pass 0.0699`, `Force Spike 0.0753` (select Spike, **fail**).
- frozen S0 C16: live `Pass 0.1129`, `Force Spike 0.1916` (select Spike,
  pass); payable `Pass 0.1534`, `Force Spike 0.2022` (select Spike,
  **fail**).

Both controls passed the bit-identical hidden-repartition check. The generic
probe command still exited zero because these controls are explicitly
reject-only diagnostics; the report marks each model's behavioral gate as
failed.

Decision: retain the diagnostic and reject the stronger claim that the current
learner understands Force Spike. G0 and C16 do cast it when it counters, but
both also waste it when Red can pay, exactly the hold-versus-spend defect the
context treatment targets. S1 must flip the payable state to Pass while
retaining Force Spike in the live state before it can clear the preregistered
offline gate.

### Sparse/context-live S1 offline cell (result: rejected)

Recorded after rereading the independent review timestamped 2026-07-25 16:10
PDT. This is the preregistered S1 cell only: the existing sparse decision-root
trace with the neutral decision context live. It used the frozen S0 artifact,
the same Actor-owned labels and common worlds, and no Handcrafted data or
gameplay result:

```sh
./build/old-school-sim --score-probes \
  --probe-worlds 8 --probe-horizon 0 \
  --learned-generations 16 \
  --challenger learned-value-context-c16 \
  --learned-rollouts 8 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/old-school-probe-dev-v3-k8-h0-audit.labels.tsv
```

The run reproduced the exact frozen identities for Actor G0
`7639176465b7b7c240e9d0d0067d352b0cac052a7083b47e6504073206068a84`,
Value G0
`c900b03b9b66e788c5a0d1efadea038c526968c229b7ab626b3d603dc43496a0`,
and S0 C16
`bda1ea4401388bac3f26cf773623bac8848482f68e73d45a968473105a6d8dbc`.
S1 trained in 281.20 seconds and atomically published
`build/model-cache/old-school-value-context-s1-v2-c16-t800-s424242.bin`
with fingerprint
`68d0ffefa511318983588c10cebf04d1008ba2b1a40b8315bef6190cd530d742`.

S1 retained 108,428 sparse roots: 30,041 random anchors and 78,387
self-play roots. Decision-player deck coverage was Green 17,194, Red 15,956,
Blue 25,024, White 34,401, and RU Aggro 15,853. It included 5,643 pass-one
roots and 13,143 nonempty-stack roots. Hidden-zone repartition was
bit-identical for all seven policy views across all 20 probes.

Offline metrics:

- S0: pooled top-1 90.0%, stable-pair agreement 94.44%, mean regret
  0.0085, critic Brier 0.0528; deck regrets Green 0.0368, Red 0, Blue
  0, White 0.0013, RU Aggro 0.0045.
- S1: pooled top-1 90.0%, stable-pair agreement 96.30%, mean regret
  0.0061, critic Brier 0.0711; deck regrets Green 0, Red 0, Blue 0,
  White 0.0259, RU Aggro 0.0045.
- S1 fixed the Green Growth selection and retained zero Blue regret with no
  reported Counterspell selection regression. It nevertheless worsened White
  regret by 0.0246 and critic Brier by 0.0183.
- Force Spike live control: S1 scored Pass 0.1921 and Force Spike 0.2701,
  uniquely selecting Force Spike (pass).
- Force Spike payable control: S1 scored Pass 0.1686 and Force Spike 0.2284,
  again selecting Force Spike (fail).

Decision: reject S1 before gameplay. It improved pooled regret, but failed the
explicit payable-tax behavior gate and exceeded the per-deck +0.01 regret
nonregression limit on White. The result is informative rather than an
implementation failure: the critic can represent the public mana difference,
but sparse collection omits many forced-pass successor roots used by shallow
search. Per the preregistration, implement and score dense/context-masked D0
and dense/context-live D1 next; neither may be selected from a gameplay seed.

### Learned-pilot deck evolution route (engineering result)

The deck-evolution CLI previously hardcoded Handcrafted. It now accepts an
explicit immutable context challenger while preserving Handcrafted as the
default. The deterministic routing smoke was:

```sh
./build/old-school-sim --evolve-deck \
  --evolve-pilot learned-value-context-c1 \
  --generations 1 --population 5 --games 1 --seed 7 \
  --learned-rollouts 1 --train-games 1 --train-seed 424242
```

Fresh generation and cached reload used the same fingerprint
`cdfacd5620c449bfd18b4e86a1799f5a1b66f3c5da0006e10a6ac753cad16058`
and produced byte-identical evolution reports. The loaded run identified the
pilot as `Learned Value Context C1 (K=1, training seed 424242, 1 initial
games)` and returned the Green seed list at 50.0% (10-10) overall, exactly
2-2 against each of the five metagame decks. This is a T1/C1 lifecycle smoke,
not evidence that the deck or pilot is strong. Real use should select the
frozen C16 artifact, K=8, and an independently sized evolution search.

### Dense D0/D1 offline cells (result: rejected)

Recorded after rereading the independent review timestamped 2026-07-25 16:46
PDT. S1 failed its preregistered gates, so the remaining two cells were trained
and scored together with the exact ordered S0 -> S1 -> D0 -> D1 attribution:

```sh
./build/old-school-sim --score-probes \
  --probe-worlds 8 --probe-horizon 0 \
  --learned-generations 16 \
  --challenger learned-value-dense-context-c16 \
  --learned-rollouts 8 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/old-school-probe-dev-v3-k8-h0-audit.labels.tsv
```

The run reloaded the exact frozen S0 fingerprint `bda1ea44...` and S1
fingerprint `68d0ffef...`; it did not regenerate Actor-owned labels. D0
(Dense/context masked) trained in 351.72 seconds and published
`build/model-cache/old-school-value-context-d0-v2-c16-t800-s424242.bin`,
fingerprint
`19aa52c98fbd848a0f086b8eda11016cf1dc5b9eb7b669bbc5b7040ce03352f1`.
D1 (Dense/context live) trained in 308.31 seconds and published
`build/model-cache/old-school-value-context-d1-v2-c16-t800-s424242.bin`,
fingerprint
`9e2c225e53de967fa5b48b0e4625d6965e9edd9a341ae97449f43847107e3e55`.

Both dense cells retained the full deterministic cap of 256,000 roots over
4,000 games. D0 contained 111,968 pass-one and 49,756 nonempty-stack roots;
D1 contained 111,809 and 49,906 respectively. Every decision-player deck had
roughly 50--52k retained roots. Hidden-zone repartition remained bit-identical
for all nine scored policy views.

Offline checkpoint results:

| Cell | Pooled top-1 | Stable pairs | Mean regret | Critic Brier | Deck regrets G/R/B/W/RU |
| --- | ---: | ---: | ---: | ---: | --- |
| S0 | 90.0% | 94.44% | 0.0085 | 0.0528 | .0368/.0000/.0000/.0013/.0045 |
| S1 | 90.0% | 96.30% | 0.0061 | 0.0711 | .0000/.0000/.0000/.0259/.0045 |
| D0 | 90.0% | 92.59% | 0.0032 | 0.0954 | .0146/.0000/.0000/.0013/.0000 |
| D1 | 90.0% | 94.44% | 0.0032 | 0.0985 | .0146/.0000/.0000/.0013/.0000 |

Dense collection therefore halved pooled regret relative to S0 and fixed the
RU land-choice probe. It retained zero Blue regret and produced no reported
Counterspell regression. However, calibration worsened substantially, and D1
did not beat D0 on pooled regret or critic loss as required.

The paired deployed Force Spike controls were decisive:

- D0 live: Pass 0.1393, Force Spike 0.1690 (select Spike, pass);
  payable: Pass 0.1171, Force Spike 0.1349 (select Spike, fail).
- D1 live: Pass 0.1140, Force Spike 0.1468 (select Spike, pass);
  payable: Pass 0.0941, Force Spike 0.1167 (select Spike, fail).

Decision: reject both dense cells before validation-v1 or gameplay. D0 fails
the payable-tax gate. D1 also fails that gate, ties rather than beats D0's
pooled regret, and has worse critic Brier than both D0 and S1. The experiment
falsifies the hypothesis that context representation plus substantially more
forced-pass value roots is sufficient. Dense data improves broad action
ranking, but the remaining hold-versus-spend failure is now most plausibly an
action-preference/teacher-target problem. Proceed to a separately versioned,
iterated search-distilled policy-head experiment; do not tune D1 or screen it
on a gameplay seed.

### P16 search-teacher sufficiency audit (declared)

Declared after rereading the independent review timestamped 2026-07-25 16:46
PDT and before implementing or fitting a policy head. This is a
measurement-only prerequisite to the proposed P16 experiment. It changes no
training, model, probe label, or deployed policy.

Hypothesis: the frozen S0 C16 model's own high-sample, unblended Value-mirror
search contains the action signal that a separate policy head would need.
Using the existing paired live/payable Force Spike controls, K=256 common
information-set worlds, one rollout per world, H=4, and no shallow-prior
blend, it must:

- rank Force Spike above Pass in the live state with the paired 95% lower
  confidence bound on `Q(Spike)-Q(Pass)` above zero;
- rank Pass above Force Spike in the payable state with the paired 95% lower
  confidence bound on `Q(Pass)-Q(Spike)` above zero; and
- rank Pass above opponent-targeted Disintegrate X=0 in the harvested
  validation-v1 state with the paired 95% lower confidence bound on
  `Q(Pass)-Q(X=0)` above zero; and
- remain bit-identical under opponent-hidden-zone repartition.

The 256 paired samples will also be partitioned in order into 32 disjoint
K=8 blocks. At least 24 of 32 blocks must have the correct ordering in each
of the three comparisons, so a practical K=8 training teacher is not justified only by an
expensive asymptotic estimate. Exact ties count as incorrect.

Two fixed diagnostics are reported but cannot substitute for the primary
gate: S0 C16 at K=256/H=0 without a shallow prior isolates its critic
bootstrap, and frozen Actor G0 at K=256/H=0 without a shallow prior measures
whether the existing Actor-owned reference has the distinction. Every row
uses the same physical controls, deterministic seed derivation, paired
candidate worlds, and hidden-repartition check.

Decision rule: implement pure iterated search distillation only if the primary
S0 K=256/H=4 teacher passes every conjunctive gate. If it fails, do not tune a
policy-prior weight or distill a teacher already known to prefer the dominated
payable action. Instead, the next P16 design must add an orthogonal
self-generated improvement signal (for example counterfactual outcome
advantage or recursive search), still with no Handcrafted data, card-specific
rule, combat score, or opponent hidden identity. This two-state audit can
reject a teacher mechanism but cannot promote a bot.

### P16 search-teacher sufficiency audit (result: rejected)

Recorded after rereading the independent review timestamped 2026-07-25 17:01
PDT. A read-only diagnostic harness composed the existing public probe,
information-set search, artifact-loader, and paired-estimate APIs. It loaded
the frozen S0 C16 artifact in 0.014 seconds and verified fingerprint
`bda1ea4401388bac3f26cf773623bac8848482f68e73d45a968473105a6d8dbc`.
Every row used 256 common information-set worlds, one rollout per world, no
shallow-prior blend, Value-mirror continuation, epsilon zero, and an exact
hidden-repartition clone.

The primary K=256/H=4 results were:

| comparison | first Q | second Q | oriented delta | paired SE | 95% interval | correct K=8 blocks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| live Spike minus Pass | .191790 | .121599 | +.070191 | .001934 | [.066401, .073982] | 32/32 |
| payable Pass minus Spike | .122070 | .165836 | -.043767 | .001860 | [-.047412, -.040122] | 0/32 |
| RU Pass minus X=0 | .478538 | .597960 | -.119422 | .005931 | [-.131046, -.107797] | 0/32 |

The fixed H=0 diagnostic retained the same signs. Live Spike minus Pass was
`+0.029915` with interval `[+0.027377,+0.032453]` and 32/32 correct K=8
blocks. Payable Pass minus Spike was `-0.005043` with interval
`[-0.006648,-0.003438]` and only 4/32 correct blocks. RU Pass minus X=0 was
exactly `-0.079679` in every paired sample and 0/32 blocks. Every original
versus hidden-repartition sample was bit-identical. The three H=4 rows took
4.24 seconds and the whole audit took 4.77 seconds after compilation.

The separately frozen Actor G0 diagnostic agreed with the failure. Its
payable `Q(Spike)-Q(Pass)` remained positive at K=256/H=0
(`+0.006708`, interval `[+0.005741,+0.007675]`), K=64/H=4
(`+0.020490`, `[+0.014830,+0.026151]`), and K=256/H=12
(`+0.005094`, `[+0.001171,+0.009017]`). Increasing worlds or horizon
therefore does not reveal a clean distillation target.

Decision: reject pure P16 search distillation before fitting it. The primary
teacher passes only the live counter and fails both option-value comparisons
decisively, including every practical K=8 block. H=4 makes both wrong margins
larger, not smaller. A head trained only by cross-entropy to this search would
make the two known failures more confident. The next policy experiment must
retain search as a bounded prior while obtaining its corrective direction from
an orthogonal, self-generated outcome signal.

The one-off harness was removed after the measurement. A permanent CLI audit
using the same public APIs is being added so this result can be reproduced
without reconstructing the harness; that engineering reproduction will not
change the decision.

### Outcome-tilted priority residual P16 (declared)

Declared after the failed teacher audit and before changing Learned policy
training or deployment. The hypothesis is that long-term outcomes contain
useful hold-versus-spend information even though the frozen search teacher
does not. A bounded, priority-only residual trained by advantage-weighted
regression can correct those preferences while retaining the strong S0 C16
Value search, critic, and combat policy.

The immutable P0 parent is exact S0 C16 fingerprint `bda1ea44...`. Every
Value model already contains a neutral observation-action Priority head whose
output paths are initialized to zero. For legal priority action `a`, the P
family's deployed score is fixed to:

```text
S(a) = Q_S0_or_parent(a)
       + 0.10 * tanh(logit(a) - mean_legal_logit)
```

`Q` is the unchanged production K=8/H=4 Value score, including its aggregate
shallow-prior blend. The centered residual is bounded to `[-0.10,+0.10]`;
there is no weight sweep. Uniform P0 logits contribute exactly zero, so P0 at
weight 0.10 and weight zero must both be action-, score-, and game-record
identical to S0. Both continuation seats receive the same frozen parent model
and residual, while Value attack, block, and damage-order code remains
unchanged.

Training is 16 immutable frozen-parent generations, with P1 and P4 as
predeclared mechanism checkpoints rather than selectable endpoints:

- each generation uses the existing exact 40-game schedule: all ten unordered
  five-deck pairs, both seat orientations, and both starting players;
- both seats use the same frozen parent; Handcrafted is never used;
- every multi-action Priority root runs the same K=8/H=4 information-set
  search and samples the real action from
  `mu = 0.9*softmax(S/0.10) + 0.1/N`, using indexed seeds;
- after each game, retain at most 32 evenly spaced Priority roots per actor
  and weight each by the inverse retained count, so every seat-game and deck
  contributes equal total weight;
- for each actor's chronological retained roots, compute the existing
  TD(lambda) return with lambda `0.90` and terminal `z` in `{0,0.5,1}`;
  use `R = 0.5*G_lambda + 0.5*z` and
  `A = clamp(R - V_S0(root), -0.5, +0.5)`;
- turn that chosen-action evidence into one valid all-action target,
  `y(a) proportional to mu(a) * exp((A/0.25) * I[a=chosen])`.
  Positive advantage raises the sampled action; negative advantage suppresses
  it and redistributes mass over all alternatives according to `mu`.
  At zero advantage, `y` equals `mu` exactly.

There is no separate search cross-entropy loss: the known-wrong search score
is the KL anchor inside `mu`, while the outcome advantage is the sole
improvement direction. Fit only the outer Priority head to weighted
cross-entropy between `y` and the candidate's combined-score distribution.
The critic ensemble and Attack, Block, and DamageOrder heads remain
bit-identical. Replay contains exactly the newest three immutable policy
shards, with occupancy `1,2,3,3,...`.

The fixed optimizer is deterministic mini-batch Adam: batch 64, eight epochs,
learning rate `0.001`, beta1 `0.9`, beta2 `0.999`, epsilon `1e-8`, global
gradient-norm clip `5`, indexed PolicyFit shuffles, and fresh moments each
generation. These values will not be tuned against named probes or gameplay.

P0 and P1 mechanism gates precede any strength interpretation:

1. default residual zero and uniform-head residual 0.10 reproduce S0 exactly;
2. P1 is fixed-seed deterministic and leaves its parent, every critic
   prediction, and all three non-Priority heads bit-identical;
3. each deck has both positive and negative retained advantage weight and
   nonzero search/outcome conflict;
4. newest-shard `KL(y || deployed_distribution)` falls at least 30%, weighted
   chosen probability moves with the sign of advantage more than 60% of the
   time, some deployed argmax weight changes, and fewer than 5% of residuals
   saturate;
5. all targets are finite/normalized, exact per-deck game/root/weight and
   rollout accounting balances, and hidden repartition leaves logits,
   combined scores, targets, and choices bit-identical; and
6. P1 retains live Force Spike and all four Counterspell top-one choices, with
   no deck regret more than S0 plus 0.01. Payable Spike and X=0 are reported
   but are not required to flip after only one generation.

If P1 fails a mechanism or isolation gate, stop rather than scaling a broken
operator. If it passes, continue the unchanged recipe through P4. P4 must
uniquely select Spike live, Pass payable, and Pass over validation-v1 X=0;
retain every Counterspell choice; improve pooled dev-v3 regret; worsen no deck
regret by more than 0.01; and preserve exact hidden invariance. Because the
critic is frozen, its predictions and Brier/log-loss must remain bit-identical
to P0. Only then run the reject-only screen:

```sh
./build/old-school-sim --benchmark --games 10 --seed 919191 \
  --challenger learned-value-policy-p4 --baseline learned-value-policy-p0 \
  --learned-rollouts 8 --train-games 800 --train-seed 424242
```

Stop below 45% aggregate or below 40% on any challenger deck. A near-50%
result is not an improvement claim. If P4 clears, continue the already fixed
recipe to P16 rather than choosing a lucky intermediate checkpoint.

P16 must clear the same offline behavioral/nonregression gates, then the
generation and promotion ladder:

1. 2,040 paired games versus P4 at virgin seed `161803`, requiring a Wilson
   95% lower bound above 50% and more wins on all five P16 deck slices;
2. 2,040 paired games versus P0 at seed `271828` with the same requirements;
3. 2,040 paired games versus Handcrafted at seed `314159`, requiring the
   repository's aggregate and all-five direct gates;
4. the fixed evaluation seeds `101,202,303,404,505,606,707,808`, no aggregate
   seed loss, and the all-five mixed-field lift gate.

Before any Handcrafted comparison, freeze a separate fail-closed P-family
artifact bound to the exact P0 fingerprint and full recipe. Strict unit,
integration, CLI, determinism, hidden-information, and sanitizer gates remain
mandatory. These small named probes may reject P1/P4/P16 but can never promote
one.

### Permanent search-teacher audit reproduction

Recorded after rereading the independent review timestamped
2026-07-25 17:10 PDT. The temporary audit has now been replaced by a permanent,
evaluation-only CLI and generic tested API. It scores the two physical Force
Spike controls plus validation-v1 X=0 directly from frozen models; it does not
read or mutate a label cache.

Exact command:

```sh
./build/old-school-sim --diagnose-force-spike-teacher \
  --learned-generations 16 \
  --train-games 800 --train-seed 424242
```

The CLI loaded the exact S0 C16 fingerprint
`bda1ea4401388bac3f26cf773623bac8848482f68e73d45a968473105a6d8dbc`.
Its primary K=256/H=4 row reproduced every earlier S0 point estimate,
standard error, confidence interval, K=8 block count, exact selected set, and
hidden-repartition result:

- live `Q(Spike)-Q(Pass) = +0.070191`, SE `0.001934`, 95% interval
  `[+0.066401,+0.073982]`, 32/32 correct blocks;
- payable `Q(Pass)-Q(Spike) = -0.043767`, SE `0.001860`, 95% interval
  `[-0.047412,-0.040122]`, 0/32 correct blocks; and
- RU `Q(Pass)-Q(X=0) = -0.119422`, SE `0.005931`, 95% interval
  `[-0.131046,-0.107797]`, 0/32 correct blocks.

The permanent H=0 S0 control likewise reproduced `+0.029915`,
`-0.005043`, and `-0.079679`, with 32/32, 4/32, and 0/32 correct blocks.
The canonical Actor G0 H=0 row used fingerprint `76391764...` and retained the
same failure signs: live `+0.031364`, payable `-0.007245`, and RU
`-0.029690`. Its deterministic paired seed differs from the earlier one-off
Actor-only diagnostic; that control cannot substitute for the primary row.
All nine original/hidden-repartition comparisons were bit-identical.

Verification:

```sh
make -j4 build/old-school-sim build/old-school-probe-runner-tests
./build/old-school-probe-runner-tests
```

The strict build passed and all 21 probe-runner tests passed. Decision
unchanged: pure search distillation is rejected. The permanent route closes
the independent review's reproducibility request; proceed with the already
preregistered outcome-tilted priority residual without consulting a gameplay
seed.

### P-family distribution and mechanism-metric clarification (declared)

Declared after rereading the independent review timestamped
2026-07-25 17:20 PDT and before fitting P1. This resolves two implementation
details that the recipe named but did not define numerically; it does not alter
the residual bound, data, optimizer, seeds, checkpoints, or gates.

The distribution fitted by cross-entropy is the full collection/deployment
behavior distribution

```text
p_model(a) = softmax((Q_parent(a) + residual_model(a)) / 0.10)
mu_model(a) = 0.9 * p_model(a) + 0.1 / N
```

rather than bare `p_model`. Thus, when advantage is exactly zero,
`y = mu_parent` and the frozen parent is an exact optimum with zero gradient.
This preserves the preregistered claim that outcome advantage is the sole
improvement direction; fitting bare softmax would introduce an unintended
uniform-flattening update even at zero advantage. The optimizer differentiates
through the 90/10 mixture exactly.

P1 mechanism metrics use only the newest immutable shard and inverse-root
weights:

- `KL` is weighted mean `KL(y || mu_model)`, and the required reduction is
  `(KL_parent - KL_candidate) / KL_parent`;
- signed movement is the weighted fraction of nonzero-advantage examples where
  candidate chosen-action `mu` moves strictly above the parent for positive
  advantage or strictly below it for negative advantage; ties fail;
- argmax-change weight compares exact maxima of the combined score
  `Q+residual`, with any set change counted;
- a residual is saturated when
  `abs(tanh(logit - mean_legal_logit)) >= 0.95`. Saturation rate gives each
  legal action equal share of its root weight, preventing high-action-count
  roots from dominating;
- positive and negative advantage weights are reported separately per deck;
  search/outcome conflict means positive advantage on a sampled non-argmax
  action or negative advantage on a sampled exact-argmax action. Each deck
  must have nonzero conflict weight; and
- game balance means 16 seat-games per deck. Every retained seat-game assigns
  total policy weight one across at most 32 evenly spaced roots; rootless
  seat-games, raw/retained root counts, and exact rollout totals are reported
  rather than silently normalized away.

These definitions are fixed before P1 collection. They are mechanism
diagnostics, not knobs and not substitutes for the offline behavioral gates.

### P1 canonical execution identity (declared)

Declared after rereading the independent review timestamped
2026-07-25 17:35 PDT and before the first canonical P1 collection. This pins
the remaining run identity that the P-family recipe had described only as
fixed-seed.

The immutable P0 input is Value Challenger S0 C16 trained with seed `424242`
and 800 initial games. The canonical route must load fingerprint
`bda1ea4401388bac3f26cf773623bac8848482f68e73d45a968473105a6d8dbc`
and fail closed on any mismatch. The P-family self-play root seed is the
separate fixed value `577215`. The exact command is:

```sh
./build/old-school-sim --train-p-family 1 --seed 577215 \
  --train-games 800 --train-seed 424242
```

The CLI route locks K=8, one rollout per world, the production H=4 Value
horizon, 32 evenly retained Priority roots per actor-game, a 500-turn game
limit, residual weight 0.10, TD(lambda)=0.90, the already declared Adam
configuration, and the exact 40-game five-deck schedule. H=4 is retained to
isolate the outcome-learning operator against P0; this run cannot establish
that four turns is an optimal search horizon.

P1 passes its mechanism gate only if all exact schedule/root/rollout/target
accounting holds with no rootless actor-game; the critic and all three
non-Priority heads are bit-identical; every deck has nonzero positive
advantage, negative advantage, and search/outcome conflict weight; newest-shard
KL falls by at least 30%; signed chosen-probability movement is strictly above
60%; changed-argmax weight is nonzero; and option-balanced residual saturation
is below 5%. A completed run that misses a scientific threshold is a valid
rejection, not a process error. If any mechanism conjunct fails, stop without
probe scoring or P4. If all pass, run the already declared residual-aware
dev-v3, Counterspell, live/payable Force Spike, and validation-v1 X=0 gates
before continuing to P4.

### P1 canonical mechanism run (result: rejected)

Recorded after rereading the independent review timestamped
2026-07-25 18:14 PDT. The exact preregistered command completed successfully:

```sh
./build/old-school-sim --train-p-family 1 --seed 577215 \
  --train-games 800 --train-seed 424242
```

The immutable P0 fingerprint matched
`bda1ea4401388bac3f26cf773623bac8848482f68e73d45a968473105a6d8dbc`;
P1 produced fingerprint
`8efccd2272b7bd11807081e7e7d7d67e73afbb4e66bba3d5cd8b19915cdd6131`.
The run used the declared 40 balanced games, 80 seat-games, K=8/H=4,
one rollout per world, 32-root cap, four deterministic collection threads,
residual weight 0.10, TD(lambda)=0.90, and fixed Adam recipe. It completed in
143.211 seconds.

All schedule and isolation invariants passed: zero rootless seat-games, 2,280
raw roots, 2,015 retained roots, 6,854 retained options, 54,832 rollout
evaluations, total policy weight 80, normalized targets, and a one-generation
replay window. Per-deck newest-shard accounting was:

| Deck | Raw / retained roots | Options / rollout evaluations | +A / -A weight | Conflict weight |
| --- | ---: | ---: | ---: | ---: |
| Green | 411 / 389 | 1,036 / 8,288 | 11 / 5 | 7.160232 |
| Red | 404 / 382 | 1,243 / 9,944 | 4 / 12 | 7.378268 |
| Blue | 415 / 409 | 1,019 / 8,152 | 10 / 6 | 7.803909 |
| White | 679 / 482 | 2,069 / 16,552 | 8 / 8 | 7.736905 |
| RU Aggro | 371 / 353 | 1,487 / 11,896 | 7 / 9 | 7.293609 |

Every deck had the required positive advantage, negative advantage, and
search/outcome conflict signal. The critic, Attack, Block, and DamageOrder
components were bit-identical to P0; only Priority changed. Residual
saturation was exactly zero. Changed-argmax weight was 15.406764/80
(19.2585%), so both of those gates passed.

The two learning-effect gates failed:

- weighted newest-shard KL moved from `0.1118335933` to `0.1105178315`, only
  a 1.1765% reduction versus the preregistered minimum of 30%;
- signed chosen-probability movement was 43.948167/80 = 54.9352%, versus the
  required strictly above 60%. The positive-advantage stratum was 54.8395%
  and the negative-advantage stratum was 55.0309%.

Decision: reject P1. This was a mechanism-only gate, so there are deliberately
no gameplay win rates or per-deck strength claims. Per the preregistration,
do not score behavioral probes, run P4, or benchmark this checkpoint. The
next step is a measurement-only diagnosis of whether the failure comes from
optimizer underfit, shared-feature interference between contradictory roots,
or an error/mismatch in the reported gradient objective. Any changed training
recipe will be separately hypothesized and preregistered before another fit;
the failed thresholds will not be weakened post hoc.

### P1 fixed-shard capacity diagnosis (declared)

Declared after rereading the independent review timestamped
2026-07-25 18:35 PDT and before running the full diagnostic. This is a
measurement-only decomposition of the rejected P1 fit, not a new challenger,
probe gate, gameplay screen, or endpoint selection. The exact command is:

```sh
./build/old-school-sim --diagnose-p1-fit \
  --seed 577215 \
  --train-games 800 --train-seed 424242
```

The route must load the exact fingerprint-bound C16 P0 and reproduce the
canonical K=8/H=4, 40-game, 80-seat-game P1 collection once. It then fits five
independent cells from the same frozen parent, immutable shard, and indexed
PolicyFit seed; no cell is chained or published:

| Cell | Epochs | Learning rate |
| --- | ---: | ---: |
| E8/R0.001 control | 8 | 0.001 |
| E32/R0.001 | 32 | 0.001 |
| E128/R0.001 | 128 | 0.001 |
| E512/R0.001 | 512 | 0.001 |
| E128/R0.003 | 128 | 0.003 |

Batch size, Adam betas/epsilon, clipping, residual weight, temperature, and
fit seed remain identical to P1. The E8 control must reproduce the canonical
P1 model, component fingerprints, and mechanism metrics exactly. Every cell
must leave the frozen parent and the critic, Attack, Block, and DamageOrder
components bit-identical. Pooled and all-five-deck KL, signed movement by
advantage sign, argmax change, saturation, and fingerprints are reported.

The same shard also receives a card-agnostic independent-root capacity
bracket. For each root it minimizes

```text
KL(y || 0.9 * softmax((Q + 0.1*tanh(c))/0.1) + 0.1/N)
```

subject to `sum(c)=0`. The numerical solver has no RNG: zero, directed
log-ratio, and positive/negative focus starts; Euclidean zero-sum box
projection by exactly 96 bisections; at most 512 projected-gradient
iterations; and at most 48 Armijo backtracks with initial step 1, factor 1/2,
and coefficient 1e-4. Full range uses `c` in `[-12,12]`; the second result
restricts every `abs(tanh(c))` strictly below 0.95. A separate relaxation plus
Bernoulli data processing certifies a KL lower bound. Therefore each reported
range has the interpretation:

```text
constructively achieved reduction
    <= true independent-root optimum
    <= certified reduction upper bound
```

Interpretation is fixed before seeing the output:

1. Any control mismatch, changed frozen component, malformed bracket, or
   shard-identity mismatch is a process failure, not a scientific result.
2. If the zero-saturation certified reduction upper bound is below 30%, the
   original KL gate was impossible under its own no-saturation constraint.
   If the zero-saturation numerical achievable reduction is at least 30%,
   residual geometry is constructively sufficient. A bracket spanning 30%
   is inconclusive on geometry.
3. Optimizer underfit is supported if a higher-budget shared-head cell clears
   the original pooled KL reduction of at least 30%, signed movement strictly
   above 60%, and saturation below 5%. E128/R0.003 clearing when
   E128/R0.001 does not specifically implicates step size.
4. Shared-head interference or noisy contradictory credit is supported if
   the zero-saturation numerical oracle clears 30%, no shared-head cell
   clears the original mechanism gates, and E128 to E512 at rate 0.001
   changes both KL reduction and signed movement by less than two percentage
   points. Per-deck and positive/negative strata then identify where the
   compromise occurs.
5. Any other pattern is inconclusive and requires a separately declared
   diagnostic. No cell is accepted post hoc as a new P recipe.

Correction to the review note: canonical P1 used eight optimizer epochs, not
one, and retained up to 32 roots per actor-game, not per whole game. The
original run did not persist its shard, so the new route deterministically
recollects that shard once rather than claiming it can inspect unavailable
artifacts. Whatever this diagnosis shows, the rejected P1 thresholds remain
unchanged and any successor recipe must be preregistered separately.

### P1 fixed-shard capacity diagnosis (result: optimizer underfit)

Recorded after rereading the independent review timestamped
2026-07-25 18:35 PDT. The exact declared command completed successfully in
787.115 seconds:

```sh
./build/old-school-sim --diagnose-p1-fit \
  --seed 577215 \
  --train-games 800 --train-seed 424242
```

The route reproduced the exact P1 shard: 40 games, 80 seat-games, 2,015
retained roots, 54,832 K=8/H=4 rollout evaluations, total inverse-root weight
80, P0 fingerprint `bda1ea44...`, and canonical E8 candidate fingerprint
`8efccd22...`. Control equivalence, same-parent fitting, all component-isolation
checks, and oracle shard identity passed.

The independent-root capacity bracket decisively showed that the bounded
residual geometry can express the target:

| Range | Numerical best KL | Achievable reduction | Certified KL lower | Certified reduction upper | Iteration-limit roots |
| --- | ---: | ---: | ---: | ---: | ---: |
| Full | 0.0004472792 | 99.6000% | 0.0003984361 | 99.6437% | 1,455 |
| Strict zero-saturation | 0.0009374711 | 99.1617% | 0.0005186790 | 99.5362% | 1,457 |

The parent KL was `0.1118335933`. The zero-saturation numerical solver alone
constructively clears the original 30% requirement by more than 69 points, so
residual geometry is not the P1 failure. Many roots exhausted the conservative
512-iteration numerical budget, but this cannot weaken the constructive
99.16% result; the certified upper bracket remained properly ordered.

Pooled same-shard fit results were:

| Cell | KL reduction | Signed movement | +A / -A movement | Argmax change | Saturation |
| --- | ---: | ---: | ---: | ---: | ---: |
| E8/R0.001 control | 1.1765% | 54.9352% | 54.8395% / 55.0309% | 19.2585% | 0% |
| E32/R0.001 | 4.1987% | 59.0216% | 57.8236% / 60.2197% | 29.9293% | 0% |
| E128/R0.001 | 30.9265% | 77.5594% | 77.6196% / 77.4991% | 43.1335% | 0% |
| E512/R0.001 | 71.3016% | 94.4170% | 95.1122% / 93.7219% | 47.5529% | 1.2063% |
| E128/R0.003 | 61.9172% | 90.5125% | 91.4322% / 89.5929% | 46.1637% | 0.3963% |

Per-deck KL-reduction / signed-movement / saturation results were:

| Cell | Green | Red | Blue | White | RU Aggro |
| --- | ---: | ---: | ---: | ---: | ---: |
| E8/R0.001 | 1.61 / 56.46 / 0 | 0.95 / 58.13 / 0 | 1.95 / 57.34 / 0 | 1.43 / 51.32 / 0 | 0.16 / 51.43 / 0 |
| E32/R0.001 | 4.70 / 59.42 / 0 | 3.37 / 61.31 / 0 | 6.38 / 60.29 / 0 | 4.21 / 56.29 / 0 | 2.68 / 57.79 / 0 |
| E128/R0.001 | 32.37 / 78.49 / 0 | 25.32 / 73.97 / 0 | 42.23 / 81.91 / 0 | 25.11 / 72.76 / 0 | 29.62 / 80.67 / 0 |
| E512/R0.001 | 73.36 / 95.56 / 1.97 | 67.12 / 92.66 / 0.80 | 82.40 / 97.97 / 2.31 | 64.27 / 92.33 / 0.07 | 69.04 / 93.57 / 0.89 |
| E128/R0.003 | 66.24 / 90.31 / 0.64 | 57.92 / 89.34 / 0 | 73.26 / 94.64 / 1.20 | 54.62 / 86.87 / 0.05 | 57.35 / 91.40 / 0.09 |

Table entries are percentages in the order named by the heading. The full
machine output also reported positive and negative strata separately for every
deck; neither pooled stratum is the limiting case at the two stronger cells.

Decision: the preregistered optimizer-underfit diagnosis is supported.
E128/R0.001 clears the original pooled mechanism gates with zero saturation;
E128/R0.003 and E512/R0.001 clear them by wide margins on every deck as well.
The E128-to-E512 movement is far above the predeclared two-point plateau
criterion, so the shared-head-interference diagnosis is not supported.
E128/R0.003 greatly outperforming E128/R0.001 at equal epochs also supports
step-size underfit.

No diagnostic model is promoted or scored post hoc. The next step is to
separately preregister one revised optimizer recipe, reconstruct it from the
same immutable P1 collection, and subject it first to the already declared
residual-aware dev-v3, Counterspell, Force Spike, validation-v1 X=0, and hidden
invariance gates. Gameplay remains forbidden until those offline gates pass.

### P1R revised-optimizer offline behavioral gate (declared)

Declared after rereading the independent review timestamped 2026-07-25
18:53 PDT and before implementing or running the P1R probe route. The
fixed-shard capacity experiment was hypothesis generation only. This is a new,
separately named recipe and does not retroactively promote a diagnostic cell.

Hypothesis: the outcome residual's first failure was optimizer underfit.
Refitting the exact P1 shard for 128 epochs at learning rate `0.003` will turn
the available outcome signal into behaviorally useful hold-versus-spend
changes, while the bounded residual preserves the frozen S0 critic, combat
heads, and the existing strong stack decisions.

P1R changes exactly two canonical optimizer fields: epochs `8 -> 128` and
learning rate `0.001 -> 0.003`. Batch size 64, Adam betas/epsilon, gradient
clip, indexed fit seed, P0, root seed `577215`, 40-game five-deck schedule,
K=8/H=4 collection, one rollout/world, 32-root cap, residual weight 0.10,
TD(lambda) 0.90, and every other recipe field remain fixed. E128/R0.003 was
chosen because it was the cheapest diagnostic cell with wide all-five
mechanism margins and substantially less saturation than E512/R0.001; no
named behavioral probe or gameplay result was consulted. Deterministic
reconstruction must yield the already measured same-shard fingerprint
`a17814d6cca71c95ab937d162e8fd183679b8e88c0fd175a7d1f750d4cd06a9b`;
any mismatch is a process failure.

The permanent command will be:

```sh
./build/old-school-sim --score-p1r-probes \
  --seed 577215 --train-games 800 --train-seed 424242
```

It must fail closed on the exact P0, Actor G0, cache, P1R, optimizer, schedule,
root/rollout, target, replay, component-isolation, and hidden-repartition
identities. Before scoring probes, P1R must still clear the original mechanism
thresholds: pooled KL reduction at least 30%, signed movement strictly above
60%, nonzero argmax movement, residual saturation below 5%, and nonzero
positive, negative, and search/outcome-conflict weight for every deck.

The offline data are fixed and never refreshed by this route:

- deck-balanced dev-v3 uses the immutable Actor G0 K=8/H=0 cache
  `data/old-school-probe-dev-v3-k8-h0-audit.labels.tsv`, candidate Value
  scoring K=8/H=4, and exact Actor fingerprint `76391764...`;
- focused validation-v1 uses its immutable Actor G0 K=128/H=0 cache
  `data/old-school-probe-validation-v1-k128-h0-t800-s424242.labels.tsv` and
  fresh candidate Value scoring K=256/H=4.

Both P0 and P1R deploy with residual weight 0.10 in one ordered transition
family; uniform-head P0 must be decision- and metric-identical to residual-off
S0. P1R passes only if all of the following hold:

1. pooled dev-v3 mean regret is strictly below P0 and no deck's mean regret is
   more than P0 plus 0.01;
2. the selected action remains inside the Actor-reference best set on all
   three Counterspell fixtures (expensive spell, lethal Bolt, and counter
   war), and on the live Force Spike fixture;
3. the supplemental deployed controls uniquely select Force Spike when its
   tax is unpayable and Pass when the opponent can pay;
4. on validation-v1, P1R uniquely selects Pass over opponent-targeted
   Disintegrate X=0, its fresh common-world `Q(Pass)-Q(X=0)` point estimate
   strictly improves on P0, and its own 95% lower confidence bound is above
   zero;
5. hidden-repartition invariance passes everywhere; and
6. because the critic is frozen, its prediction is bit-identical to P0 on
   every state. Calibration scores are reported but are not required to be
   identical: this evaluator conditions their target on the action selected
   by each policy, so a genuine policy change can change Brier/log-loss even
   when the critic tensor and every state prediction are unchanged.

These small fixtures are reject-only. Passing them cannot promote P1R or
establish playing strength. If any conjunct fails, stop without P4 or
gameplay and diagnose the residual signal from the reported transition rows.
If every conjunct passes, separately preregister P4R with the same revised
optimizer and replay semantics before training it.

#### P1R validation-cache identity correction (declared)

Declared immediately after the filesystem metadata audit above and before
building or running the P1R route. The historical validation-v1 cache named in
the declaration is bound to Actor fingerprint `41a1be59...`; the current
five-deck exact-environment Actor G0 is `76391764...`. Loading it would
correctly fail closed. It must not be overwritten or mislabeled as current.

Before P1R exists, generate one new cache with the existing generic probe
route, current frozen Actor G0, K=128/H=0, training seed 424242, and a new
environment-qualified path:

```sh
./build/old-school-sim --score-probes \
  --probe-corpus validation-v1 \
  --probe-worlds 128 --probe-horizon 0 \
  --actor-generation 0 --value-generation 0 \
  --learned-rollouts 2 \
  --train-games 800 --train-seed 424242 \
  --probe-cache data/old-school-probe-validation-v1-exact-v2-k128-h0-t800-s424242.labels.tsv \
  --refresh-probe-cache
```

This is an artifact-identity correction, not a behavioral experiment: P1R is
not trained or scored by the command, the old cache remains intact, and the
new cache must report exact Actor fingerprint `76391764...`. The permanent
P1R route then loads this new cache without refresh and uses candidate K=256
exactly as otherwise declared. All gates and stopping rules remain unchanged.

#### Exact-environment validation cache (result)

Recorded after rereading the independent review timestamped 2026-07-25
19:05 PDT. The exact declared cache-generation command completed
successfully. Actor G0 trained in 74.76 seconds and reproduced exact
fingerprint
`7639176465b7b7c240e9d0d0067d352b0cac052a7083b47e6504073206068a84`;
diagnostic Value G0 reproduced
`c900b03b9b66e788c5a0d1efadea038c526968c229b7ab626b3d603dc43496a0`.

The new immutable file is
`data/old-school-probe-validation-v1-exact-v2-k128-h0-t800-s424242.labels.tsv`
with SHA-256
`98b1018c7204e9ec1f98a3bd79e7d621296cf90a60cf24cb020eb92a998a13ed`.
Its metadata binds K=128/H=0, one rollout/world, training seed 424242,
800 games, one validation-v1 fixture, current Actor fingerprint `76391764...`,
and information-set fingerprint `9bb67ff6e8b476b2`. The historical
`41a1be59...` cache remains untouched.

All five diagnostic policy views were bit-identical under hidden-zone
repartition. The newly frozen Actor reference estimates
`Q(Pass)-Q(X=0)=-0.0290`, paired SE `0.0010`, 95% interval
`[-0.0309,-0.0271]`. That wrong teacher preference is diagnostic context, not
a correctness label or a P1R result.

Decision: accept only the corrected cache identity. P1R has still not been
scored. The next action is the already preregistered exact P1R route; it must
load this cache without refresh and measure its own K=256 Value pair.

### P1R revised-optimizer offline behavioral gate (result: rejected)

Recorded after rereading the independent review timestamped 2026-07-25
19:05 PDT. The exact preregistered command completed and exited one for a
scientific rejection:

```sh
./build/old-school-sim --score-p1r-probes \
  --seed 577215 --train-games 800 --train-seed 424242
```

The route loaded exact P0 `bda1ea44...`, rebuilt exact Actor G0 `76391764...`,
and deterministically reconstructed P1R fingerprint
`a17814d6cca71c95ab937d162e8fd183679b8e88c0fd175a7d1f750d4cd06a9b`
in 168.80 seconds. The revised optimizer did solve the original fit problem:
newest-shard KL fell 61.9172%, signed movement was 90.5125%, argmax weight
changed 46.1637%, and saturation was 0.3963%. All 40 games, 80 seat-games,
2,015 retained roots, 54,832 evaluations, per-deck outcome-signal, target,
replay, optimizer, fingerprint, and component-isolation gates passed.

The immutable dev-v3 result rejected the behavioral hypothesis:

| Metric | P0 | P1R | Result |
| --- | ---: | ---: | --- |
| Pooled mean regret | 0.0085 | 0.0277 | worse, reject |
| Green regret | 0.0368 | 0.0425 | within +0.01 |
| Red regret | 0.0000 | 0.0000 | pass |
| Blue regret | 0.0000 | 0.0072 | within +0.01 |
| White regret | 0.0013 | 0.0259 | +0.0246, reject |
| RU Aggro regret | 0.0045 | 0.0631 | +0.0586, reject |

P0 residual-on was bit-identical to residual-off, P1R critic predictions were
bit-identical to P0, both caches loaded rather than regenerated, and every
hidden repartition passed. P1R retained the three actual Counterspell
fixtures, but inverted the live Force Spike choice: it selected Pass with
scores `Pass 0.1513` versus `Spike 0.0870` when the tax was unpayable, and
correctly selected Pass `0.1751` versus `Spike 0.0757` when payable. Thus the
combined four-state Blue stack gate and the live/payable gate both failed.

The transition rows show a broad overcorrection toward holding:

- live Force Spike changed from the correct counter to Pass;
- Green second-main Growth changed from Growth to Pass;
- RU's lethal Disintegrate fixture changed from X=3 lethal to Pass;
- White avoid-redundant-Moat changed to casting the redundant Moat; and
- the useful RU colored-land choice did improve from Mountain to Island.

Validation-v1 supplied one real success and one literal-gate failure. At
K=256, P1R moved `Q(Pass)-Q(X=0)` from P0's `-0.118985`
(`[-0.130564,-0.107405]`) to `+0.059624`
(`95% CI [0.049549,0.069699]`). It therefore learned a statistically clean
preference against wasting Disintegrate for zero. However, its unique global
selection was casting Ironclaw Orcs (`kind-2.card-14.x-0`), not Pass, so the
predeclared literal unique-Pass conjunct failed. In retrospect that conjunct
is stricter than the user's actual bug—productive creature development is not
an X=0 error—but it was fixed before the run and is not weakened after seeing
the result. The candidate is already rejected independently by regret and
live-Force-Spike failures.

Decision: reject P1R and do not train P4R or run gameplay. The optimizer now
fits the noisy outcome targets well enough to expose their next limitation:
one sampled action with long-horizon outcome credit can learn a coarse
"preserve resources" direction but does not reliably distinguish live,
productive spending from waste. The next experiment should reduce
outcome-target overfit or improve credit assignment, while preserving the
newly demonstrated positive X=0 direction; it must be separately declared
before another fit.

### FT128 full-terminal counterfactual credit audit (declared)

Declared at 2026-07-25 19:32 PDT after rereading `AGENTS.md`, the independent
review timestamped 2026-07-25 19:05 PDT, and the P1R rejection above. This is a
measurement-only signal-sufficiency experiment. It trains no candidate,
changes no deployed policy, reads or writes no probe-label cache, and makes no
playing-strength claim.

Hypothesis: same-world, all-action terminal counterfactual returns under the
frozen clean P0 Value-mirror continuation distinguish productive spending from
waste even though the H=4 bootstrapped teacher and P1's sampled-action outcome
labels do not. Specifically, the terminal reference will prefer Force Spike
over Pass when the opponent cannot pay the tax, while preferring Pass over
Force Spike when the opponent can pay it.

The permanent exclusive command will be:

```sh
./build/old-school-sim --diagnose-terminal-credit \
  --train-games 800 --train-seed 424242
```

It must load or deterministically reconstruct exact frozen P0 fingerprint
`bda1ea4401388bac3f26cf773623bac8848482f68e73d45a968473105a6d8dbc`.
The audit uses the existing live and payable Force Spike fixtures, stable
probe/world seed derivation, `K=1024` information-set worlds, one continuation
per world, horizon 128 complete future turns, Value-search mirror
continuations, epsilon zero, residual weight zero, and no shallow-prior blend.
Every legal root action is forced in every common world; action differences
therefore remain paired. Hidden-zone repartition clones must produce
bit-identical samples.

Terminality is fail-closed rather than assumed. Before scoring, each fixture
must satisfy the conservative library bound
`library[0].size() + library[1].size() + 1 <= 128`. The sampling path must
record whether each continuation ended in a natural game result before any
critic bootstrap. Every one of the expected candidate/world evaluations must
be terminal; a single bootstrapped or missing sample invalidates the run.

Primary gates are fixed before implementation:

1. live Force Spike has `Q(Force Spike)-Q(Pass)` with a strictly positive
   lower 95% paired-confidence bound and at least 96 of 128 disjoint K=8
   blocks correctly signed;
2. payable Force Spike has `Q(Pass)-Q(Force Spike)` with the same confidence
   and block gates;
3. both fixtures satisfy exact sample, configuration, fingerprint, terminal,
   and hidden-repartition accounting.

The existing Disintegrate-X=0 validation fixture is reported as corroborating
diagnostic only, not a primary gate: Pass can be healed by a later priority
choice, whereas both Force Spike branches become irreversible immediately at
their current priority state. If reported, the useful criterion is
`Q(Pass)-Q(X=0)>0` and exclusion of X=0 from the global best set; productive
development such as casting Ironclaw Orcs must not be mislabeled as a failure.

Passing this audit establishes only that full-terminal counterfactuals contain
the missing causal signal. The next separately declared step would extend the
same terminal scorer to mirrored Giant Growth and lethal/zero Disintegrate
contrasts before fitting an all-action counterfactual target. Any primary gate
failure rejects this terminal teacher as the immediate training signal; it
must not be repaired by tuning on these fixtures.

### FT128 full-terminal counterfactual credit audit (result: rejected)

Recorded on 2026-07-25 after rereading the independent review updated at
22:15 PDT. The immutable Environment-v2 command completed:

```sh
./build/old-school-sim --diagnose-terminal-credit \
  --train-games 800 --train-seed 424242
```

It loaded exact frozen P0 fingerprint
`bda1ea4401388bac3f26cf773623bac8848482f68e73d45a968473105a6d8dbc`
from
`build/model-cache/old-school-value-challenger-v2-c16-t800-s424242.bin`
and used the declared Value-mirror `K=1024/H=128` configuration with one
rollout per world, epsilon zero, residual zero, shallow-prior blend disabled,
and natural terminal results required.

The live Force Spike control passed cleanly:
`Q(Force Spike)-Q(Pass)=+0.240234`, paired SE `0.013357`, 95% CI
`[0.214054,0.266415]`, exact best `{Force Spike}`, and `117/128` correctly
signed K=8 blocks. The payable control failed decisively in the opposite
direction:
`Q(Pass)-Q(Force Spike)=-0.167969`, paired SE `0.011770`, 95% CI
`[-0.191037,-0.144900]`, exact best `{Force Spike}`, and only `1/128`
correctly signed K=8 blocks. Both primary rows had exact accounting
(`2,048/2,048` terminal evaluations apiece, zero bootstraps), passed their
conservative terminal bounds, and were bit-identical under hidden
repartition.

The diagnostic RU X=0 row weakly preferred Pass but did not resolve the
contrast: `Q(Pass)-Q(X=0)=+0.005859`, paired SE `0.015994`, 95% CI
`[-0.025488,0.037207]`, `53/128` correctly signed blocks. Pass was the exact
best action and X=0 was excluded; all `8,192/8,192` candidate evaluations
were terminal with zero bootstraps and the hidden clone was bit-identical.

Decision: **reject full-terminal Learned-mirror outcome credit as the next
teacher**. It passed all integrity and hidden-information checks, but failed
one of two preregistered primary signs by a wide margin. This independently
matches the reviewer's horizon-response finding: terminal saturation does not
recover the payable hold-versus-spend distinction. Do not proceed to the
previously conditional all-action terminal fit. The next research experiment
must be separately declared under Environment v3 after its rules correction
and artifact reset; the leading generic candidate is an auxiliary
card/resource-advantage signal that preserves nonterminal causal information
instead of relying on saturated mirror outcomes. This result is probe science,
not a five-deck playing-strength claim.

### Environment v3 cleanup-discard correction (declared)

Declared on 2026-07-25 after the user observed that an interactive player could
finish a turn with more than seven cards. Inspection confirmed a rules bug:
`cleanup_turn` removes damage and until-end-of-turn modifiers but never makes
the active player discard to the maximum hand size.

Hypothesis: implementing an engine-authoritative cleanup discard decision will
leave the active player with exactly seven cards whenever cleanup begins above
seven, move exactly the chosen excess cards to the public graveyard, preserve
card conservation and fixed-seed determinism, and expose the same legal choice
through terminal and web human controllers. Bot choices must remain legal;
Learned may rank discard afterstates with its own frozen value model but must
not receive card-specific rules or Handcrafted values.

Acceptance is rules-level rather than a strength claim:

1. only the active player discards, and exactly `hand_size - 7` unique legal
   hand positions are moved to the graveyard before cleanup completes;
2. human controllers can choose the discarded cards, malformed choices fail
   closed, and Random/Monte-Carlo/Deep/Handcrafted/Learned games all complete
   legally and deterministically;
3. Ancestral Recall and Braingeyser overdraw regressions, duplicate-card
   selections, public observation/event output, CLI interaction, web bridge
   action validation, and card conservation are covered;
4. strict tests plus ASan/UBSan pass.

This is **Environment v3**. It changes real trajectories, especially for Blue
and RU decks, so v2 trained artifacts, lift tables, and playing-strength
results are not comparable and cannot be promoted after the fix. The already
running FT128 command remains an immutable v2 signal-sufficiency audit; record
its result honestly when it finishes, but do not treat it as a v3 policy
promotion or silently reuse its model artifact in v3.

Review reconciliation: the 2026-07-25 21:48 PDT `REVIEW.md` entry describes
Environment v3 as “live,” but that status is premature. At the time of this
note the authoritative source still has the v2 cleanup implementation and its
raw call sites; v3 becomes live only after the rules implementation, cache
invalidation, and declared tests pass. This is a status disagreement, not a
disagreement with the reviewer's recommendation to make the correction.

### Environment v3 cleanup-discard correction (result: accepted)

Recorded on 2026-07-26 after rereading the independent review timestamped
00:45 PDT. The rules hypothesis passed. Cleanup now asks the active player for
exactly `hand_size - 7` unique hand positions, validates the complete choice
transactionally, moves those cards to the public graveyard, emits a public
discard event, and only then clears mana, damage, and until-end-of-turn
effects. Random, Monte Carlo, Deep Monte Carlo, Handcrafted, Learned Value,
and Learned Actor all make legal deterministic cleanup choices. Learned ranks
only its own/public successor states with its frozen model; it receives no
card-specific rule, Handcrafted value, or opponent hidden card identity.

The same authoritative choice is exposed by both human surfaces. Terminal
interactive mode prompts for the exact excess cards. The web bridge publishes
`cleanup_discard` with zero-based hand indices and accepts only an exact,
unique integer array for the matching decision id; negative, quoted,
fractional, duplicate, short, long, and junk-suffixed inputs fail before state
mutation. Its resulting `cards_discarded` event is public. A separate opt-in
Bluff mode may expose otherwise forced single-Pass priority choices without
changing bot defaults or nontrivial-decision statistics.

Because cleanup changes real trajectories, the engine schema and all learned
artifact families advanced from v2 to v3. The G0/C2 golden model fingerprints
were regenerated under the new schema, and the exact 300,000-game Random
matrix at seed `303` was pinned to the following matchup rates:
Green–Red `61.7%`, Green–Blue `56.4%`, Green–White `54.7%`,
Green–RU `67.6%`, Red–Blue `48.9%`, Red–White `54.9%`, Red–RU
`42.0%`, Blue–White `90.1%`, Blue–RU `48.8%`, and White–RU
`38.6%`.

Verification:

- `make test` exited zero: 125/125 engine/bot tests, 16 learned-iteration
  tests, 22 probe tests, 11 probe-metric tests, 24 probe-runner tests, 7 web
  bridge tests, the complete CLI lifecycle, and 61/61 Node/browser-contract
  tests passed.
- The engine suite passed 125/125 under the exact sanitizer command
  `c++ -Iinclude -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror
  -fsanitize=address,undefined -fno-omit-frame-pointer src/game.cpp
  src/interactive.cpp src/learned_iteration.cpp tests/test_game.cpp -o
  build/old-school-tests-sanitize`, followed by
  `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
  ./build/old-school-tests-sanitize`. There were no ASan/UBSan findings;
  Apple's ASan runtime requires leak detection to remain disabled.
- `git diff --check` was clean.
- The README command
  `./build/old-school-sim --games 100 --seed 42 --bots mixed --rollouts 2
  --deep-rollouts 8 --learned-rollouts 2 --train-games 800 --train-seed
  424242` completed 1,000 games with no draws or turn-limit finishes. The
  five deck records were Green `179-221`, Red `147-253`, Blue `278-122`,
  White `218-182`, and RU Aggro `178-222`. Learned G0 went `280-120`
  overall, but its lift gate passed only White and failed Blue, RU, Red, and
  Green. This is a small, unpaired user-facing smoke, not a bot-strength
  conclusion.

Decision: **accept Environment v3 as live**. All rules and verification gates
passed. Do not reuse any v2 artifact, probe-cache fingerprint, lift table, or
strength result as v3 evidence. The next required artifact step is a fresh,
fingerprint-pinned v3 C16 retrain before any new policy experiment.

Review reconciliation: the 00:35 corpus qualification report proposed 7 of 20
legacy dev-v3 fixtures as outcome-relevant. A subsequent code audit found that
the reviewer-branch qualifier always resumes `FirstMain`, resets the recorded
pass count, uses the pre-v3 raw cleanup path, and discards paired per-world
outcomes. Therefore the seven are preregistered hypotheses, not yet
Environment-v3 ground truth; they must reproduce through the corrected v3
qualification seam. The other thirteen, including every current White lore
fixture and both Force Spike controls, remain descriptive only. A future probe
corpus must qualify candidate branches by strong-pilot outcome separation
before assigning an acceptance label. The X=0 no-op class is the first
candidate post-v3 defect target, contingent on that corrected qualification.

### Environment v3 C16 control artifact (declared)

Declared on 2026-07-26 after the Environment v3 correctness gate passed and
before starting a fresh post-reset training run.

Hypothesis: the unchanged card-agnostic 16-generation Value Challenger recipe
will train deterministically under Environment v3, publish a schema-bound
artifact distinct from every v2 model, and reload with the exact same
fingerprint and predictions. This experiment freezes the new research parent;
its small gameplay tail is an artifact smoke and cannot establish strength.

The exact generation command is:

```sh
./build/old-school-sim --benchmark --games 1 --seed 919190 \
  --challenger learned-value-c16 --baseline learned-value-g0 \
  --learned-rollouts 8 --train-games 800 --train-seed 424242 \
  --refresh-value-challenger-cache
```

It must write only
`build/model-cache/old-school-value-challenger-v3-c16-t800-s424242.bin`,
report the complete fingerprint, and complete the balanced 60-paired-game
smoke. A second invocation without `--refresh-value-challenger-cache` must
load that artifact and report the identical fingerprint. Any schema mismatch,
cross-load, nondeterministic fingerprint, incomplete run, or test regression
rejects the artifact. The 60-game aggregate and five 12-game deck slices are
reported for reproducibility but are explicitly below the measurement floor
and will not select, reject, or promote a policy.

### Environment v3 C16 control artifact (result: accepted)

Recorded on 2026-07-26 after rereading the independent review timestamped
00:58 PDT. The exact preregistered refresh command trained C16 in 298.15
seconds and atomically published:

- artifact:
  `build/model-cache/old-school-value-challenger-v3-c16-t800-s424242.bin`;
- fingerprint:
  `68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`;
- training seed `424242`, initial games `800`, generations `16`.

The command then trained the Environment-v3 G0 comparator in 20.50 seconds
with fingerprint
`ab7a782478d9dbafe7bfd3242a2434b24f9b9fb6a0f3f83b8c406e3818566f78`
and completed all 60 paired smoke games at evaluation seed `919190`. C16 went
`30-30` (`50.0%`, approximate interval `37.7%--62.3%`), with challenger-deck
records Green `3-9`, Red `6-6`, Blue `9-3`, White `5-7`, and RU Aggro `7-5`.
The CLI exited one because the verdict was correctly inconclusive.

The exact same command without the refresh flag loaded the artifact in 0.01
seconds, reproduced the complete C16 and G0 fingerprints, and reproduced the
aggregate, all five deck slices, and efficiency values exactly. This satisfies
the declared deterministic generation/reload and schema-isolation gates.

Decision: **accept this fingerprint as the immutable Environment-v3 C16
research parent**. Accept no strength interpretation from the 60-game smoke.
Future Environment-v3 challenges must bind this exact fingerprint or declare
and freeze a different parent before collecting evidence.

Review disagreement: the 00:58 entry calls the reviewer branch's
`tools/certify.sh` a full certification panel. Read-only inspection shows that
it currently has only one virgin evaluation seed, omits the required fixed
`101,202,303,404,505,606,707,808` no-losing-seed panel, does not enforce
per-deck direct wins or parse the pooled Wilson lower bound above 50%, uses
one-seed lift rather than the seeded stability gate, omits sanitizers, and
scores unqualified dev-v3 probes. Its final `CERTIFIED` label can therefore
false-promote under `AGENTS.md`. Preserve the useful one-command concept, but
do not run or import that script as authoritative until every required gate,
qualification dependency, and collision-safe evidence log is implemented.
More critically, its stage-three lift command omits
`--learned-generations 16` and therefore evaluates legacy Value G0, while
stage four explicitly scores G0; it can print `CERTIFIED` for the wrong model.
It also accepts `gate-games < 34`, can silently train during evaluation, cannot
prove a virgin evaluation seed, and returns shell status zero for
`NOT CERTIFIED`. Stage two at 34 games does correctly combine the current
CLI's Wilson-lower-bound and all-deck direct-win gates; that narrow piece is
sound but does not repair the panel.

### DC1 immediate resource-dominance mining audit (declared)

Declared on 2026-07-26 after rereading the independent review timestamped
01:20 PDT and freezing exact Environment-v3 C16 parent fingerprint
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.

Hypothesis: an evaluation-only, card-agnostic comparator can find a useful,
deck-balanced supply of strictly dominated Priority actions in real frozen-C16
self-play without assigning preferences to genuine resource tradeoffs. This
is a mining sufficiency audit only. It trains no weights, filters no deployed
action, changes no policy, and cannot promote a model.

For a candidate root action, canonical settlement forces that action and then
only passes priority until the current stack has resolved or the priority
window ends. Rules-authoritative optional payments still occur. Settlement
stops on the empty stack before any new phase, combat, cleanup, draw, or
controller decision; natural terminal life/deck outcomes are retained. Every
branch records an explicit cost ledger relative to the common root.

Two settled branches may be ordered only when their non-cost effects are
exactly equal after normalizing their branch-local costs. Exact comparison
includes life, battlefield, stack, libraries, public zones, hand sizes, turn
state, extra turns, and failed-draw state; irrelevant statistics and next-ID
counters are normalized. Resource coordinates are factual and unweighted:
own-hand card identities consumed, mana paid, preexisting mana sources newly
tapped, land-play entitlement consumed, and opponent public/count resources
consumed in the opposite direction. Action A dominates B only if A consumes
no more actor resource on every coordinate, consumes no fewer opponent
resource on every coordinate, and at least one coordinate is strict. No card
name, power sum, permanent value, damage score, mana-to-card exchange rate, or
Handcrafted value is permitted. Thus forcing a payable Force Spike tax is
explicitly incomparable: it trades the actor's card/mana for opponent mana.

Dominance must have one unanimous orientation across `K=8` common
information-set worlds. Repartitioning/reordering opponent hidden zones must
reproduce byte-identical sampled-world keys, action descriptors, effect
fingerprints, ledgers, orientations, and example weights. Disagreement in one
world makes the pair incomparable.

The mining command is fixed:

```sh
./build/old-school-sim --audit-dc1-dominance \
  --train-games 800 --train-seed 424242 \
  --learned-generations 16
```

It loads, never refreshes, the pinned C16 parent. Training-mining seed is
`577215`; held-out seed is `271828`. Each split contains two exact balanced
40-game blocks, for 160 games and 320 seat-games overall. Training trajectories
use the frozen parent with deterministic indexed 10% legal-action exploration;
held-out trajectories use deployed C16 at `K=8`, epsilon zero. The collector
retains at most 16 evenly spaced multi-action Priority roots per seat-game,
including exact root state, all legal actions, phase, priority player,
consecutive-pass count, sorcery permission, ordered decks, and stable decision
ordinal. More than 64 legal actions fails closed. The full legal set is never
truncated; at most eight unordered pairs per root are selected by stable hash.

Accounting is bounded and printed compactly: at most 5,120 roots, 40,960 pair
groups, 327,680 paired-world cells, and 2,621,440 total settlement operations.
Examples are deduplicated by information-set fingerprint plus ordered action
descriptors. Per deck, training must contain at least 32 strict positives and
32 deterministically matched incomparable controls, each drawn from at least
8 distinct seat-games. Held-out must contain at least 16 of each across at
least 4 seat-games. Any deck miss rejects the audit; counts may not be repaired
by oversampling a fixture, broadening the comparator, or changing thresholds
after observation.

Before mining, fixture gates must prove:

1. Pass strictly dominates Disintegrate X=0 in the validation-v1 no-op state;
2. payable Force Spike is incomparable;
3. live Force Spike, lethal Disintegrate, useful Giant Growth, and productive
   land, creature, and artifact development do not trigger dominance merely
   because their outcomes/resources differ;
4. malformed roots fail before mutation, settlements preserve card
   conservation, hidden repartition is exact, and candidate order cannot
   change descriptor-keyed results.

The reported 7-of-20 reviewer outcome fixtures are not inputs or labels for
DC1; their corrected Environment-v3 qualification is separate and remains
mandatory before any later policy change. If the density audit passes, the
next experiment must separately preregister either an exact dominance filter
or a pairwise Priority residual fit. A critic-wide target is not licensed by
this audit because calibration bleed could alter unrelated actions.

Pre-run implementation reconciliation, recorded before executing the command:

- The independent review's 02:10 working-tree lift table (4/5 decks, with
  Blue shown at -10.0 points) is a useful smoke diagnostic only. It reports
  80 games per cell (roughly +/-11 points), no complete reproducible command
  in the entry, and is not the paired 2,000-game/panel gate. It is neither a
  strength conclusion nor an input to DC1.
- The reviewer's current fixture qualifier and certification harness have
  unresolved source-binding, pairing, and false-pass defects. Consequently,
  neither its 7-of-20 qualification nor its Blue-fixture claims are accepted
  as DC1 labels or evidence. DC1 uses only its independently tested,
  rules-exact resource comparator.
- "Indexed exploration" in the declaration means that every mining game has
  a seed derived from the fixed split/block/schedule index. Within a game,
  the existing engine's deterministic RNG supplies the 10% exploration
  decisions; there is no new per-decision RNG stream. This is reproducible
  for the frozen binary, but intentionally does not claim invariance to
  unrelated future RNG-consumption changes.
- The earlier operation ceiling was arithmetically doubled. With 5,120 roots
  across both splits, eight pairs/root, eight common worlds/pair, and four
  raw settlements/world (two candidates plus their hidden clones), the exact
  ceiling is 1,310,720 settlement operations, not 2,621,440. No sampling or
  density threshold changed.
- The CLI now fails closed unless the exact pinned C16 artifact already
  exists. It cannot silently train, refresh, write, filter, or deploy a model.
  Comparator proof coverage is 29/29, including exact X=0 and two-sided Force
  Spike ledgers, priority-stop context, candidate-orientation reversal, and
  exact Mox Sapphire/Sol Ring payment ledgers (including Sol Ring excess pool
  clearing).

### DC1 immediate resource-dominance mining audit (result: rejected before density)

Run on 2026-07-26 after rereading the independent review through 04:05 PDT.
The exact command was:

```sh
./build/old-school-sim --audit-dc1-dominance \
  --train-games 800 --train-seed 424242 \
  --learned-generations 16
```

The load-only route loaded the pinned Environment-v3 C16 artifact and
reproduced exact model fingerprint
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.
Artifact loading self-reported 0.01 seconds; the complete failed invocation
took approximately 48.0 seconds of observed tool wall time. It then exited
with status 2 and:

```text
error: DC1 root exceeds legal-action bound
```

Result: **rejected before density**. The preregistered `>64` legal-action
guard failed closed. No train/held-out split completed, so there are no
aggregate or per-deck density counts to report, no positive/control examples
were accepted, and no treatment is licensed. The model was not trained,
refreshed, written, filtered, or deployed.

The thrown context retained only the error text above; it did not retain the
split, block, game, seat, deck, turn, phase, legal-action count, or descriptor
set. Those facts cannot be reconstructed without new instrumentation and a
rerun, neither of which is part of this rejected experiment.

This is an implementation-bound failure, not evidence for or against the
dominance hypothesis. The next experiment may not silently raise the bound.
First add an evaluation-only diagnostic that reports the offending fixed
split/block/schedule/seat/root, exact action count, and action-kind histogram,
then predeclare a finite replacement bound from that evidence while preserving
the complete legal action set and the original pair/root, world, density, and
all-five-deck thresholds. Rerunning density requires a separately named
declaration. The review's 03:55 Blue loss-state harvest is v2 and its current
qualifier remains disputed; it is a follow-up hypothesis, not evidence that
changes this rejection.

### DC1-B0 legal-action census (declared)

Declared on 2026-07-26 after rereading the independent review through
04:50 PDT and receiving a read-only collector audit. This is diagnostic
instrumentation, not a strength or density experiment. It fits, filters,
writes, and deploys nothing.

The collector audit found one pre-density correctness defect that must be
fixed before any later DC1 density result: duplicate observations were
deduplicated by `(information-set/action-pair key, observed label)`, so the
same exact pair could count once as a positive and once as an incomparable
control when finite-world labels conflicted. The corrected rule groups only
by the information-set/action-pair key and drops the entire key on any label
conflict. Conflict counts must be printed. Pair-world seeds must derive from
the split seed plus that same descriptor-keyed identity, not trajectory
provenance or retained-pair ordinal. Accounting must additionally enforce
per-deck seat balance and all root/pair/cell/settlement sums and caps.

The audit also found an undeclared implementation default:
`max_game_turns=128`. DC1-B0 explicitly freezes that 128-turn mining horizon.
It does not claim equivalence to the normal 500-turn game limit.

Hypothesis: the exact frozen-C16, all-five-deck DC1 trajectories contain a
finite, reproducible maximum legal Priority-action count above 64 but no more
than 512. A complete census will identify every over-64 root and provide a
non-post-hoc finite bound for a separately declared density rerun.

The command is:

```sh
./build/old-school-sim --audit-dc1-action-census \
  --train-games 800 --train-seed 424242 \
  --learned-generations 16
```

It must load exact Environment-v3 C16 fingerprint
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.
It replays the unchanged training-mining seed `577215` and held-out seed
`271828`, two exact balanced 40-game blocks per split, K=8 frozen-C16
self-play, 10% training-split exploration, zero held-out exploration, and
`max_turns=128`. It traverses every captured Priority root and enumerates the
complete legal action set, but performs no pair comparison, deduplication,
density counting, training, or policy mutation.

For every root with more than 64 actions, output must include split, block,
schedule index, seat, both public deck IDs, turn, phase, consecutive-pass
count, stack size, exact legal-action count, action-kind histogram, and a
digest of sorted complete action descriptors. It must also report exact
global and per-deck maxima and legal-count histograms, plus:

- 80 games, 160 seat-games, and exactly 32 seat-games per deck in each split;
- deck/root totals that cross-sum exactly;
- distinct fixed split seeds;
- a statement that no pair settlements or density examples were evaluated.

Run the command twice. Acceptance requires byte-identical scientific output
(timing, if printed separately, is excluded), exact accounting, the pinned
fingerprint, at least one reproduced over-64 root, and a maximum no greater
than the preregistered diagnostic ceiling of 512. If accepted, the next
separately named density declaration will use the exact observed maximum as
`max_legal_actions`; it may not round upward or change the original
root/pair/K/density thresholds. If the maximum exceeds 512, or either replay
differs, DC1-B0 is rejected and density remains unlicensed.

Review reconciliation: the 04:50 entry endorses this bound census. Its
proposed dual-pilot v3 Blue qualifier is a separate future experiment. The
review's v2 Blue labels, and its current certification claims, remain outside
DC1-B0.

### DC1-B0 legal-action census (result: accepted diagnostic; exact bound 90)

Run on 2026-07-26 after rereading the independent review through 06:20 PDT.
The 06:20 dual-pilot Blue result is useful separate evidence, but it neither
changes this rules-only census nor supplies DC1 labels. The exact command was
run twice:

```sh
./build/old-school-sim --audit-dc1-action-census \
  --train-games 800 --train-seed 424242 \
  --learned-generations 16
```

Both invocations loaded, without refreshing, exact Environment-v3 C16
fingerprint
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`
and exited 0. The complete outputs were byte-identical: each was 3,517 bytes
over 32 lines with SHA-256
`05094a4284c375027efefe7b01a7d5a9707e1193008b1f4d9305a77ed5ba7b43`.
The scientific sections beginning at `DC1-B0 Legal-Action Census` were also
byte-identical with SHA-256
`c4cb2ddbb6d63599c9e56608d9502c7cbf0863f7a0dc78477b0d400232b74559`.
Observed wall times were approximately 147 and 148 seconds.

Exact split accounting:

- training seed `577215`: 80 games, 160 seat-games, 18,543 Priority roots,
  32 seat-games per deck, descriptor uniqueness PASS, accounting PASS;
- held-out seed `271828`: 80 games, 160 seat-games, 18,046 Priority roots,
  32 seat-games per deck, descriptor uniqueness PASS, accounting PASS;
- pair comparisons `0`, density examples `0`, distinct split seeds PASS, and
  cross-split accounting PASS.

Per-deck root counts and exact maxima were:

| Split | Green | Red | Blue | White | RU Aggro |
| --- | ---: | ---: | ---: | ---: | ---: |
| Training roots / max | 3,612 / 9 | 3,561 / 12 | 3,244 / 17 | 4,691 / 9 | 3,435 / 90 |
| Held-out roots / max | 3,577 / 9 | 3,397 / 11 | 3,029 / 12 | 4,529 / 9 | 3,514 / 52 |

The exact global training histogram was
`{1:14838, 2:2108, 3:944, 4:238, 5:176, 6:76, 7:77, 8:32, 9:21, 10:9, 11:6, 12:1, 13:2, 14:1, 15:3, 17:2, 20:1, 22:1, 26:1, 32:1, 35:1, 37:1, 42:1, 80:1, 90:1}`.
The exact held-out histogram was
`{1:14480, 2:1942, 3:939, 4:250, 5:178, 6:77, 7:91, 8:34, 9:22, 10:9, 11:10, 12:1, 13:2, 14:1, 15:3, 17:1, 23:1, 27:1, 31:2, 33:1, 52:1}`.

Exactly two roots exceeded 64 actions, both in training block 1, schedule 39,
seat 0, RU Aggro versus White, turn 26 first main with zero prior passes and
an empty stack:

- trace root 301: 80 actions = Pass 1, Play Land 1, Cast Creature 1, and
  Disintegrate 77; sorted-descriptor FNV-1a
  `45e87156943666fe`;
- trace root 302: 90 actions = Pass 1, Cast Creature 1, and Disintegrate 88;
  sorted-descriptor FNV-1a `0f1f4432ae3f8a05`.

Result: **accepted as a diagnostic**. It confirms the hypothesis
`64 < maximum <= 512` and supplies the evidence-bound replacement
`max_legal_actions=90`. It does not show density, improve a bot, or license a
policy treatment. No model was trained, written, filtered, or deployed.

### DC1-B1 exact-bound resource-dominance density rerun (declared)

Declared on 2026-07-26 after accepting DC1-B0 and rereading the independent
review through 06:20 PDT. This reruns the previously rejected DC1 density
audit with exactly one evidence-required configuration change:
`max_legal_actions` increases from 64 to the observed maximum 90. It is not
rounded upward. The comparator, complete action sets, retained-root and pair
limits, common-world count, split seeds, exploration, 128-turn horizon,
density thresholds, hidden-information checks, and frozen parent remain
unchanged.

Hypothesis: with the exact bound 90, the fixed all-five-deck mining replay will
complete its accounting and hidden-repartition gates and meet, for every
deck, at least 32 strict positives plus 32 matched incomparable controls from
at least 8 seat-games in training, and at least 16 of each from at least 4
seat-games in held-out.

The exact command is unchanged:

```sh
./build/old-school-sim --audit-dc1-dominance \
  --train-games 800 --train-seed 424242 \
  --learned-generations 16
```

It must load, never refresh, exact C16 fingerprint
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.
The CLI report must print `max_legal_actions=90`, conflict-dropped key counts,
exact global/per-deck root, pair, world-cell, and settlement cross-sums, and
all density/seat-game counts. The prior exact ceilings remain: no more than
5,120 retained roots, 40,960 pair groups, 327,680 paired-world cells, and
1,310,720 raw settlement operations across both splits. Any bound excess,
accounting mismatch, descriptor/world-seed invariance failure, fingerprint
mismatch, or per-deck density miss rejects B1. No threshold may be repaired
after observation.

If B1 passes, it licenses only a separately preregistered narrow treatment:
either an exact dominance filter or a pairwise Priority residual fit. It does
not license critic-wide targets or a Learned-is-king claim. If it fails,
record the exact all-five-deck density shortfall and move to the separately
qualified v3 Blue stack-response corpus rather than weakening DC1.

### DC1-B1 exact-bound resource-dominance density rerun (result: rejected)

Run once on 2026-07-26 after rereading the independent review through
00:21 PDT. That review independently reproduced DC1-B0 bit-for-bit and
predicted that per-deck density, rather than the corrected bound, was B1's
real risk. The only configured audit change was the declared exact bound
`max_legal_actions=90`; all other seeds, limits, thresholds, and comparator
semantics remained frozen. The exact command was:

```sh
./build/old-school-sim --audit-dc1-dominance \
  --train-games 800 --train-seed 424242 \
  --learned-generations 16
```

The route loaded, without training or refreshing, exact Environment-v3 C16
fingerprint
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.
It printed `K=8`, `max_legal_actions=90`, `max_turns=128`, 16 retained
roots/seat-game, and 8 pairs/root. The fixture, hidden-repartition, and exact
accounting gates all passed. The process exited 1 with the intended scientific
verdict `REJECT (density insufficient; no treatment)`. Audit time was
159.657089958 seconds and observed wall time was 159.87 seconds. The complete
capture SHA-256 was
`1eca49d3f67285fe105e33af1ec07f294214b531bffd5904f793dba8e97bf8a2`.
There was no retry or alternate configuration.

Training split, seed `577215`:

- aggregate: 80 games, 160 seat-games, 18,543 Priority roots, 3,705
  multi-action roots, 2,448 retained roots, 6,346 pair groups, 50,768
  paired-world cells, and 203,072 settlements; hidden/accounting PASS;
- Green: 3,612 / 653 / 476 roots, 899 pairs, 6 positives, 893 unique
  incomparable, 6 matched controls, 0 conflicts, positive/control seat
  coverage 5/2 — FAIL;
- Red: 3,561 / 660 / 486 roots, 1,571 pairs, 0 positives, 1,571
  incomparable, 0 controls, 0 conflicts, coverage 0/0 — FAIL;
- Blue: 3,244 / 753 / 505 roots, 884 pairs, 66 positives, 818
  incomparable, 66 controls, 0 conflicts, coverage 25/4 — FAIL because
  control coverage was below 8 seat-games;
- White: 4,691 / 1,018 / 503 roots, 1,659 pairs, 2 positives, 1,655
  incomparable, 2 controls, 0 conflicts, coverage 2/2 — FAIL;
- RU Aggro: 3,435 / 621 / 478 roots, 1,333 pairs, 29 positives, 1,304
  incomparable, 29 controls, 0 conflicts, coverage 11/2 — FAIL.

Held-out split, seed `271828`:

- aggregate: 80 games, 160 seat-games, 18,046 Priority roots, 3,566
  multi-action roots, 2,473 retained roots, 6,883 pair groups, 55,064
  paired-world cells, and 220,256 settlements; hidden/accounting PASS;
- Green: 3,577 / 661 / 485 roots, 943 pairs, 8 positives, 935
  incomparable, 8 controls, 0 conflicts, coverage 6/2 — FAIL;
- Red: 3,397 / 605 / 482 roots, 1,648 pairs, 4 positives, 1,644
  incomparable, 4 controls, 0 conflicts, coverage 4/2 — FAIL;
- Blue: 3,029 / 669 / 505 roots, 1,010 pairs, 69 positives, 941
  incomparable, 69 controls, 0 conflicts, coverage 25/4 — PASS;
- White: 4,529 / 963 / 508 roots, 1,642 pairs, 4 positives, 1,638
  incomparable, 4 controls, 0 conflicts, coverage 4/2 — FAIL;
- RU Aggro: 3,514 / 668 / 493 roots, 1,640 pairs, 53 positives, 1,587
  incomparable, 53 controls, 0 conflicts, coverage 14/2 — FAIL because
  control coverage was below 4 seat-games.

Both splits cross-summed exactly to 36,589 Priority roots, 7,271
multi-action roots, 4,921 retained roots, 13,229 pairs, 105,832 world cells,
and 423,328 settlements. Those remain below every preregistered ceiling.
There were 241 positives, 12,986 unique incomparable pairs, 241 matched
controls, and zero conflicting exact-pair keys. Raw per-deck root counts
exactly reproduced DC1-B0.

Result: **rejected on all-five-deck density**. Immediate resource dominance is
real but too sparse and uneven for the declared general auxiliary target.
The evidence especially rules out repairing DC1 by merely increasing sample
count or weakening thresholds: Red had zero training positives, while most
decks lacked diverse controls. No dominance filter, residual, critic target,
model change, or deployment is licensed.

Next experiment: follow the preregistered fallback, not a DC1 retry. Define a
separate Environment-v3 diagnostic that harvests the frozen C16 Blue
stack-response decisions it actually loses, then measures its chosen-action
regret with a deeper hidden-information-safe Learned-mirror reference. The
review's earlier eight states establish a method and three dual-pilot-agreed
v2 candidates, but they are not silently promoted into Environment-v3
training labels. The v3 command, corpus balance, reference budget, and
acceptance threshold must be separately declared before implementation or
execution.

### Exact ordered-deck matrix instrumentation smoke (not a strength result)

Run on 2026-07-26 after rereading the independent review through 00:21 PDT.
This was a CLI/accounting smoke for the new certification evidence seam, not a
bot candidate screen. The exact command was:

```sh
./build/old-school-sim --stability --stability-runs 1 --games 1 --seed 1 \
  --train-games 1 --train-seed 424242 \
  --rollouts 1 --deep-rollouts 2 \
  --learned-generations 1 --learned-rollouts 1
```

It loaded the existing Environment-v3 C1 artifact with fingerprint
`5f499b27515b6a379a227e929f961803edce8991f321d885cff66633b736357d`.
Evaluation seed was `102`; each direct policy comparison contained 60 games.
Aggregate Learned records were 36-24-0 versus Random, 31-29-0 versus Monte
Carlo, 25-35-0 versus Deep Monte Carlo, and 9-51-0 versus Handcrafted.

The pooled Handcrafted section emitted all 25 exact
challenger-deck-by-baseline-deck cells from the challenger perspective.
Every diagonal contained 4 games and every off-diagonal contained 2, for 60
total. Reconstructing its row and reciprocal-column marginals gives the
printed all-five deck result:

| Learned deck | Learned wins | Handcrafted wins | Verdict |
| --- | ---: | ---: | --- |
| Green | 0 | 12 | FAIL |
| Red | 6 | 12 | FAIL |
| Blue | 1 | 10 | FAIL |
| White | 0 | 9 | FAIL |
| RU Aggro | 2 | 8 | FAIL |

The one-seed mixed-field lift rows were Green +5.0 versus +35.0 Handcrafted,
Red +50.0 versus +50.0 Handcrafted, Blue -15.0 versus +40.0 Handcrafted,
White -20.0 versus +55.0 Handcrafted, and RU Aggro +55.0 versus +65.0
Handcrafted. Only Red tied for best. At one repetition and a one-game-trained
C1, none of these win rates is a strength claim.

Instrumentation result: **accepted**. The exact 5x5 output has the intended
shape and reconstructs the aggregate and both deck marginals, closing the
false-pass class where impossible independent marginals could previously
satisfy the certification parser. Next verification is adversarial parser
coverage plus the full test suite; this smoke does not alter the frozen C16
champion or the next Blue stack-response research experiment.

### Blue harvest context correction (review reconciliation; no experiment)

Recorded after rereading the independent review's 00:28 PDT correction. The
earlier eight outcome-relevant states cannot be described as Blue mistakes:
exact callback context shows `decision_player=1` for all eight, so they were
opponent-held windows in games Blue eventually lost. Frozen C16 Blue did not
choose any of those hypothetical branches. Their outcome relevance and the
dual-pilot qualification method survive, but all three previously
pilot-agreed action labels are invalid as evidence about Blue's policy.

This corrects, without deleting, the B1 fallback wording above. There are
currently **zero qualified Blue-held mistakes**. A valid v3 harvest must:

- retain exact decision-player context and accept only Blue-held windows;
- admit two-action stack decisions, because pass-versus-counter binaries were
  structurally excluded by the prior three-action minimum;
- keep opponent-held windows as relevance-only observations, never Blue
  labels;
- treat a zero-qualified result as evidence for value miscalibration rather
  than inventing a discrete wrong-pick treatment.

No model, fixture label, or policy changed. The next Blue diagnostic must be
separately declared under these corrected constraints before it runs.

### DC1 signal geography (review reconciliation; no experiment)

Recorded after rereading the independent review through 00:40 PDT. The
DC1-B1 rejection closes immediate resource dominance as an all-deck auxiliary
target, but its distribution is a positive diagnostic result: Blue supplied
66 training and 69 held-out strict positives, while Red supplied 0 and 4.
The corresponding Blue control coverage was 25/4 seat-games. Thus
dominance-comparable action structure is concentrated in Blue even though the
declared balanced density gate failed. This does not license a DC1 treatment,
but it strengthens the case for inspecting Blue's exact pass-versus-response
decisions rather than treating B1 only as an undifferentiated failure.

The review's independent `harvest-v2` has since reported ten correctly owned
Blue windows, including one dual-pilot-qualified two-action decision with
32.5 percentage points of regret and two relevance-only wrong picks. That
result was observed before the declaration below, so it is treated as an
external prediction to reproduce, not as a blind discovery by this worktree.

### BSR0 Environment-v3 Blue-held stack-regret audit (declared)

Declared on 2026-07-26 after rereading `AGENTS.md`, the B1 result, and the
independent review through 00:40 PDT. BSR0 is a load-only diagnostic and an
independent replication of the review's corrected `harvest-v2`; it does not
train, refresh, write, or deploy a model and it cannot by itself license a
Learned-is-king claim.

Hypothesis: in an exactly balanced sample of 200 real games that frozen C16
Blue loses, a correctly owner-filtered harvest will retain 20 Blue-held
opponent-stack priority roots, including two-action windows, and a deeper
hidden-information-safe Learned-mirror reference will find at least one
stable chosen-action mistake with point regret at least `0.05` and a paired
95% lower confidence bound above zero. This is the minimum independent
replication criterion for the externally reported rare, high-cost binary
mistake. A stronger density criterion is also predeclared: at least 4 of 20
stable mistakes spanning at least two opponent-deck strata would license a
separately declared regret-weighted treatment experiment. Missing the
stronger criterion does not erase a replicated single mistake.

The exact command will be:

```sh
./build/old-school-sim --audit-v3-blue-stack-regret \
  --train-games 800 --train-seed 424242 \
  --learned-generations 16
```

It must load, without training or refreshing, exact Environment-v3 C16
fingerprint
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.
No alternate seed or retry is permitted after observing the result.

Source-state protocol:

- source seed `1618033`, 10 schedule blocks, all five opponent deck IDs;
- Blue is the tracked deck in both policy seats and on both play/draw sides,
  producing 20 games per block, 200 total, and 40 per opponent deck;
- source games use a 128-turn cap and production C16 Learned search
  (`K=8`, horizon 4) for the tracked Blue seat;
- Handcrafted may act only as the other seat to source real losing states
  from the measured matchup. Its score, action preference, features, hidden
  information, and continuation are never copied, queried, labeled, or used
  by Learned training or the reference. Snapshots are the only retained
  product. This is evaluation-state sourcing, not training against
  Handcrafted;
- an eligible root must come from a tracked-seat loss, have valid decision
  context with `decision_player` exactly equal to the tracked Blue seat, have
  a nonempty stack whose top object is controlled by the opponent, expose a
  complete Priority action set of size 2 through 512, and trace C16's actual
  selected action to exactly one stable action descriptor;
- retain at most one root per lost game: the lexicographically smallest
  stable information/action-set key. Retain the four smallest provenance
  keys per opponent deck, from four distinct games, for exactly 20 roots.
  Opponent-held windows are counted only as rejected relevance observations
  and can never become Blue labels.

Reference protocol:

- reference seed `1414213562`;
- two disjoint descriptor-keyed common-world passes, a 64-world scout and an
  independent 64-world confirmation, horizon 8, complete legal action set,
  frozen C16 Learned-mirror continuations for both seats, no shallow-prior
  blend, epsilon, or residual;
- the scout selects its argmax set. A root is a stable chosen-action mistake
  only if the independent confirmation has the same best-action set, C16's
  traced actual action is outside both best sets, the paired point estimate
  `Q(best)-Q(actual)` is at least `0.05`, and its paired 95% lower confidence
  bound is above zero;
- descriptor ordering cannot affect seeds or results, and a hidden clone that
  preserves public state, own hand, opponent hand size, and card-count
  multiset while repartitioning unknown cards must produce identical
  eligibility, worlds, and scores.

Bounds and required reporting:

- exactly 200 source games and no more than 20 retained roots;
- no more than 512 legal actions per root;
- no more than 2,621,440 reference settlements in the worst case;
- report exact source balance, losses, eligible/rejected ownership counts,
  retained provenance and action counts, scout/confirmation best sets,
  actual-action mapping, point regret, paired standard error/lower bound,
  per-opponent and aggregate mistake counts, hidden-clone result, every
  accounting cross-sum, elapsed time, process exit, model fingerprint, and
  complete-capture SHA-256.

Focused tests must cover exact owner retention, opponent-owner rejection,
two-action stack-window retention, traced-action legality and descriptor
mapping, five-opponent seat/play-draw balance, descriptor-order invariance,
hidden-clone invariance, disjoint scout/confirmation seeds, regret/lower-bound
math, exact accounting, and unchanged behavior when tracing is disabled.

Interpretation is fixed in advance. One or more stable mistakes meeting the
minimum criterion independently reproduces a discrete Blue decision defect.
Four or more across two opponent strata licenses only a separately
preregistered regret-weighted treatment. A zero result means no discrete
wrong pick was witnessed under this protocol and redirects the next
experiment to value calibration; thresholds will not be weakened. Any
fingerprint, isolation, invariance, bound, or accounting failure rejects the
audit as invalid rather than negative.

#### BSR0 pre-execution rare-error amendment

Amended before implementation completed and before any BSR0 execution, after
incorporating the 00:40 review's full rare-error result and the implementer's
independent design check. The broader `4 of 20 at 0.05` treatment criterion
above was poorly matched to the now-observed error class. The following
clauses supersede only BSR0's root-retention count and pass threshold; all
seeds, ownership/isolation rules, reference settings, invariance checks, and
reporting requirements remain unchanged.

- retain up to two lexicographically smallest eligible roots per source loss;
- require exactly eight retained roots per opponent-deck stratum, spanning at
  least four distinct source losses, for 40 roots total;
- BSR0's practical high-cost-error PASS requires at least one stable root
  whose confirmation regret is at least `0.20` and whose paired 95% lower
  confidence bound is above `0.10`;
- stable regrets from `0.05` through `0.199999...`, or a positive lower bound
  no greater than `0.10`, are still reported as diagnostic signal but make
  BSR0 inconclusive and license no treatment;
- the new worst-case bound is 5,242,880 rollout evaluations: 40 roots × 512
  actions × 128 scout-plus-confirm worlds × two exact original/hidden-clone
  passes.

This amendment explicitly uses the external 32.5-point finding as a
replication prior; it is not presented as blind preregistration. A PASS
licenses only a separately declared stack-root deployment diagnostic such as
bounded quiescence, not training, promotion, or a card-specific rule.

#### BSR0 pre-execution capture/reporting amendment

Added before any BSR0 execution. The one-shot invocation will use:

```sh
sh tools/run_bsr0_once.sh <absolute-new-output-prefix>
```

The wrapper executes the unchanged exact binary argv declared above and
creates three new, immutable artifacts: `<prefix>.complete.txt` contains the
combined simulator stdout/stderr plus POSIX elapsed/user/system timing,
`<prefix>.exit.txt` contains the simulator's actual process exit, and
`<prefix>.sha256.txt` contains the complete-capture SHA-256. It refuses to
overwrite any of those paths, preventing an accidental retry at the same
declared prefix. After the sole run, the notebook will record the capture's
exact byte count, line count, SHA-256, and actual process exit along with its
scientific output. This is reporting infrastructure only: no source/reference
seed, model, schedule, search configuration, retention rule, bound, threshold,
or interpretation changed.

#### BSR0 result — invalid because the fixed retention design was infeasible

Run once on 2026-07-26 after committing the exact Environment-v3 source as
`48b8709219cd02adb0b8c663b7240935761a328d`. Before recording this result I
reread the independent review through its 01:41 PDT entry. No alternate seed,
replacement prefix, retry, or post-result threshold change was used.

Exact invocation:

```sh
sh tools/run_bsr0_once.sh \
  /Users/andrewjohnson/proj/magic-ai-vibes/build/experiments/bsr0-env-v3-c16-48b8709-20260726T014524-0700
```

The wrapper executed the exact preregistered simulator argv:

```sh
./build/old-school-sim --audit-v3-blue-stack-regret \
  --train-games 800 --train-seed 424242 \
  --learned-generations 16
```

The load-only check passed and loaded fingerprint
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.
The source and reference configurations also matched the declaration exactly:
source seed `1618033`, 10 blocks, 200 games, 128-turn cap, production K8/H4;
reference seed `1414213562`, disjoint K64 scout plus K64 confirmation, H8,
frozen Learned-mirror continuations, and four threads.

Source result:

| Opponent | Games | Blue losses | Eligible roots | Eligible-loss games | Retained roots / distinct losses |
| --- | ---: | ---: | ---: | ---: | ---: |
| Green | 40 | 13 | 36 | 12 | 8 / 4 |
| Red | 40 | 13 | 69 | 13 | 8 / 4 |
| Blue | 40 | 19 | 98 | 18 | 8 / 4 |
| White | 40 | 0 | 0 | 0 | 0 / 0 |
| RU Aggro | 40 | 15 | 63 | 15 | 8 / 4 |
| **Total** | **200** | **60** | **266** | **58** | **32 / 16** |

There were no draws or turn-limit draws. The trace contained 43,776 priority
roots, including 1,919 correctly owned Blue-held/opponent-top roots and 1,920
rejected opponent-held/opponent-top roots. All opponent-stratum/aggregate
cross-sums matched. The 32 retained roots used 20,736 reference evaluations
(5,158 terminal and 15,578 bootstrap), below the declared 5,242,880 maximum.
Traced-action, descriptor-order, hidden-clone, disjoint-seed, accounting, and
bound checks all passed.

The preregistered retention check failed. Frozen C16 Blue went 40-0 against
Handcrafted White in this exact source schedule, so the requirement for eight
roots from at least four Blue losses in **every** opponent stratum was
mathematically impossible. The harness therefore reported:

```text
Checks: source-balance=PASS, retention=FAIL, traced-actions=PASS,
descriptor-order=PASS, hidden=PASS, split-seeds=PASS, accounting=PASS,
bounds=PASS
Audit validity: INVALID
Minimum diagnostic replication: NOT FOUND
BSR0 practical high-cost verdict: INCONCLUSIVE
```

The scored-but-incomplete 32-root sample contained zero `>=0.05/lower>0`
diagnostic mistakes and zero `>=0.20/lower>0.10` practical mistakes. Those
zeros are reported for completeness but are **not** accepted as evidence
against the hypothesis because the audit failed its fixed retention gate.
This is a design failure, not a negative scientific result. In particular,
the all-five exact-loss-stratum requirement failed to account for the
possibility that a tracked matchup would supply no losses.

The simulator's intended and actual process exit was `1`. Internal audit time
was 93.716979625 seconds; the complete POSIX capture recorded `real 93.75`,
`user 115.77`, and `sys 0.56` seconds. The immutable complete capture has 219
lines, 33,859 bytes, and SHA-256
`7e4b45381261df017ed93b47b146c903ec2bdc6ecac1927b474c070539896474`.
Its three artifacts are:

```text
build/experiments/bsr0-env-v3-c16-48b8709-20260726T014524-0700.complete.txt
build/experiments/bsr0-env-v3-c16-48b8709-20260726T014524-0700.exit.txt
build/experiments/bsr0-env-v3-c16-48b8709-20260726T014524-0700.sha256.txt
```

Decision: **BSR0 invalid; accept no mechanism claim and do not retry it.**
The next experiment is the already planned frozen Environment-v3
certification panel on this committed tree. Its pooled all-five-deck result
will establish the current deficit shape before a separately preregistered
general training-strength challenger. Any future stack-regret audit must
predeclare a loss-conditioned or feasibility-aware retention rule rather
than requiring losses from every matchup.

### Environment-v3 C16 first certification panel (declared)

Declared on 2026-07-26 after committing the BSR0 invalid result and rereading
the independent review through 01:41 PDT. This is the first certification run
of the committed Environment-v3 C16 control. It is evaluation only: it must
load the existing artifact, may not refresh or retrain it, and binds exact
fingerprint
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.

Hypothesis: frozen C16 will pass the 2,040-game primary direct benchmark
against Handcrafted at fresh seed `11235813`: aggregate win rate above 50%,
two-sided Wilson 95% lower bound above 50%, and more direct wins for C16 on
each of Green, Red, Blue, White, and RU Aggro. Conditional on that primary
gate, the full fixed-seed panel is expected to **reject** certification on
the all-five mixed-field lift criterion, because the independent two-seed
preview currently shows only Green ahead and small deficits on the other
four decks. This makes the run falsifiable in both directions: clearing the
full panel would overturn the preview, while primary or panel rejection fixes
the exact current baseline before changing training.

Exact command:

```sh
sh tools/certify.sh 11235813 \
  68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f
```

Seed `11235813` was selected and searched in the repository before execution;
it is distinct from training seed `424242`, artifact-smoke seed `919190`,
fixed validation seeds `101,202,303,404,505,606,707,808`, and every existing
certification claim. There was no `certification-runs` directory before this
declaration. No alternate primary seed or retry is allowed after observing
scientific output.

The committed harness fixes:

- 34 balanced repetitions / 2,040 primary paired games, 408 games per
  challenger deck, with a predeclared three-percentage-point effect and exact
  binomial/Wilson power contract;
- if primary passes, five repetitions at each of the eight fixed evaluation
  seeds, 2,400 pooled direct games, 480 per challenger deck, plus 640
  mixed-field games per deck-policy cell;
- exact all-five direct and mixed-lift gates, pooled Wilson lower bound, and
  no losing validation seed;
- a clean committed source archive, collision-safe seed claim and evidence
  directory, exact artifact fingerprint/byte checks before and after
  evaluation, full tests, and ASan/UBSan;
- shell exit `0` only for full certification, `1` for a valid scientific
  rejection, and `2+` for incomplete infrastructure or evidence.

If the primary gate rejects, the harness intentionally stops and the fixed
panel remains unobserved; that is a valid result, not permission to choose a
different seed. If the run reaches and rejects the fixed panel, its pooled
all-five tables become the control for the next separately declared
general-training-strength challenger.

### Environment-v3 C16 first certification panel (result: infrastructure incomplete)

Run once on 2026-07-26 from committed source
`fb34908392e5cb8ddb09f0f3957ee1a4514b5bb2` after rereading the independent
review through 01:48 PDT. The exact declared command was used:

```sh
sh tools/certify.sh 11235813 \
  68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f
```

Result: **infrastructure incomplete, exit 2; no gameplay or scientific gate
ran**. The harness permanently claimed primary seed `11235813`, archived the
exact committed source, bound the expected C16 artifact SHA-256
`53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca`,
accepted only the exact external ` M REVIEW.md` worktree status, passed its
40 certification parser/integrity self-tests, and began the forced
archived-tree `make test`. It then failed in `npm --prefix web ci
--ignore-scripts`.

The npm debug log establishes the infrastructure cause: the harness forced a
fresh isolated npm cache, that cache contained package metadata but not the
required tarballs, and sandboxed DNS returned `ENOTFOUND` for
`registry.npmjs.org` through all three attempts. npm 11.7.0 surfaced this as
`Exit handler never called!`; Make exited 2. The failed make stage took
186.03384953923523 seconds. No model was loaded by the runtime simulator, no
primary game was played, and every scientific criterion remains `null`.

Evidence:

- run directory:
  `certification-runs/runs/20260726T085410Z-fb34908392e5-s11235813-p16274`;
- `03-make-test.log`: 14 lines, 1,783 bytes, SHA-256
  `83aac42fcda24bba1d113e34c502c46869574fb20ec3c13476441abeba70dd6c`;
- completed `report.json` SHA-256
  `c6532ff8f1b3b8355f1f2c23b12a4c86cb888bf17ebb81f301e75ca729454f88`.

The harness's own power calculation also records a useful metrology
disclosure: 2,040 games provide 77.0656% power at a true 53% win rate; 80%
power corresponds to about a 3.111-point effect. This still satisfies the
repository's explicit at-least-2,000-games rule for claims about a roughly
three-point effect, but future prose must not call it literal 3.00-point/80%
power.

Decision: consume seed `11235813` and make no bot-strength inference. Do not
delete or reuse its seed claim and do not resume the partially created run.
Next, repair and test dependency provisioning so archived-tree verification
does not depend on an empty network-isolated cache. Only then preregister a
new primary seed and run a new certification attempt; the replacement must
remain load-only and retain every scientific gate.

#### Certification dependency repair (accepted)

Implemented after the incomplete seed-`11235813` attempt and before any
replacement certification declaration. The defect was the harness's forced
empty cache, not the project lockfile or web build. The repaired runner:

- asks the pinned npm executable for its preexisting cache path and requires
  one existing absolute directory;
- pins that exact path in the archived-test environment;
- sets `NPM_CONFIG_OFFLINE=true`, so missing content fails immediately and
  cannot silently fetch from the network;
- keeps audit, funding, and lifecycle scripts disabled;
- records the cache path, offline contract, package-lock SHA-256, and
  no-network-fallback fact in `report.json`.

Validation:

```sh
npm ci --ignore-scripts --offline --audit=false --fund=false
# in a fresh temporary directory containing only web/package.json and
# web/package-lock.json
```

completed in 1.1031 seconds and installed 41 packages from the existing
content-addressed cache. The focused certification suite then passed 42/42
tests, including new regressions for strict offline environment construction,
replacement of an empty cache override, and rejection of a relative cache
path:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tests/test_certify.py
```

`python3 -m py_compile tools/certify.py tests/test_certify.py` and
`git diff --check` also passed. Decision: **accept the infrastructure fix**.
It changes no model, seed schedule, sample size, parser, or scientific gate.

Post-commit integration then archived exact source `69c8b2e` into a fresh
temporary directory and ran the certification-equivalent environment:

```sh
env CXX=/usr/bin/c++ \
  NPM_CONFIG_AUDIT=false NPM_CONFIG_FUND=false \
  NPM_CONFIG_IGNORE_SCRIPTS=true NPM_CONFIG_OFFLINE=true \
  NPM_CONFIG_CACHE=/Users/andrewjohnson/.npm \
  PYTHONDONTWRITEBYTECODE=1 make -B test
```

Offline `npm ci` installed all 41 locked packages in 712 ms. The complete
fresh-archive gate passed: 126 engine, 16 learned-iteration, 40 probe, 11
probe-metric, 24 probe-runner, 9 web-bridge, CLI, capture-once, 82 web, and
42 certification tests. This closes the exact failure path rather than only
the pure environment-builder seam.

Review reconciliation after the fix: the independent 02:03 PDT review
reported the first at-scale committed-v3 control result from its separate
harness: frozen C16 went `970-1070-0` (`47.5%`, 95% interval
`45.4%--49.7%`) against Handcrafted at virgin seed `9317` over 2,040 games.
Its three-seed pooled mixed-field view passed only Red (`+0.4` points) and RU
Aggro (`+3.3`), while Green (`-2.4`), White (`-4.6`), and Blue (`-7.1`)
failed. Treat this as independently generated external evidence, not as a
result from the incomplete local certification. It nevertheless becomes the
best current headline: Environment-v3 C16 is below Handcrafted, and its lift
gap is diffuse rather than a Blue-only defect. Historical 53.5%/55.1%
results remain valid only for their older environment versions.

The review has independently started a clean committed-v3 retrain and
reserved seed `6733`; do not collide with that seed. Before preregistering a
replacement local certification, compare that retrain's fingerprint with the
current artifact. If it differs, the committed-source retrain becomes the
required new frozen control. If it is bit-identical, proceed from the current
artifact with a fresh, noncolliding primary seed.

Review reconciliation at 02:17 PDT (no local experiment): the committed-v3
retrain reproduced fingerprint `68126afc...` bit-for-bit and a second virgin
2,040-game control scored 46.8% (95% interval `44.6%--48.9%`) at seed `6733`.
Thus the existing artifact is already the exact committed-source retrain;
same-seed retraining cannot improve it.

The review's preregistered DP-0 attribution also rejected cleanup discard as
the cause. Delegating only Learned's cleanup choices to Handcrafted's
heuristic scored 45.3% (95% interval `43.2%--47.5%`) at virgin seed `5077`,
within noise of and directionally below the 46.8% control. The Red and RU
mirror deficits persisted. This Handcrafted surface swap was evaluation-only
and is neither training data nor a permitted Learned policy change.

Decision: do not build a discard-policy treatment from DP-0. Together with
the rejected shallow-blend, rollout-world, and stack-horizon treatments, this
exonerates four deployment-side explanations. The next challenger must
change clean Learned self-play/training quality, remain balanced across all
five decks, and use the especially weak Red/RU mirrors only as sensitive
diagnostics rather than as card- or deck-specific training inputs. Await the
already-preregistered VC-1 self-consistent-realization result to choose
between value-target quality and richer strong-pilot data.

### Environment-v3 C16 replacement certification (declared)

Declared on 2026-07-26 after committing the offline dependency repair, proving
the complete fresh-archive gate, and rereading the independent review through
02:18 PDT. The independent clean retrain has reproduced exact fingerprint
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`;
therefore this remains the correct immutable committed-v3 control.

Hypothesis: frozen C16 will **reject at the 2,040-game primary direct gate**
against Handcrafted on fresh seed `202607260219`. The prior independent
at-scale controls scored 47.5% and 46.8% on distinct virgin seeds, so the
predeclared expectation is that aggregate win rate will not exceed 50% with a
Wilson 95% lower bound above 50%. Per-deck results are reported but not
predicted. If the primary unexpectedly passes, the harness must continue into
its unchanged fixed eight-seed all-five direct/mixed-lift panel; no result can
promote C16 without every gate.

Exact command:

```sh
sh tools/certify.sh 202607260219 \
  68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f
```

Seed `202607260219` was selected and repository-searched before execution. It
is distinct from training, artifact-smoke, fixed-panel, historical, reviewer
(`9317`, `6733`, `5077`), and consumed local (`11235813`) seeds, and there was
no preexisting seed claim. This is the only invocation permitted for that
seed. An exit of `1` after a fully parsed benchmark is the expected valid
scientific rejection; `2+` is infrastructure incomplete and licenses no
strength conclusion.

The sample remains 34 balanced repetitions / 2,040 paired games, 408 per
challenger deck. The harness truthfully reports 77.0656% exact-binomial power
at a true 53% win rate and a 3.111-point MDE at 80% power. It loads only the
existing artifact, binds source/artifact/toolchain integrity, runs the full
archived test and sanitizer gates, and preserves the fixed all-five
definition. This control certification does not tune or select a policy.

### Environment-v3 C16 replacement certification (result: valid rejection)

Executed the declaration exactly once from committed source `40b96a8`:

```sh
sh tools/certify.sh 202607260219 \
  68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f
```

The run completed as a scientifically valid **NOT CERTIFIED** result with
exit `1`; the predeclared rejection hypothesis was confirmed. This was not an
infrastructure failure. The primary balanced benchmark used evaluation seed
`202607260219`, training seed `424242`, 34 repetitions, and 2,040 games. Frozen
C16 went `978-1062-0`, or 47.94%, with Wilson 95% interval
45.78%--50.11%. Consequently the aggregate-over-50 and Wilson-lower-over-50
criteria both failed.

The exact challenger-deck rows and the corresponding Handcrafted
baseline-deck-column wins were:

| Challenger deck | C16 row record | C16 wins | Handcrafted column wins | Direct sign |
| --- | ---: | ---: | ---: | --- |
| Green | 129-279-0 | 129 | 173 | fail |
| Red | 184-224-0 | 184 | 222 | fail |
| Blue | 273-135-0 | 273 | 279 | fail |
| White | 223-185-0 | 223 | 186 | pass |
| RU Aggro | 169-239-0 | 169 | 202 | fail |

Thus only White passed the five-deck direct sign. The fixed eight-seed panel,
mixed-field lift panel, and probe stage were not run because the immutable
primary gate rejected. This is consistent with the independent 47.5% and
46.8% committed-v3 controls rather than evidence of a new regression.

All prerequisite engineering evidence passed before gameplay: 42/42
certification self-tests, the complete archived `make -B test`, the release
build, the sanitizer build, and 126/126 AddressSanitizer/UndefinedBehaviorSanitizer
engine tests. The principal stage durations were 4.303 s for certification
self-tests, 320.792 s for the full archived test, 29.760 s for the release
build, 49.253 s for the sanitizer build, 217.473 s for sanitizer tests,
12.203 s for artifact load, and 189.228 s for the primary benchmark. Source,
toolchain, simulator, and artifact integrity checks passed before and after
the benchmark. The loaded artifact retained fingerprint `68126afc...` and
SHA-256 `53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca`
before and after load.

Evidence is immutable under:

```text
certification-runs/runs/20260726T092249Z-40b96a827400-s202607260219-p32666
```

The primary log is 59 lines / 2,796 bytes with SHA-256
`28e71514e9c13273f000a4c1fc458b85d7c9316e236ff5cc1baa9840fa15ac46`.
`report.json` is 1,034 lines / 39,338 bytes with SHA-256
`bb5c6ed5f01bf74f5856d91a60930a9b0da0568fbd311ef2490c8869f6663ce5`.

Review reconciliation before recording this conclusion: the newest
independent entry remained 02:34 PDT and endorsed this rejection-expected
declaration. Its VC-1 evidence points specifically at four-record bootstrap
target myopia, but its committed-v3 confirmation VC-1v3 was still running.
Decision: retain C16 as the immutable rejected control; do not retry or reuse
seed `202607260219`. Await VC-1v3 before preregistering a target-distance
challenger. If it transfers, isolate the label distance with same-shard
TD4-C17 and TD8-C17 siblings rather than confounding a TD8 C17 treatment with
an extra generation.

### Certification v4 pooled-direct metrology correction (declared)

Declared after the valid C16 rejection was committed. This changes future
certification measurement only; it does not reinterpret or rerun the v3 C16
result, change a bot, or relax any five-deck acceptance requirement.

Hypothesis: pooling the two already-predeclared, exact direct matrices will
make the per-deck sign gate more credible and eliminate premature rejection
caused solely by an underpowered standalone deck slice. The primary matrix
contains 34 repetitions (2,040 games; 408 per challenger deck), and the fixed
eight-seed panel contains 40 repetitions (2,400 games; 480 per challenger
deck). Their component-wise sum must contain exactly 74 repetitions:

- 296 games in every diagonal cell and 148 in every off-diagonal cell;
- 888 games for each of Green, Red, Blue, White, and RU Aggro;
- 4,440 games in aggregate.

Certification schema v4 will retain all existing parsing and integrity
checks, but separate standalone diagnostics from promotion evidence:

1. The 2,040-game primary remains an early large-regression screen. Its
   aggregate win rate and Wilson 95% lower bound must both exceed 50%.
2. If that aggregate screen passes, a primary CLI rejection caused only by a
   standalone 408-game deck sign will not stop collection of the fixed panel.
   Its exit code must still agree with the CLI's own stricter printed verdict.
3. The fixed panel must still have no losing evaluation seed and Learned must
   still have the largest lift over Random on all five decks. Its standalone
   480-game deck signs and Wilson interval remain reported diagnostics rather
   than separate vetoes.
4. The two exact matrices are then summed cell by cell. The combined
   4,440-game aggregate win rate and Wilson lower bound must exceed 50%, and
   Learned must have strictly more direct wins than Handcrafted for every one
   of the five 888-game challenger-deck marginals.
5. Tests, release/sanitizer gates, load-only artifact identity, hidden-info
   rules, fixed seeds, and every source/toolchain integrity check remain
   mandatory.

This is not threshold weakening: the all-five direct requirement remains
strict, but is applied once to all prespecified direct evidence instead of
twice to smaller noisy slices. The implementation must cross-check every
combined cell, row, column, and aggregate; reject ties, losses, Wilson
failures, tampering, and CLI exit-code contradictions; bump the report schema;
and preserve both standalone matrices in the report. It must not change the
simulator commands.

No evaluation seed is claimed by this declaration. After implementation,
focused tests, complete integration tests, and a committed source snapshot,
any certification using v4 requires a new repository-searched virgin primary
seed. Seeds `11235813` and `202607260219` remain permanently consumed.

#### Certification v4 implementation (accepted)

Implemented the declaration without changing either simulator command.
Primary parsing now distinguishes its original standalone CLI verdict from
the aggregate/Wilson smoke gate, and fixed-panel parsing distinguishes its
original standalone verdict from the no-losing-seed plus all-five-lift
contribution required by v4. Exit codes remain bound to the CLI's own printed
verdicts even when a stricter standalone deck slice no longer vetoes collection
of the second prespecified matrix.

The implementation reconstructs both source matrices from their 25 cells,
checks their printed rows, reciprocal columns, and aggregates, then sums them
component-wise. It fails closed unless the pooled evidence is exactly 74
repetitions, 296 games per diagonal cell, 148 per off-diagonal cell, 888 per
row and column, and 4,440 aggregate. The final direct report includes the
combined Wilson interval, all five strict deck signs, both source matrices,
and a `learned-value-certification/v4` schema marker.

Verification:

```sh
make test-certify
PYTHONDONTWRITEBYTECODE=1 python3 -m py_compile \
  tools/certify.py tests/test_certify.py
git diff --check
```

All 48 certification tests passed. New coverage proves exact cell/row/column
cross-sums; repaired weak slices in either source; strict pooled tie/loss and
Wilson rejection; source-cell, row, and aggregate tamper rejection; and CLI
exit-code disagreement failure. Decision: **accept the metrology
implementation**. It collects no new gameplay evidence and claims no seed.

Review reconciliation before this conclusion: the 02:47 PDT independent entry
explicitly endorsed the v4 pooling discipline as sound and not threshold
weakening. The newer 02:55 PDT research entry reported C17-DB8 separately; it
does not alter this measurement-only decision.

### Production bootstrap-helper equivalence audit (accepted)

After commit `c1b790a` routed both production challenger label paths through
`n_state_bootstrap_targets(..., 4)`, a fresh full-size C16 retrain was run in
an isolated working directory and cache:

```sh
/Users/andrewjohnson/proj/magic-ai-vibes/build/old-school-sim \
  --benchmark --games 1 --seed 919191 \
  --train-games 800 --train-seed 424242 \
  --challenger learned-value-c16 --baseline random \
  --learned-rollouts 1 --refresh-value-challenger-cache
```

Training completed in 291.22 seconds. The regenerated T800/C16 model reproduced
fingerprint
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.
More strongly, its 3,111,437-byte artifact was byte-identical to the frozen
control: both have SHA-256
`53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca`.
The incidental one-repetition/60-game Random benchmark scored 53-7 and is not
bot-strength evidence; seed `919191` belongs only to this equivalence audit.

Together with the exact small C2 and contextual-C1 component goldens and the
independent review's bit-identical 2,000-game lift-table replay, this accepts
the refactor as behavior-preserving. Distance 4 remains the immutable C16
recipe, and a future non-4 treatment must produce a different target hash and
artifact fingerprint before evaluation.

### TW-C17 terminal-credit endpoint experiment (declared)

Declared after rereading `REVIEW.md` through 03:03 PDT. VC-1v3 reproduced the
four-record target's signed self-play miscalibration under Environment v3
(approximately Blue -9 points and Green +17 points). The independent C17-DB8
distance-8 fork then rejected at 48.7% over 2,040 games, but its preregistered
sensitive rows moved in the predicted direction (Red mirror +9 points, Green
+12, RU Aggro +4). This licenses the value-target surface while rejecting
distance 8 as a sufficient standalone recipe.

We take the terminal-credit side of the reviewer's proposed bracket; the
reviewer may test distance 6 independently. Hypothesis: on one shared C17
shard, increasing terminal outcome weight from 0.50 to 0.75 will improve
held-out terminal calibration and paired gameplay across all five decks
relative to an otherwise bit-identical 0.50 control. The 0.75 endpoint is
fixed a priori as the midpoint between the canonical equal blend and
terminal-only supervision: it halves inherited-parent bias while retaining
25% bootstrap signal. No endpoint tuning is permitted on these seeds.

#### Shared training construction

- Immutable parent: exact Environment-v3 C16, training seed `424242`, T800,
  fingerprint `68126afc...`.
- Reconstruct C16 once and abort unless its fingerprint and captured canonical
  replay accounting match. Retain the terminal-only random anchor plus the
  original G15 and G16 blocks.
- Raw C17 seed: `202607260311`, repository-searched and unused before this
  declaration.
- Collect exactly five 40-game balanced blocks (200 games) using
  `balanced_schedule(seed, 17, block)`. Every unordered deck pairing appears
  in both seat orientations and with each seat starting; every deck appears
  80 times.
- Both seats are frozen C16 Learned mirrors with canonical late-stage
  collection: K=1, horizon 4, exploration 0.05, no Handcrafted actions,
  labels, features, or continuation policy.
- For each chronological trace and perspective, let `z` be the unchanged
  discounted terminal target and `v4 = V_C16(s[i+4])`. The final four records
  remain `z`. Earlier TW50 targets are `0.50*z + 0.50*v4`; TW75 targets are
  `0.75*z + 0.25*v4`.
- Fit TW50-C17 and TW75-C17 from recursive deep clones of the exact same C16
  using the identical anchor + G15 + G16 + shared C17 feature order, three
  epochs, learning rate 0.006, and identical member/training/shuffle seeds.
  Historical replay labels retain their original 0.50 blend; only the new C17
  target bits may differ.

Hard pre-evaluation invariants: one raw shard is generated exactly once;
feature, outcome, ordering, schedule, and replay hashes match across arms;
terminal-tail targets match; every bootstrapped row satisfies exactly
`TW75 - TW50 = 0.25 * (z - v4)`; the two target hashes and candidate
fingerprints differ; the parent fingerprint remains unchanged; and only
critic components change. Either candidate must use a separate versioned
terminal-weight artifact family and can never overwrite or load as generic
C16/C17.

#### Sealed offline and gameplay gates

Holdout seed `202607260312` is reserved for a separate 200-game, five-block
balanced C16-mirror corpus and may not enter fitting. Report game-clustered
paired Brier loss, soft-label log loss, calibration bias, target variance,
prediction saturation, and actual trace-record-to-turn distance pooled and
for Green, Red, Blue, White, and RU Aggro. TW75 must beat TW50 and frozen C16
on pooled Brier and log loss with the paired game-cluster 95% upper bound on
each loss difference below zero; neither loss may worsen by more than 0.01 on
any deck, and the VC-1v3 signed biases must shrink without creating a new
material deck bias. Existing immutable Learned-reference action probes remain
diagnostic and hidden-repartition invariance must be exact. Fitting the target
formula itself is only a wiring check, never evidence of quality.

If and only if the offline gate passes, evaluation seed `202607260313` is
reserved for two load-only, deployment-identical K=8/H4 panels:

1. TW75-C17 versus TW50-C17, 200 same-deck paired games per deck (1,000 total),
   exactly balancing policy seat and play/draw.
2. If the first passes, TW75-C17 versus frozen C16 under the identical panel.

The license gate is aggregate strictly above 50% and no losing deck in both
panels. The 1,000-game aggregate can resolve only effects of roughly three
points; 200-game deck rows are directional screens, not small-effect claims.
Red, Green, and RU Aggro are registered sensitive diagnostics but receive no
special training weight or easier acceptance rule.

Passing licenses a separately preregistered full-recipe comparison:
constant-0.50 C16 versus a linear generation schedule
`a_g = 0.50 + 0.25*(g-1)/15`, so G1 is exactly 0.50 and G16 exactly 0.75.
It does not itself promote a bot. Failure rejects this 0.75 endpoint without
seed retries; the next axis must be declared separately. All seeds
`202607260311`, `202607260312`, and `202607260313` are now consumed for this
single experiment regardless of outcome.

#### TW-C17 pre-run metric and accounting clarifications

Committed before training, HOLD1, or gameplay opened any of the three reserved
seeds. These definitions close ambiguities in the declaration; they do not
change either arm or select a result.

- HOLD1 uses five exact calls to
  `balanced_schedule(202607260312, 17, block)`: 200 physical games, 400
  seat-game perspectives, 80 perspectives per deck, and exact seat/play-draw
  balance. Both pilots are frozen C16 K1/H4 with exploration 0.05 and a
  500-turn bound. Candidate models never choose a HOLD1 action.
- Losses are record-weighted to match critic training. Uncertainty clusters by
  physical game: for record metric `x`, compute the overall record mean, each
  game's sum of centered record residuals, and the CR1 variance
  `G/(G-1) * sum(U_g^2) / N^2`. Use
  `1.959963984540054 * SE` for the paired 95% interval.
- Signed calibration bias is `prediction - discounted_terminal_target`.
  Green and Blue satisfy the registered shrink condition only when
  `abs(TW75 bias)` is strictly below both TW50 and C16. A material bias is
  absolute bias at least 0.05 with its game-clustered 95% interval excluding
  zero. Reject a newly material TW75 deck bias when neither control already
  has a same-sign material bias.
- Prediction saturation means `p <= 0.01 || p >= 0.99`; it is reported only
  and is not a gate.
- The tracked source fixtures do not currently have a qualified immutable
  Environment-v3 deep-reference cache. Older v2 caches are forbidden. Unless
  a new cache is independently declared, qualified, generated, and frozen
  before HOLD1, action diagnostics are limited to selection changes and exact
  hidden-repartition invariance; no reference agreement/regret claim or gate
  may be fabricated from stale labels.
- Each gameplay panel uses 50 common-seed quartets per deck: challenger in
  each seat crossed with each starting player, for exactly 200 games per deck,
  1,000 total, 100 play/100 draw, and 50 in every seat-by-play/draw quadrant.
  Aggregate passes at at least 501 wins. A deck is “not losing” when its wins
  are greater than or equal to its losses; a draw does not turn an otherwise
  tied head-to-head deck into a loss.
- The earlier “roughly three points” wording is a nominal independent-binomial
  95% half-width (3.10 points), not an 80%-power MDE. The corresponding
  two-sided 80%-power MDE is approximately 4.43 points pooled and 9.91 points
  for a 200-game deck row. Because four games share each base seed, report
  quartet-clustered uncertainty and do not claim those independent-binomial
  scales as achieved paired power.

The only permitted evaluation route is exclusive and load-only: it must
require exact frozen C16 and TW bundle artifacts, hash/size/mtime them before
and after, run HOLD1 first, suppress panel 1 after an offline rejection,
suppress panel 2 after a panel-1 rejection, and distinguish scientific
rejection (exit 1) from incomplete evidence (exit 2).

Review coordination: the 03:18 PDT entry independently endorsed TW-C17 as the
clean one-shard attribution arm. Its separate full-recipe 0.50-to-0.80 anneal
uses reserved seed `1471`; the designs and seeds do not collide. Both results
must read out before any combined form is declared.

#### TW-C17 implementation freeze (no experiment run)

Recorded after rereading `REVIEW.md` through 03:47 PDT. Commit `2379443`
implements the declared shared-shard family without opening seeds
`202607260311`, `202607260312`, or `202607260313`.

- C16 is reconstructed once and rejected unless its exact frozen fingerprint
  matches. The capture retains the random anchor plus G15 and G16, which is
  the canonical replay window that a C17 update consumes.
- One five-block balanced C17 schedule supplies both arms. TW50 and TW75 use
  identical encoded features, order, parent, optimizer, epochs, learning
  rate, member seeds, and shuffle seeds. Runtime checks enforce the declared
  row-level target relation, terminal-tail identity, frozen parent, distinct
  target/model fingerprints, and critic-only component changes.
- The two models and compact provenance live in one separate-magic,
  checksummed, atomic artifact. Generic C16/C17 loaders reject it, and the
  TW loader rejects generic artifacts, corruption, mismatched dimensions,
  seeds, recipe, model fingerprints, or accounting.
- `--train-terminal-weight-c17` is canonical-only and refuses to retrain or
  overwrite an existing one-shot bundle. The selectable TW50/TW75 benchmark
  tokens are load-only. Generic benchmark use of all three reserved seeds is
  prohibited so only the sealed evaluator can open HOLD1/gameplay.

Verification:

```sh
make build/old-school-sim build/old-school-tests
./build/old-school-tests
sh tests/test_cli.sh ./build/old-school-sim
git diff --check
```

Results: strict C++20 `-Werror` builds passed; engine tests passed 128/128; CLI
tests passed; diff check passed. An independent read-only code review found
the original overwrite gap, which was fixed and regression-tested, then
reported no remaining material findings. The implementation milestone is
accepted; this is not evidence for either scientific arm. Next: implement and
freeze the exclusive load-only evaluator, then open raw-shard seed
`202607260311` exactly once.

#### TW-C17 sealed-evaluator freeze (no experiment run)

Recorded after rereading `REVIEW.md` through 04:23 PDT. The new Red
mispricing result refutes the reviewer's earlier Red-bias prediction but does
not alter this experiment's preregistered Green/Blue calibration gates. We
accept its interpretation as a readout aid: a Red gameplay change without a
Red calibration shift can be a policy effect rather than a contradiction.

The exclusive `--evaluate-terminal-weight-c17` route is now frozen before any
reserved seed is opened. It:

- loads the exact frozen Environment-v3 C16 and the distinct paired TW bundle,
  then verifies full fingerprints, independently recomputed component
  fingerprints, and critic-only TW50/TW75 deltas;
- snapshots content digest, byte size, and modification time for both
  artifacts before and after every completed or exceptional route;
- runs a cache-free, label-free deployed-policy hidden-repartition audit for
  C16, TW50, and TW75 over all 20 immutable dev-v3 probes before HOLD1;
- implements the declared five-block HOLD1 schedule, record-weighted Brier and
  soft-label log loss, physical-game CR1 intervals, signed bias/materiality,
  saturation, trace-distance reporting, and every preregistered offline gate;
- conditionally runs the two exact 50-quartet-per-deck K8/H4 gameplay panels,
  with raw-win `>=501`, every-deck `W>=L`, quartet-clustered intervals, and
  strict suppression after a failed preceding gate;
- emits both a human report and a fixed 13-column TSV containing seeds,
  configurations, artifact/model identities, pooled and five-deck metrics,
  W/L/D rows, and explicit unavailable/suppressed states; and
- accepts no other CLI options and returns 0 for scientific pass, 1 for a
  valid scientific rejection, and 2 for infrastructure or incomplete
  evidence. The CLI regression invokes it only from an isolated temporary
  directory with no artifacts, proving fail-closed load-only behavior without
  opening HOLD1.

Exact verification on the frozen source:

```sh
make build/old-school-sim \
  build/old-school-terminal-weight-eval-tests \
  build/old-school-probe-runner-tests
./build/old-school-terminal-weight-eval-tests
./build/old-school-probe-runner-tests
sh tests/test_cli.sh ./build/old-school-sim
make test
c++ -Iinclude -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  src/game.cpp src/learned_iteration.cpp src/probes.cpp \
  src/probe_eval.cpp src/probe_runner.cpp \
  src/terminal_weight_eval.cpp tests/test_terminal_weight_eval.cpp \
  -o build/old-school-terminal-weight-eval-tests-sanitize
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ./build/old-school-terminal-weight-eval-tests-sanitize
git diff --check -- ':!REVIEW.md'
```

Results: evaluator 10/10, probe runner 25/25, engine 128/128, learned
iteration 25/25, probes 40/40, probe metrics 11/11, web bridge 9/9, web
client 82/82, certification 48/48, full CLI and capture-once suites passed,
strict simulator build passed, and the evaluator sanitizer passed 10/10 with
no ASan/UBSan finding. A separate read-only agent rechecked the final formulas,
gates, provenance, isolation, output schema, and conditional execution against
the preregistration and reported no material findings.

No canonical trainer or evaluator was run. Seeds `202607260311`,
`202607260312`, and `202607260313` remain unopened. This implementation
milestone is accepted; it is not evidence for TW75. Next: commit this freeze,
open raw-shard seed `202607260311` exactly once under `capture_once.sh`, bind
the resulting artifact hash, reread `REVIEW.md`, and only then run the sealed
evaluator.

#### TW-C17 one-shot shared-shard training result

Recorded after rereading `REVIEW.md` through 04:32 PDT. Commit `b737e28`
was the frozen source. The exact command was:

```sh
sh tools/capture_once.sh \
  /Users/andrewjohnson/proj/magic-ai-vibes/build/experiments/tw-c17-train-b737e28-s202607260311 \
  ./build/old-school-sim --train-terminal-weight-c17 \
  --train-games 800 --train-seed 424242
```

The process exited 0 in 290.86 seconds. The immutable complete-transcript
SHA-256 is
`fa79ef62388b16c65aa9cfc4904634c1608c913571a9d47f107c24ddf02ceb52`;
the recorded exit file contains `0`. The resulting 6,224,082-byte paired
bundle is
`build/model-cache/old-school-value-terminal-weight-c17-v1-t800-p424242-r202607260311.bin`
with SHA-256
`7df4c122b46ca5256ea09c48831bb04dbfe472e2f27eae9bc5c251884f031ec0`.

Embedded identities and shared-shard accounting:

- parent C16:
  `68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`;
- TW50-C17:
  `46afe9c63fb4ca49984eb93b74299721508cc47191fae8a4792da2488be1a94b`;
- TW75-C17:
  `247eae8467a955015377486235af4968b4691fd13c1594000403eb5ce3983fd3`;
- schedule/raw/features/outcomes:
  `ae63290d0d830552468c4191878b2d6aefe13a7139e126d11423e868aac3386c` /
  `0b96ebfa6d999fc5d96f9203a4cd370541a31d3f545529981d724177252926f2` /
  `18419407ac5e68f812657b960a0965e0e4d8260cd920ab920064036a16f8dae0` /
  `3c2bca6f4989f1c8b81d20ec94a305b2b8be4489472dd464c5141726c7a926e7`;
- TW50/TW75 target hashes:
  `89609007c43ffc8fd5753114117a791c32e63c5457eb001476a6254cb0b4a323` /
  `868413bd385465c973cb7a92a9a9632f6db840594bf2d4e007536b55c6930027`;
- examples historical/shard/bootstrap/tail:
  `81228 / 10936 / 9336 / 1600`.

Every deck appeared in exactly 80 physical games. Per-deck
examples/bootstrap/tail were Green `2143/1823/320`, Red
`2006/1686/320`, Blue `1914/1594/320`, White `2936/2616/320`, and RU
Aggro `1937/1617/320`.

Artifact construction passes all preregistered identity/accounting invariants:
one balanced shard, distinct arm targets and fingerprints, exact frozen
parent, and complete all-five-deck balance. This accepts the training run as a
valid paired artifact only; it provides no performance evidence and neither
arm is accepted or rejected yet. Raw-shard seed `202607260311` is now
consumed. HOLD1 seed `202607260312` and gameplay seed `202607260313` remain
unopened. Next: commit this provenance, then invoke the exclusive sealed
evaluator once; it will stop after HOLD1 unless every offline gate passes.

#### TW-C17 sealed result: rejected on the Green bias gate

Recorded after rereading `REVIEW.md` through its 04:32 PDT cycle. Source and
training provenance were frozen at commit `892d9f3`. The exclusive one-shot
evaluation command was:

```sh
sh tools/capture_once.sh \
  /Users/andrewjohnson/proj/magic-ai-vibes/build/experiments/tw-c17-sealed-eval-892d9f3 \
  ./build/old-school-sim --evaluate-terminal-weight-c17
```

It exited `1`, the evaluator's valid-scientific-rejection status, in 4.83
seconds. The 23,300-byte, 202-line complete transcript has SHA-256
`07a31648bc9be4bc47125ebfe145e5b70f9d865955b47c8bd9a79058f078ad02`.
The paired bundle remained byte-identical after evaluation: 6,224,082 bytes
and external SHA-256
`7df4c122b46ca5256ea09c48831bb04dbfe472e2f27eae9bc5c251884f031ec0`.
The evaluator also reported both frozen artifact snapshots unchanged.

HOLD1 opened holdout seed `202607260312` exactly once and produced 10,694
record-weighted trace perspectives from 200 physical C16-mirror games, with
80 perspectives per deck. Pooled results were:

| Model / contrast | Brier | Soft log loss | Signed bias |
| --- | ---: | ---: | ---: |
| C16 | 0.079574 | 0.589600 | -0.012049 |
| TW50-C17 | 0.078107 | 0.586657 | +0.007598 |
| TW75-C17 | 0.077647 | 0.585682 | +0.007972 |
| TW75 - TW50 | -0.000460 `[-0.000717, -0.000203]` | -0.000975 `[-0.001511, -0.000439]` | — |
| TW75 - C16 | -0.001927 `[-0.002917, -0.000937]` | -0.003919 `[-0.005970, -0.001867]` | — |

Thus TW75 passed all four prespecified pooled paired-loss comparisons with
their physical-game-clustered 95% upper bounds below zero. No deck's Brier or
log loss worsened by 0.01, no new material signed bias was created, and Blue's
absolute bias strictly shrank against both controls. The complete per-deck
readout was:

| Deck | Records | C16 bias | TW50 bias | TW75 bias | TW75-TW50 dBrier / dLogLoss | TW75-C16 dBrier / dLogLoss |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Green | 2,072 | +0.078265 | +0.085781 | +0.084001 | -0.000821 / -0.001711 | -0.001269 / -0.002481 |
| Red | 1,828 | -0.023658 | +0.002205 | +0.000187 | -0.000284 / -0.000619 | -0.001210 / -0.002623 |
| Blue | 2,036 | -0.087186 | -0.060748 | -0.058301 | -0.000354 / -0.000776 | -0.004751 / -0.010430 |
| White | 2,888 | -0.032160 | -0.009962 | -0.006661 | -0.000597 / -0.001279 | -0.001899 / -0.003315 |
| RU Aggro | 1,870 | +0.012098 | +0.027772 | +0.026095 | -0.000137 / -0.000255 | -0.000327 / -0.000622 |

The sole failed gate was registered Green bias shrinkage:
`abs(+0.084001)` improved slightly over TW50's `abs(+0.085781)` but did not
improve over C16's `abs(+0.078265)`. This is a real rejection under the
precommitted conjunctive gate even though every pooled loss result favored
TW75. Cache-free deployed-policy hidden-repartition invariance passed
bit-identically for all three policies across all 20 Environment-v3 probes.
Action agreement/regret remained explicitly unavailable because no qualified
immutable v3 deep-reference cache exists.

Decision: **reject TW75-C17 as a licensed endpoint**. Panel 1 (TW75 versus
TW50) and panel 2 (TW75 versus C16) were both correctly suppressed, so
gameplay seed `202607260313` was not opened; under the declaration it is still
retired with the other TW-C17 seeds. We do not reinterpret favorable pooled
calibration as a gameplay win and do not weaken the Green gate after seeing
the result.

Interpretation relative to the independent review: the result supports the
reviewer's claim that the terminal-weight surface is live—TW75 made small,
precisely estimated pooled calibration gains, especially on Blue—but it does
not supply the paired gameplay evidence that would have licensed the proposed
combined distance/terminal-weight recipe. Red's near-zero bias also agrees
with the independently registered VC-2 correction that Red's target error was
not chiefly calibration. The next experiment must therefore be a separately
declared axis, not an endpoint retry or a seed substitution.

### TA4-0 calendar-turn bootstrap-target audit (declared)

Declared at 2026-07-26 04:53 PDT after rereading `REVIEW.md` through its
04:47 PDT independent reproduction of TW-C17. This is a new, measurement-only
target-semantics axis. It does not train or promote a model, open any retired
TW-C17 seed, change terminal weight, or weaken the failed Green gate.

TW-C17 exposed a concrete mismatch in the canonical label recipe. The
so-called four-state bootstrap uses trace record `i+4`, but `run_with_trace`
retains both turn-start states and nontrivial priority roots. On sealed HOLD1,
`i -> i+4` advanced only 2.949 actual turns pooled, with range 0–4; the means
were Green 3.130, Red 3.116, Blue 2.711, White 2.768, and RU Aggro 3.147.
Thus the target horizon is both shorter than the deployed four-turn search and
systematically deck-dependent. This is a card-agnostic target-semantics defect,
not a reason to retry terminal weight.

Hypothesis: on frozen-C16 mirror traces, replacing record offset four with the
earliest future trace record whose `turn_number` is at least the root's
`turn_number + 4` will materially reduce Green's optimistic canonical 0.50
terminal / 0.50 parent bootstrap target. On common bootstrap-eligible rows it
will preserve pooled Brier quality, move at least one of RU optimism or Blue
pessimism in the registered direction, and leave Red/White without a new
material bias.

#### Frozen construction

- Exact parent: Environment-v3 C16, T800, training seed `424242`, fingerprint
  `68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.
- Audit seed: `202607260501`, repository-searched and unused before this
  declaration. Generate 50 exact 40-game balanced blocks using generation
  coordinate 18: 2,000 physical games, 4,000 perspectives, and 800
  perspectives per Green, Red, Blue, White, and RU Aggro.
- Both seats are frozen C16 Learned mirrors with K=1, H=4, exploration 0.05,
  residual zero, no Handcrafted policy, and a 500-turn bound.
- For record `i` and perspective `p`, let `z` be the unchanged discounted
  terminal target. The control is
  `c = 0.50*z + 0.50*V_C16(trace[i+4], p)` when `i+4` exists, else `z`.
- Let `j` be the earliest record after `i` with
  `trace[j].turn_number >= trace[i].turn_number + 4`. The treatment is
  `a = 0.50*z + 0.50*V_C16(trace[j], p)` when `j` exists, else `z`.
  Because every real turn has an unconditional turn-start snapshot, every
  treatment bootstrap must land at exactly four elapsed turns; fail closed on
  a skipped/regressing turn, invalid probability, empty trace, schedule
  imbalance, parent mismatch, or nondeterministic rerun.
- Primary comparisons use only the common eligible rows where `j` exists.
  This prevents the treatment's larger terminal tail (`a=z`) from winning
  mechanically. All-record metrics, record-offset and calendar-turn tail
  counts, the complete record-offset physical-turn-distance histogram, target
  hashes, target means/variances, and saturation are descriptive and fully
  reported.
- Losses are record-weighted and uncertainty clusters by physical game using
  the same CR1 estimator and 95% interval as TW-C17. Report control, treatment,
  and paired deltas pooled and per deck. Also report an equal-actor-game
  sensitivity analysis so a long, decision-dense game cannot dominate merely
  by contributing more trace records; its directional conclusions must agree
  with the record-weighted primary result.
- “Four turns” means four `turn_number` advances. It is not asserted to be
  identical to the deployed search boundary unless a focused rules test proves
  that equivalence.

#### Fixed reject-only gate

TA4 passes as a target mechanism only if all of the following hold:

1. on common-bootstrap rows, Green's paired signed-bias change `a-c` is at
   most `-0.010`, its physical-game-clustered 95% upper bound is below zero,
   and the equal-actor-game sensitivity estimate also points downward;
2. common-bootstrap pooled Brier is noninferior with the `a-c` upper 95%
   bound below `+0.001`, while all-record pooled Brier has a nonpositive point
   estimate;
3. at least one additional registered biased deck moves correctly: Blue
   signed bias increases toward zero or RU Aggro signed bias decreases toward
   zero, with record- and equal-actor-game weighting agreeing in direction;
4. no deck's all-record Brier worsens by more than `0.005`, Red and White
   acquire no new material bias, treatment bootstrap distances are exactly
   four turns, all five decks have exact schedule balance, repeated fixed-seed
   reports are bit-identical, the parent artifact is unchanged, and target
   computation is hidden-information safe.

Soft-label log loss remains a mandatory pooled/per-deck report and
corroborating readout, not another conjunctive veto. White has no directional
bias gate because the independent mispricing map did not register a White
sign. Green movement smaller than the declared one-point threshold is
inconclusive and does not license training; a wrong-direction move rejects the
mechanism. The 2,000-game audit is still target science, not playing-strength
evidence.

Passing licenses a separately preregistered, same-shard paired C17 critic fit:
canonical record-offset-4 control versus calendar-turn-4 treatment with all
other data, order, parent, optimizer, seeds, and terminal weight identical.
Failure rejects calendar alignment as the immediate training target without
changing the four-turn endpoint, retrying a seed, or inspecting gameplay.
No benchmark or training run is licensed by this declaration itself.

#### TA4-0 implementation freeze

Recorded at 2026-07-26 05:49 PDT after rereading `REVIEW.md` through its
05:47 PDT cycle and before opening audit seed `202607260501`.

The exclusive load-only `--audit-calendar-turn-targets` route is now
implemented. It loads and fingerprints only the exact Environment-v3 C16
artifact; constructs the fixed 50-block shard twice from fresh games; reduces
worker slots in semantic schedule order; checks the existing record-offset
helper bit-for-bit; checks earliest exact four-turn treatment indices, terminal
tails, Time Walk turn numbering, common eligibility, and hidden-zone
repartitions; emits pooled/per-deck record-weighted and equal-actor-game CR1
metrics plus all three root-turn strata; snapshots the parent before, during,
and after collection; and implements the frozen exit taxonomy (`0` pass, `1`
valid scientific rejection, `2` infrastructure/incomplete evidence).

The hidden-zone check compares public observations, encoded critic features,
critic values, control/treatment target arrays, eligibility/future-index
records, and a reconstructed scoring hash bit-for-bit after changing only the
opponent hand/library partition. A compile-time assertion binds the audit's
reported four-turn horizon to the production Learned search-horizon constant.
No trainer symbol is called by the audit route.

Verification before the one-shot run:

```sh
make test
```

passed 128 engine, 27 learned-iteration, 40 probe, 11 probe-metric, 25 probe
runner, 10 terminal-weight evaluator, 8 TA4 audit, and 9 web-bridge tests;
the CLI and capture-once scripts passed; the representative simulator smoke
completed; all 82 web unit/contract tests passed; and all 48 certification
harness tests passed.

```sh
c++ -Iinclude -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  src/game.cpp src/learned_iteration.cpp src/probes.cpp \
  src/probe_eval.cpp src/probe_runner.cpp \
  src/terminal_weight_eval.cpp src/turn_alignment_audit.cpp \
  tests/test_turn_alignment_audit.cpp \
  -o build/old-school-turn-alignment-audit-tests-sanitize
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ./build/old-school-turn-alignment-audit-tests-sanitize
```

passed all 8 TA4 tests under AddressSanitizer and UndefinedBehaviorSanitizer.
The focused suite covers exact schedule balance, ordinary/same-turn priority
roots, Time Walk extra-turn sequencing, terminal tails, invalid trace helpers,
uneven record counts, directly paired clustering, zero-common actor-games,
synthetic pass/reject gates, exact `0/1/2` status mapping, missing-artifact
fail-before-collection, and nonreserved 1-versus-2-worker bit determinism.

An independent read-only implementation review found no blocking issue after
the hidden scoring-hash and horizon assertions were added. `git diff --check`
and strict `-Werror` builds are clean. The reserved seed and canonical command
remain unopened. The next and only licensed action is to commit this frozen
implementation and invoke the canonical audit once through
`tools/capture_once.sh`; its registered gate determines whether same-shard
calendar-turn C17 training is licensed.

#### TA4-0 preimplementation statistical clarification

Recorded at 2026-07-26 04:57 PDT before implementation and before audit seed
`202607260501` was opened. The initial declaration used a five-block HOLD1-size
sample and a broad all-deck improvement conjunction. Independent design review
correctly noted that this would be underpowered for the one-point Green
mechanism effect and would confuse a focused target-sufficiency audit with
model promotion.

The frozen design above therefore uses 50 blocks (2,000 physical games), adds
equal-actor-game sensitivity weighting, fixes a one-point Green MDE and
clustered direction gate, treats common-row pooled Brier as a noninferiority
guard, and makes log loss corroborative. These changes were selected entirely
from TW-C17's already published trace-distance accounting and the independent
04:55 PDT early/late Green stratification; no TA4 implementation, target,
metric, or seed result existed. The formula, seed, exact parent, balance,
hidden-information requirements, and conditional training license are
unchanged.

#### TA4-0 execution and gate freeze

Recorded at 2026-07-26 05:03 PDT after rereading `REVIEW.md` through 05:02
PDT and an independent preimplementation audit. No audit code had run and seed
`202607260501` remained unopened. These definitions resolve execution and
metric ambiguities without changing the treatment.

The only canonical route is:

```sh
./build/old-school-sim --audit-calendar-turn-targets
```

It accepts no other option, uses the fixed seed internally, loads the existing
exact C16 artifact only, and never trains, refreshes, caches, or writes a
model. It fingerprints and snapshots artifact content, size, and modification
time before collection and on every completed/exceptional route. Missing or
wrong C16 must fail before collection begins. Exit `0` means mechanism pass,
`1` means valid scientific rejection, and `2` means infrastructure or
incomplete evidence. The mode is mutually exclusive with every other main
mode, and generic seed routes reject reserved seed `202607260501`. One
captured invocation independently constructs the entire fixed 2,000-game
corpus twice from fresh `Game` instances, compares semantic reports plus
raw/target hashes bit-for-bit, and emits one report. This is one deterministic
dataset reproduced internally, not two independent samples.

Exact metric semantics:

- Common eligibility requires both `i+4 < trace.size()` and an aligned `j`.
  Existence of `j` without `i+4` is an invariant failure.
- Every aligned row asserts `turn[j]-turn[i] == 4` and that every record
  strictly between `i` and `j` has a smaller turn number. Control targets must
  be bit-identical to the existing
  `n_state_bootstrap_targets(parent_values,z,4)` output. Control and treatment
  tails must be bit-identical to `z`.
- Hashes bind globally unique physical-game ID
  `block*40 + schedule_index`, block and schedule coordinates, perspective,
  root record index, eligibility/future index, and both target bit patterns.
  Completed worker slots are reduced only in fixed semantic schedule order.
- Every paired CR1 estimate is formed directly from row-level paired deltas
  and clustered by globally unique physical game; it is never the subtraction
  of two separately estimated uncertainties.
- Equal-actor-game weighting first averages row deltas within each
  `(physical_game,perspective)`, weights the resulting actor-games equally,
  and still clusters the two shared-trajectory perspectives by physical game.
  Actor-games with no common row are explicitly excluded and reported.
  Evidence is incomplete unless at least 95% of the 4,000 actor-games, at
  least 760 of 800 for every deck, and at least 1,900 of 2,000 unique physical
  games contribute common rows. Reports include eligible records,
  actor-games, and physical-game clusters pooled and per deck.
- Gate 1 additionally requires Green absolute bias to shrink under both
  record and equal-actor-game weighting. Gate 3 is evaluated on common rows:
  the same one of Blue or RU must move in its registered direction and have
  strictly smaller absolute bias under both weighting schemes; ties fail.
  Blue's direction is `a-c > 0`, RU's is `a-c < 0`.
- Gate 4's `0.005` all-record per-deck Brier guard is a point-estimate guard.
  “New material bias” exactly reuses TW-C17: treatment absolute bias at least
  0.05 with its physical-game-clustered 95% interval excluding zero, unless
  control already has a same-sign material bias.
- The earlier broad sensitivity wording applies only to Green and the one
  qualifying Blue/RU row. All other equal-game metrics remain reported
  diagnostics.
- For each perspective, an executable hidden-zone audit repartitions only its
  opponent's hand/library while preserving public state and zone sizes.
  Encoded root/future critic features, future C16 value, eligibility, control
  and treatment targets, and scoring hashes must remain bit-identical.
- In addition to pooled and five-deck scopes, report descriptive
  treatment-minus-control bias deltas by root-turn stratum `<=3`, `4-7`, and
  `>=8`. These strata do not add a post-hoc gate.

Focused tests must cover same-turn priority roots, ordinary turn starts,
terminal tails, skipped/regressing/overflowing synthetic traces, Time Walk
extra-turn numbering, uneven record counts, paired clustering, zero-common
actor-games, 1-versus-N worker determinism on a small nonreserved schedule,
hidden repartition, missing artifact fail-before-collection, option
exclusivity, artifact immutability, and the `0/1/2` taxonomy. No canonical
audit execution is part of implementation verification.

#### TA4-0 sealed result: rejected at the registered Green MDE

Run once at 2026-07-26 05:50 PDT from frozen implementation commit `9e422f9`.
Immediately before interpreting the result, `REVIEW.md` was reread through its
05:47 PDT cycle. Exact invocation:

```sh
sh tools/capture_once.sh \
  /Users/andrewjohnson/proj/magic-ai-vibes/build/experiments/ta4-0-sealed-audit-9e422f9 \
  ./build/old-school-sim --audit-calendar-turn-targets
```

The process exited `1`, a valid scientific rejection. The immutable transcript
is
`build/experiments/ta4-0-sealed-audit-9e422f9.complete.txt`
(66,014 bytes, 420 lines), SHA-256
`1755cf33fc63f3115af7d40bbd29f627f4aea495d43e5ad13b2a1bfd02777278`.
Wall time was 62.43 seconds. The fixed seed was `202607260501`, generation
coordinate 18, with 50 balanced blocks: 2,000 physical games, 4,000
perspectives, and 800 perspectives per deck. It produced 106,942
trace-perspective rows, of which 84,232 were common eligible.

All evidence and mechanical checks passed: all 4,000 actor-games and 2,000
physical games contributed common rows; each deck contributed all 800
actor-games; the two complete fresh constructions were bit-identical; 106,577
actual hidden-zone repartitions preserved public observations, features,
values, eligibility, targets, and scoring hashes; and the C16 artifact was
unchanged. The frozen parent fingerprint remained
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`;
its evaluator content hash was
`ee89a4aa1e91bbfc3d1ca6144139a86910bbe3e7d465de369f3b5e52471132fe`.

Primary common-row results (control record-offset four to treatment
calendar-turn four):

| Scope | signed bias, control -> treatment (delta; 95% delta CI) | Brier delta | soft-log-loss delta |
| --- | --- | ---: | ---: |
| Pooled | -0.006554 -> -0.005887 (+0.000667; [+0.000542, +0.000792]) | -0.001316 | -0.003503 |
| Green | +0.027097 -> +0.019316 (-0.007781; [-0.008537, -0.007025]) | -0.001171 | -0.003184 |
| Red | -0.009065 -> -0.011712 (-0.002647; [-0.003447, -0.001848]) | -0.001186 | -0.003218 |
| Blue | -0.019293 -> -0.015390 (+0.003903; [+0.002778, +0.005028]) | -0.001898 | -0.005105 |
| White | -0.029755 -> -0.020294 (+0.009461; [+0.008742, +0.010181]) | -0.001343 | -0.003429 |
| RU Aggro | +0.007803 -> +0.004174 (-0.003628; [-0.004406, -0.002851]) | -0.000965 | -0.002598 |

Equal-actor-game sensitivity agreed with the registered directions: Green
`-0.006035`, Blue `+0.004517`, and RU Aggro `-0.002567`. All-record pooled
Brier also improved by `-0.001556`; every deck's all-record Brier delta was
negative. Blue and RU both satisfied the corroboration condition, pooled
common-row Brier was strictly better rather than merely noninferior, Red and
White acquired no new material bias, and every other registered gate passed.

The sole failed gate was deliberately decisive: Green's common-row bias moved
by `-0.007781`, precisely in the predicted direction with its interval below
zero, but it did not reach the preregistered `-0.010` minimum effect. The
calendar alignment is therefore rejected as the immediate training target.
Its broad loss improvements are scientifically useful but do not permit
lowering the MDE after seeing the data, retrying another seed, fitting the
calendar-turn C17 pair, or opening gameplay.

Interpretation: record-offset target semantics were genuinely lossy—calendar
alignment improved Brier and log loss on all five decks and repaired the
registered Blue/RU bias directions—but they explain only about 0.78 points of
Green's optimistic common-row target error. The correction was smallest at
Green root turns `<=3` (`-0.001433`) and largest at `>=8` (`-0.009726`).
Combined with the independently published finding that Green optimism is
concentrated early, this says calendar alignment is insufficient rather than
inert and leaves the early-game target problem substantially intact.

Result: **REJECT TA4-0**. Seed `202607260501` is retired. No model was trained
or promoted, and C16 remains the champion/control. The next experiment must be
a separately preregistered axis; the standing candidate is the independently
preregistered horizon-eight collection-target axis using verified inert,
card-agnostic plumbing, not a post-hoc calendar-alignment retry.

#### C18-H8 external sealed result: rejected from promotion

Recorded at 2026-07-26 06:17 PDT after rereading `REVIEW.md` through its
06:11 PDT cycle and independently auditing the separately maintained
`claude/challenger` experiment at commit `7e808bc`. That branch had
preregistered the standalone collection-horizon treatment before TA4-0
finished, so mainline did not duplicate its reserved seed.

The sole training change was in late-generation **Priority** collection:
one-world Value continuations used horizon eight instead of the canonical
horizon four. The terminal/parent target remained record-offset four with
weight `0.50/0.50`; combat selectors and K=8/H=4 deployment remained
canonical. Exact sealed benchmark command:

```sh
OS_COLLECTION_HORIZON=8 ./build/old-school-sim --benchmark \
  --games 34 --seed 2861 \
  --train-seed 424242 --train-games 800 \
  --challenger learned-value-c16 --baseline handcrafted \
  --learned-rollouts 8
```

The frozen treatment fingerprint was
`99544b7dd6153d25e5986a3e04e7b0839b8e7798c6852f3171ab293e4348c7e0`;
its 3,111,437-byte artifact SHA-256 was
`096b59ba0fc8df139a61a3eca5e8911d7b9de7d259c6dfddbb62bf16ef799b1a`.
It differs from exact C16
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`
(artifact SHA-256
`53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca`).

The 34-repetition all-five-deck benchmark produced 2,040 games:

| Scope | C18-H8 | Handcrafted |
| --- | ---: | ---: |
| Pooled | 963-1077, **47.2%** (Wilson 95% 45.0-49.4) | 1077-963 |
| Green | 137-271, 33.6% | 160-248, 39.2% |
| Red | 190-218, 46.6% | 221-187, 54.2% |
| Blue | 278-130, 68.1% | 296-112, 72.5% |
| White | 202-206, 49.5% | 218-190, 53.4% |
| RU Aggro | 156-252, 38.2% | 182-226, 44.6% |

Registered same-deck diagnostics also failed to show the predicted Green
repair: Green was `56-80` (41.2%), Red `62-74` (45.6%), Blue `66-70`
(48.5%), White `73-63` (53.7%), and RU Aggro `60-76` (44.1%). A separate
load-only reproduction of the exact seed and artifact reproduced every
scientific field, including `963-1077`; its transcript at
`/private/tmp/c18-h8-audit.txt` has SHA-256
`596cf6753134938f0313f52acd70106eb160795faeee76ba4b5c5ded19a2361a`.

Two provenance defects were found and must not be copied into mainline:

- the treatment artifact embeds the canonical
  `terminal-anchor-bootstrap4w50-replay3-k1h4.v1` recipe ID. Horizon eight
  exists only in an environment-dependent filename, so a renamed canonical
  artifact could cross-load and malformed environment text is not rejected;
- `drc-builder --deployment` does not read or hash the claimed frozen
  deep-reference cache. It recomputes its reference with the candidate
  model, so that check fails open. Manual comparison against the committed
  cache nevertheless confirmed that this treatment selected the frozen best
  action on all seven fixtures.

These defects weaken artifact self-authentication and the claimed automatic
DRC gate, but they do not rescue a model whose deterministic 2,040-game
promotion benchmark has an upper Wilson bound below 50%. Result:
**REJECT C18-H8 from promotion** and retire seed `2861`. We do not make the
stronger causal claim that H8 has exactly zero lift over C16, because it was
not paired against C16 on this evaluation seed. No H6/H10 sweep or another
Handcrafted seed is licensed.

### CT8-0: record/calendar unit x four/eight horizon target audit (declared)

Declared at 2026-07-26 06:17 PDT after the independently reproduced TA4-0
and C18-H8 rejections, before seed `202607260621` or any CT8 implementation
was opened. Repository and all-history searches found no prior use of that
seed.

`REVIEW.md` at 06:07 and 06:11 recommends immediately training a broad C19
composite of calendar targets plus deeper or annealed credit and possibly
deeper collection. We agree that the interaction is now the important
question, but not that a three-knob fit is yet confirmatory. The `+1.8` and
`+1.9` point TW8/DB8 estimates came from different noisy evaluation seeds;
their mirror gains may describe the same error mass. TW75 worsened Green
bias, and H8 collection was flat with its Green prediction refuted. Adding
those knobs after seeing five single-change results would introduce
unnecessary researcher choice. This audit therefore tests one exact
two-factor target interaction before any model is fit.

Falsifiable hypothesis: canonical targets are wrong in both their unit
(trace records rather than physical turns) and their reach. On a fresh,
balanced frozen-C16 mirror corpus, the exact eight-calendar-turn target
`CT8` will materially reduce the independently identified early-Green
optimism beyond either single-factor treatment (`CT4` or `RO8`), improve
pooled loss, move Blue and RU bias toward zero, and remain safe on all five
decks.

#### Frozen construction

- Parent/source policy: exact Environment-v3 C16, T800, training seed
  `424242`, fingerprint
  `68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.
- Audit seed `202607260621`, generation coordinate 19, 50 repetitions of the
  exact 40-game balanced schedule: 2,000 physical games, 4,000
  perspectives, and 800 perspectives for each of Green, Red, Blue, White,
  and RU Aggro.
- Source games are C16 mirrors at K=1/H=4, exploration `0.05`, maximum 500
  turns. No Handcrafted action, label, feature, or state enters the audit.
- Terminal weight is fixed at `0.50`. For every trace root and perspective,
  compute four arrays on the identical trace:
  - `RO4 = 0.5*z + 0.5*V(i+4 records)` (canonical control);
  - `RO8 = 0.5*z + 0.5*V(i+8 records)`;
  - `CT4 = 0.5*z + 0.5*V(earliest exact turn+4)` (TA4 treatment);
  - `CT8 = 0.5*z + 0.5*V(earliest exact turn+8)`.
  Roots without the requested future state retain `z`.
- Primary comparisons use only rows bootstrap-eligible under all four
  formulas. All-row metrics, terminal tails, and pair-specific common sets
  remain mandatory sensitivity reports.
- Report Brier, soft log loss, signed bias, target distribution, eligibility,
  and directly paired physical-game-clustered CR1 uncertainty for RO4, RO8,
  CT4, CT8, each contrast to RO4, CT8 versus each constituent, and the
  interaction `(CT8-CT4) - (RO8-RO4)`. Report record weighting and
  equal-actor-game weighting pooled, per deck, and at root turns `<=3`,
  `4-7`, and `>=8`.

This is load-only measurement. It may not call a trainer, write/refresh a
model, alter the parent artifact, or benchmark gameplay. One canonical CLI
route will accept no other option, build the complete corpus twice from
fresh `Game` instances, reduce in semantic schedule order, and require
bit-identical reports and hashes. Hidden-zone repartitioning must preserve
public observations, all four target arrays, eligibility, critic values, and
scoring hashes. Exact +4/+8 landings, earliest-index semantics, terminal
tails, Time Walk extra-turn numbering, deck/seat/play-draw counts, and
one-versus-N worker determinism require focused tests. Exit `0` is a
mechanism pass, `1` a complete scientific rejection, and `2`
infrastructure/incomplete evidence.

#### Fixed qualification and gates

Evidence is incomplete unless at least 3,000 of 4,000 actor-games, 560 of
800 for every deck, and 1,500 of 2,000 physical games contribute a
four-arm-common row, with at least 1,000 early-Green common records from 500
Green actor-games. RO4 early-Green signed bias must be positive with its
clustered lower 95% bound above zero; failure is an unqualified corpus, not a
license to choose another seed.

All of the following must pass:

1. **Early-Green primary:** on four-arm-common roots at turn `<=3`,
   `CT8-RO4` signed-bias delta is at most `-0.010`, its clustered 95% upper
   bound is below zero, and the equal-actor-game estimate is negative.
   CT8 absolute bias must be at least `0.005` smaller than both CT4 and RO8
   under record weighting and strictly smaller than both under equal-actor
   weighting.
2. **Interaction:** `(CT8-CT4) - (RO8-RO4)` early-Green signed-bias
   interaction is negative with its clustered 95% upper bound below zero.
   Whole-Green four-arm-common absolute bias must also shrink versus RO4
   under both weighting schemes.
3. **Pooled loss:** on four-arm-common rows, CT8 Brier and soft-log-loss
   deltas versus RO4 have clustered 95% upper bounds below zero. Versus CT4
   and RO8, both point deltas must be nonpositive and their upper bounds
   below `+0.001`. On all rows, pooled CT8 Brier must be no greater than
   RO4, CT4, or RO8.
4. **All-five guard:** for every deck, CT8 all-row Brier may be no more than
   `0.005` worse than each other arm. CT8 may create no material signed bias
   (`abs(bias) >= 0.05` with the physical-game-clustered interval excluding
   zero) unless RO4 already has same-sign material bias.
5. **Registered deck directions:** Blue `CT8-RO4` signed bias is positive
   and RU Aggro is negative under both record and equal-actor-game
   weighting, with absolute bias shrinking. Red and White must pass the
   no-new-material-bias rule.
6. Every coverage, schedule, target identity, tail, landing, earliest-index,
   hidden-information, artifact-immutability, repeated-construction, and
   deterministic-reduction check passes.

The 2,000-game size reuses TA4's observed precision: its early-Green
calendar contrast had clustered SE `0.000377` over 2,460 rows, far below the
new one-point MDE. CT8 has stricter eligibility, so the explicit actor-game
and row minima fail closed if that precision does not transfer.

If CT8-0 fails, seed `202607260621` is retired and the whole
record/calendar-horizon/terminal-weight/collection-horizon surface is closed:
no H6/H12, anneal add-on, best-cell promotion, or seed retry. The next axis
will be card-agnostic actor-game/calendar-turn-balanced replay weighting,
motivated by the already measured record-density imbalance.

Only a CT8-0 pass licenses a separately preregistered **CT8-only** full C16
recipe (constant terminal weight `0.50`, canonical H4 collection). That model
must then pass a fresh all-five held-out fitted-critic gate, the correctly
implemented frozen DRC regression check, and a paired frozen CT8-versus-C16
benchmark before any Handcrafted or certification seed is opened.

#### CT8-0 implementation freeze (seed still unopened)

Frozen at 2026-07-26 06:55 PDT after rereading `REVIEW.md` through its
06:47 cycle. The reserved seed `202607260621` and the exclusive
`--audit-calendar-eight-targets` route have not been run.

The implementation extends the established TA4 trace collector without
changing any legacy TA4 hash input or output field. On each identical
C16-mirror trace it now captures RO8 and CT8 targets, future indices,
physical-turn distances, exact-formula and tail identities, independent
target hashes, and hidden-repartition checks alongside the existing RO4 and
CT4 arrays. The new load-only evaluator:

- scores all rows, four-arm-common rows, every pair-specific common set, and
  four-arm-common `<=3`, `4-7`, and `>=8` root-turn strata;
- forms every target contrast and the factorial interaction row by row before
  physical-game CR1 clustering;
- separately reports record weighting and within-actor-game averaging,
  followed by clustering of both perspectives on the physical game;
- implements the declared coverage, early-Green qualification, MDE,
  constituent, interaction, pooled-loss, all-five safety, Blue/RU direction,
  and mechanical gates without threshold changes;
- loads and fingerprints only the exact immutable C16 artifact, constructs the
  entire corpus twice from fresh `Game` instances, and contains no trainer or
  artifact writer;
- emits human and stable TSV reports including all target distributions,
  effective sample counts, terminal-tail accounting, and the RO4/RO8
  physical-turn-distance histograms needed to audit the unit/reach mechanism;
  and
- returns `0` for a complete scientific pass, `1` for a complete scientific
  rejection, and `2` for missing/incomplete infrastructure or evidence.

An independent read-only code review found no blocking scientific correctness
issue. It confirmed direct paired interaction construction, equal-actor
aggregation, common-set semantics, exact gates, load-only behavior, hidden
information isolation, deterministic reduction, and TA4 compatibility. Its
two reporting findings were fixed before this freeze: equal-actor target
distributions now report actor-game rather than raw-record sample counts, and
the previously computed RO4/RO8 physical-turn-distance histograms are emitted
in both report formats.

Verification on the frozen implementation:

```sh
make test
./build/old-school-sim --games 5 --seed 42 --bots random
c++ -Iinclude -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  src/game.cpp src/learned_iteration.cpp src/probes.cpp \
  src/probe_eval.cpp src/probe_runner.cpp src/terminal_weight_eval.cpp \
  src/turn_alignment_audit.cpp src/target_factorial_audit.cpp \
  tests/test_target_factorial_audit.cpp \
  -o /private/tmp/old-school-target-factorial-audit-tests-asan-final
/private/tmp/old-school-target-factorial-audit-tests-asan-final
```

The full suite passed: 128 engine tests, 27 learned-iteration tests, 40 probe
tests, 11 probe-metric tests, 25 probe-runner tests, 10 terminal-weight tests,
10 TA4 tests, 7 CT8 tests, 9 web-bridge tests, the CLI and capture-once suites,
82 web tests, and 48 certification tests. The dedicated CT8 suite also passed
7/7 under ASan/UBSan. The representative 50-game, five-deck Random tournament
completed with no draws or turn-limit failures and printed all five deck
statistics.

One review notation disagreement is made explicit rather than silently
adopted. `REVIEW.md` 06:32 calls the predicted interaction “positive,” but
also predicts that CT8 will cut positive early-Green bias more than either
constituent. Under the frozen definition
`(CT8-CT4) - (RO8-RO4)`, that super-additive downward correction is
**negative**. The implementation therefore retains the preregistered
`mean < 0` and `upper95 < 0` gate. The review's 06:47 suggestion to move
directly to castability representation if early Green fails is useful as a
later axis, but it does not replace the already frozen failure branch:
actor-game/calendar-turn-balanced replay weighting will be tested first
because record-density imbalance is independently measured. Representation
work remains next if that weighting axis is falsified.

#### CT8-0 sealed result: rejected on RU absolute-bias shrink

Run exactly once at 2026-07-26 06:56 PDT from frozen implementation commit
`d6f4e2e`. Exact invocation:

```sh
sh tools/capture_once.sh \
  /Users/andrewjohnson/proj/magic-ai-vibes/build/experiments/ct8-0-sealed-audit-d6f4e2e \
  ./build/old-school-sim --audit-calendar-eight-targets
```

The process exited `1`, a complete scientific rejection rather than an
infrastructure failure. The immutable 470,034-byte, 2,776-line transcript is
`build/experiments/ct8-0-sealed-audit-d6f4e2e.complete.txt`, SHA-256
`33540418d0362241430399269e9e1d31fe0545be88b9261d336119da47cacfa6`.
Wall time was 82.36 seconds. The reserved seed was `202607260621`, generation
coordinate 19, with 50 balanced blocks: 2,000 physical games, 4,000
perspectives, and 800 perspectives per deck.

The exact parent fingerprint was
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.
Its artifact remained unchanged with evaluator content digest
`ee89a4aa1e91bbfc3d1ca6144139a86910bbe3e7d465de369f3b5e52471132fe`
and raw-file SHA-256
`53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca`.

All qualification and mechanical evidence passed. The two fresh complete
constructions were bit-identical. The corpus contained 107,804
trace-perspective rows; the four-arm-common set contained 63,018 rows from
3,998 actor-games and 1,999 physical games. Common actor-games by deck were
Green 799, Red 799, Blue 800, White 800, and RU Aggro 800. Early Green
contained 2,448 rows from 799 actor-games. All schedule, exact landing,
earliest-index, target-identity, terminal-tail, hidden-information, artifact,
and deterministic-reduction checks passed; 107,382 changed hidden-zone
repartitions preserved every observed quantity and hash.

Four-arm-common record-weighted Brier / signed bias:

| Scope | RO4 | RO8 | CT4 | CT8 |
| --- | ---: | ---: | ---: | ---: |
| Pooled | 0.020922 / -0.007240 | 0.016678 / -0.006202 | 0.019602 / -0.006695 | **0.014297 / -0.004429** |
| Green | 0.020618 / +0.030859 | 0.015318 / +0.019809 | 0.019261 / +0.024190 | **0.012957 / +0.006503** |
| Red | 0.025420 / +0.014130 | 0.020366 / +0.006265 | 0.024435 / +0.009687 | **0.018208 / -0.002784** |
| Blue | 0.021410 / -0.026451 | 0.017353 / -0.017318 | 0.019545 / -0.023882 | **0.013656 / -0.008429** |
| White | 0.015249 / -0.042497 | 0.012406 / -0.026420 | 0.013815 / -0.032292 | **0.010415 / -0.007243** |
| RU Aggro | 0.025598 / +0.007014 | 0.020823 / -0.002683 | 0.024707 / +0.002479 | **0.018853 / -0.009385** |

The primary early-Green hypothesis passed strongly. Record-weighted biases
were RO4 `+0.032105`, RO8 `+0.016445`, CT4 `+0.030960`, and CT8
`+0.007353`. `CT8-RO4` was `-0.024752`, 95% CI
`[-0.028520, -0.020985]`; the equal-actor estimate was `-0.024963`.
The registered row-level interaction was `-0.007946`, CI
`[-0.009889, -0.006004]`; its equal-actor estimate was `-0.008065`.
Thus CT8 cleared the one-point MDE, precision, constituent-advantage,
equal-actor, interaction, and whole-Green gates.

Pooled common-row loss also passed decisively:

- versus RO4, CT8 Brier delta `-0.006625`
  `[-0.007030, -0.006221]` and soft-log-loss delta `-0.017742`
  `[-0.018742, -0.016742]`;
- versus CT4, Brier `-0.005305` and log loss `-0.014191`, with both
  intervals wholly below zero; and
- versus RO8, Brier `-0.002382` and log loss `-0.006473`, again with both
  intervals wholly below zero.

On all rows, CT8's pooled Brier was `0.008357`, versus RO8 `0.010751`,
CT4 `0.013528`, and RO4 `0.014953`. The all-five Brier guard,
no-new-material-bias guard, whole-Green shrink, and Blue direction/shrink
gates passed. Blue common bias improved from `-0.026451` to `-0.008429`
under record weighting and from `-0.025564` to `-0.005570` under
equal-actor weighting.

The sole failed gate was preregistered and decisive. RU moved in the predicted
downward direction, but crossed zero far enough that absolute bias grew:

- record weighting: `+0.007014 -> -0.009385`, delta `-0.016399`;
- equal-actor weighting: `+0.004383 -> -0.008522`, delta `-0.012906`.

An independent result audit recomputed the transcript and artifact hashes,
checked every threshold, and confirmed that this was the only failed gate and
that exit `1` was correct.

Result: **REJECT CT8-0** and retire seed `202607260621`. The target-reach
hypothesis receives strong diagnostic support, but the conjunctive all-five
safety claim does not pass. Per the frozen decision rule, there is no CT8
model fit, C19 promotion, best-cell salvage, mixture, anneal add-on, H6/H12
retry, or further record/calendar-horizon/terminal-weight/collection-horizon
search. The next experiment will be separately preregistered
actor-game/calendar-turn-balanced replay weighting with canonical RO4,
constant-0.50 targets; no new experiment is active yet.

`REVIEW.md` was reread through its 07:02 cycle before recording this result.
That entry verified the frozen implementation but still described the seed as
unopened; it contains no post-result evidence that changes the sealed verdict.

### RB0-0: actor-game / exact-calendar-turn replay-weight census (declared)

Declared at 2026-07-26 07:22 PDT, before implementation and before opening a
new seed. `REVIEW.md` was reread through its 07:17 cycle. Its independent
reproduction verifies CT8-0's rejection and correctly identifies a
forward-looking gate-design lesson: a uniform card-agnostic correction should
not fail merely because a nearly unbiased deck crosses zero within a small
tolerance band. RB0-0 adopts that lesson prospectively with a fixed `0.010`
bias-safety band. It does not reopen or salvage CT8-0. The CT8 declaration
explicitly required user authorization to reopen its closed target family,
none has been given, and the review agrees that replay weighting therefore has
the floor. The review's FEAT-0 result also rejects castability as the next
representation candidate if RB0-0 fails.

#### Falsifiable hypothesis

The canonical critic currently gives every recorded trace state one training
unit. Long actor-games and turns with many priority/state records therefore
receive more gradient mass than short actor-games and sparse turns. On a
fresh, balanced frozen-C16 mirror corpus, this density imbalance materially
overweights optimistic early-Green canonical RO4 labels. Giving every
actor-game equal mass and every exact calendar turn within that actor-game
equal mass will reduce early-Green `RO4 - discounted-terminal` signed bias by
at least `0.005`, while remaining safe on Green, Red, Blue, White, and RU
Aggro.

RB0-0 is deliberately a load-only, zero-training mechanism test. A pass says
that the proposed training objective contains useful leverage; it does not
license a model, bot-strength claim, probe claim, or gameplay benchmark.

#### Frozen source and schedule

- Parent: the exact immutable Environment-v3 C16 artifact, T800, training seed
  `424242`, fingerprint
  `68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`.
  The canonical artifact loader must load it rather than retrain it.
- Audit seed: `202607260731`. It and generation coordinate `20` were searched
  in the working tree and all Git history and were unused at declaration.
- Schedule: 60 exact 40-game balanced blocks, or 2,400 physical games, 4,800
  actor-game perspectives, and exactly 960 actor-games for each of the five
  decks. Play/draw, seat, and ordered distinct-deck pairing remain balanced.
- Both seats use frozen C16 Value mirrors with K=1, H=4, exploration `0.05`,
  and a 500-turn maximum. No Handcrafted action, label, feature, state, or
  continuation enters the corpus.
- The only target is canonical
  `RO4 = 0.50*z + 0.50*V_parent(i+4 recorded states)`. The final four records
  of a trace, or every record of a shorter trace, retain discounted terminal
  target `z`.
- CT4, RO8, CT8, collection horizon, target reach, terminal weight, search
  policy, model features, and card rules are unchanged. The route may not call
  a trainer, write or refresh an artifact, score probes, or benchmark gameplay.

Each trace is viewed from both perspectives. For actor-game `a`, let `T_a` be
the number of distinct exact public `turn_number` values represented in its
trace, and let `n_(a,t)` be its number of records at turn `t`. With `N` total
rows and `A` actor-games, the two fixed weights are:

```text
control:   w_i = 1
treatment: w_i = N / (A * T_a * n_(a,t))
```

Mathematically, treatment mass is `N`, every actor-game receives `N/A`, and
each represented calendar turn within actor-game `a` receives
`N/(A*T_a)`. No clipping, cap, exponent, blend, dropping, resampling, phase
bin, deck weight, outcome weight, target-dependent weight, or other
normalization is permitted. Actor/game/turn identifiers are grouping metadata
only and may not enter critic features.

#### Fixed estimands, uncertainty, and qualification

Score `RO4 - z` signed bias, squared error, and soft log loss. Report control,
treatment, and treatment-minus-control on all rows and separately on rows with
an actual RO4 bootstrap future; report them pooled, for all five decks, and at
root turns `<=3`, `4-7`, and `>=8`. Also report actor-game length, distinct-turn
count, within-turn record multiplicity, terminal-tail accounting, weight
minimum/quantiles/maximum, Kish effective sample size, target and weight
distributions, and stable hashes.

Treatment weights are computed once on the complete corpus. For a reporting
scope `S`, they are renormalized only in that scope's mean: if `n=|S|` and
`W=sum_S(w_i)`, a metric contrast is
`mu_w - mu_u = sum_S(w_i*m_i)/W - sum_S(m_i)/n`. For physical game `g`, the
paired ratio-estimator cluster score is formed directly as
`U_g = sum_(i in g)[(w_i/W)*(m_i-mu_w) -
(1/n)*(m_i-mu_u)]`; CR1 is applied to the physical-game scores `{U_g}`.
Both perspectives share their globally unique physical-game cluster. This
uses the correct two-ratio influence function even when row counts and
treatment mass differ by game, and keeps the reported contrast equal to the
exact change in training-population weight rather than silently recomputing
new actor or turn weights per slice.

Evidence is incomplete unless at least 4,560 actor-games, 912 actor-games for
every deck, and 2,280 physical games contribute an RO4-eligible row. Early
Green must contain at least 2,500 eligible rows from 900 actor-games. Control
early-Green eligible signed bias must be positive with its physical-game CR1
95% lower bound above zero. All weights must be finite and positive; global,
per-actor, and per-turn mass identities must hold within
`64 * epsilon * max(1, expected_mass)`. Pooled Kish ESS must be at least half
the row count and every deck's ESS at least 40% of its row count.

The audit fixes 2,400 independent physical-game clusters and a `0.005`
minimum scientifically meaningful early-Green point effect before data. It
is a fixed-resource audit and makes no prospective power claim because this
weighting/standardization contrast has a different influence function from
CT8 and therefore cannot borrow CT8's cluster variance. The report must
publish its achieved clustered SE and `2.802 * SE`, labeled only as the
post-run approximate detectable effect under a normal approximation. The
point-effect gate and interval-sign gate remain separate; no post-result
sample-size or threshold revision is allowed.

#### Fixed scientific gates

Every gate must pass:

1. On RO4-eligible early-Green rows, the treatment-minus-control signed-bias
   delta is at most `-0.005`, its clustered 95% upper bound is below zero, and
   treatment absolute bias is smaller.
2. Whole-Green absolute signed bias strictly shrinks on both all rows and
   RO4-eligible rows.
3. On pooled all rows, the Brier and soft-log-loss contrast upper 95% bounds
   are below `+0.001`.
4. For every deck, on both all and RO4-eligible rows:
   - treatment absolute bias is at most
     `max(control absolute bias, 0.010)`;
   - Brier point delta is at most `+0.002` and its upper 95% bound is below
     `+0.003`;
   - soft-log-loss point delta is at most `+0.002` and its upper 95% bound is
     below `+0.003`; and
   - treatment creates no material bias (`abs(bias) >= 0.05` with its
     interval excluding zero) unless control already has material bias of the
     same sign.
5. Every schedule, target identity, terminal-tail, weight identity, hidden
   information, artifact, repeated-construction, and deterministic-reduction
   safeguard passes.

#### Required implementation safeguards and decision rule

- A synthetic balanced fixture with equal turn counts and one row per turn
  produces treatment weights bit-identical to `1.0`.
- A generic weighted reducer forced to unit weights is bit-identical to the
  legacy record-weighted reducer.
- Synthetic uneven fixtures prove exact equal actor-game mass, equal
  within-actor turn mass, global normalization, and scope-fixed weighting.
- Two complete fresh constructions from new `Game` instances must have
  bit-identical schedule, trace, outcome, feature, RO4-target, weight, and
  scoring hashes.
- Repartitioning the opponent's hidden hand/library must preserve the public
  observation, critic features and value, targets, grouping metadata, weights,
  and hashes.
- Reversing collector input and reducing with one versus N workers must
  produce the identical canonical report.
- The parent artifact's path, size, timestamp, and content digest are bound
  before and after. The exclusive CLI rejects training, writing, benchmark,
  probe, alternate-seed, and conflicting-mode options.
- Exit `0` means a complete scientific pass. Exit `1` means a complete
  scientific rejection, including a complete mechanically valid corpus that
  misses any declared coverage, positive-control, ESS, or scientific gate.
  Exit `2` is reserved for corruption, implementation failure, missing or
  changed artifact, or failed determinism/hash/mechanical validation.

Any completed run retires seed `202607260731`. A pass licenses only a
separately preregistered paired critic fit on a new common training shard and
a different held-out corpus: identical rows, targets, initialization,
ordering, optimizer, and RNG, with unit versus hierarchical weights as the
sole arm difference. The weighted update forced to all ones must reproduce
the legacy update fingerprint bit-for-bit, and no gameplay opens before the
held-out all-five/probe gates pass. A valid RB0-0 failure closes this replay
weight family and moves to a card-agnostic representation audit of removal
exposure or trajectory; FEAT-0 forbids reviving castability without new
evidence. Any complete mechanically valid corpus that misses qualification is
an unqualified scientific rejection: it retires the seed and closes this
declared replay-weight family rather than permitting a larger rerun. Exit `2`
still consumes and quarantines the seed but supports no mechanism claim. A
new-seed RB0-0 rerun may correct only the mechanical failure causally
responsible for exit `2`; its hypothesis, estimands, weights, scopes,
thresholds, qualifications, sample size, and analysis remain frozen. Any
substantive scientific change retires RB0-0 and requires a genuinely new
family and hypothesis rather than a rerun.

### RB0-0 implementation freeze (no audit data opened)

Frozen at 2026-07-26 08:04 PDT on implementation commit `6c44ae8`. The
reserved audit seed `202607260731` has not been executed. `REVIEW.md` was
reread through its 07:53 cycle before this freeze. Its independent FEAT-0b
result found zero early-Green board-exposure variance because the sampled
early boards were empty. That closes the proposed board-vulnerability
representation fallback on the reviewer's branch and strengthens the reason
to run RB0-0 next, but it is not evidence that replay weighting passes.

The frozen route is:

```sh
./build/old-school-sim --audit-replay-weights
```

It exclusively loads the exact C16 artifact and runs the declared 60-block,
2,400-physical-game corpus. It trains no model, writes no artifact, scores no
probe, invokes no Handcoded action or label, and accepts no other CLI option.
The scientific report uses the first canonical capture. Three additional
complete fresh captures isolate repeatability, reversed task input, and fixed
one-versus-four-worker determinism rather than confounding those checks.

The implementation includes:

- the exact complete-corpus
  `N/(A*T_a*n_(a,t))` actor-game/calendar-turn weights, global mass
  preservation, and global-weight/scope-renormalized estimands;
- the corrected paired two-ratio physical-game cluster score
  `U_g = sum_g[(w_i/W)*(m_i-mu_w) - (1/n)*(m_i-mu_u)]`, with variance
  `G/(G-1) * sum_g(U_g^2)` and no second mean division;
- bit-exact all-unit compatibility, RO4/tail identities, weight/multiplicity
  diagnostics, target distributions, Kish ESS, and stable schedule, trace,
  outcome, feature, grouping, target, weight, and scoring hashes;
- a genuine opponent hand/library repartition whose public observation,
  features, critic values, targets, grouping, complete-corpus weights, and
  target/scoring hashes must all remain identical;
- exact binding of every task to the declared seed/generation/block schedule,
  all 20 ordered distinct-deck pairing counts, both seats, and play/draw;
- the amended all-five bias, Brier, and soft-log safety gates, with coverage,
  positive-control, and ESS misses classified as complete exit-`1`
  unqualified scientific rejections rather than restartable infrastructure
  failures.

The first independent implementation review blocked the run before data on
three real proof gaps: hidden clones did not yet rebuild weighted scoring
records, schedule checks covered only marginals, and reversal/worker
determinism was confounded. All three were fixed. A second review then cleared
the reserved run with no remaining blocker. Its only API note—an unusable
default `CaptureConfig`—was removed before this freeze.

Verification on the frozen tree:

```sh
make test-replay-weight-audit
sh tests/test_cli.sh ./build/old-school-sim
make test
c++ -Iinclude -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  src/game.cpp src/learned_iteration.cpp src/probes.cpp \
  src/probe_eval.cpp src/replay_weight_audit.cpp \
  tests/test_replay_weight_audit.cpp \
  -o /tmp/test-replay-weight-audit-asan
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
  /tmp/test-replay-weight-audit-asan
```

Results:

- 13/13 dedicated replay-weight audit tests passed under `-Werror`;
- the same 13/13 dedicated tests passed under AddressSanitizer and
  UndefinedBehaviorSanitizer;
- CLI exclusivity, reserved-seed fencing, and missing-artifact/no-write guards
  passed;
- the full gate passed: 128 engine, 27 learned-iteration, 40 probe,
  11 probe-metric, 25 probe-runner, 10 terminal-weight, 10 TA4, 7 CT8,
  13 RB0, and 9 web-bridge C++ tests; CLI and capture-once tests; 82 Node/web
  tests; and 48 certification tests.

Next: capture the exclusive route exactly once with
`tools/capture_once.sh`, retain exit `0/1/2` without reinterpretation, reread
the newest independent review, and have a separate agent audit the sealed
transcript before recording the result.

### RB0-0 sealed audit (infrastructure failure; no scientific result)

The reserved audit was opened exactly once on 2026-07-26 from the complete
frozen tree at `9e2d48b` (the implementation itself entered at `6c44ae8`;
`9e2d48b` added the final notebook freeze). The exact command was:

```sh
sh tools/capture_once.sh \
  /Users/andrewjohnson/proj/magic-ai-vibes/build/experiments/rb0-0-sealed-audit-9e2d48b \
  ./build/old-school-sim --audit-replay-weights
```

The four declared captures all finished collection and each contained 129,280
trace-perspective rows, corresponding to 2,400 physical games and 4,800 actor
games per capture. The route then exited `2` at the aggregate infrastructure
guard before printing any scientific metric or gate table:

```text
Loading exact frozen Environment-v3 C16... done
Constructing RB0-0 audit corpus four times (2,400 games each: repeat, reverse, and fixed 1-vs-4 workers; K=1/H=4)... done (129280 trace-perspective rows per capture)
RB0-0 infrastructure/incomplete-evidence failure: RB0 audit mechanics, hidden-zone, artifact, or deterministic-reduction checks were incomplete
real 738.75
user 1324.09
sys 10.50
```

The immutable capture is
`build/experiments/rb0-0-sealed-audit-9e2d48b.complete.txt`; its independently
recomputed SHA-256 is
`8022e54f9e6d22a98a77ea20970b3a4edad6c257463f3f6daa5568fde9f847bf`, matching
the capture sidecar byte for byte. The exit sidecar contains exactly `2`.

Decision: **infrastructure failure, not accepted or rejected scientifically**.
No RB0 weighting estimand, loss, bias, confidence interval, qualification, or
gate result was exposed, so none may be inferred from this run. Seed
`202607260731` is consumed and quarantined permanently; it will not be rerun.
The transcript proves that artifact loading, all four full collections, report
construction, the 129,280-row accounting print, and the final artifact
snapshot completed. It narrows the fault to one or more nonthrowing booleans
inside the aggregate repeat/reverse/worker, schedule, weight, hidden-zone, or
mechanical invariant guard, but the generic fail-closed message does not retain
which boolean. The independent sealed-transcript audit confirmed that exact
non-identifiability: the report and gates were computed in process memory, but
the generic exception discarded every distinguishing flag before human/TSV
output, so no analyst opened or retained the scientific values.

The strongest static candidate is numerical, but is **not** promoted to a
result: this arm64 toolchain defines eight-byte, 53-bit `long double`
(`__SIZEOF_LONG_DOUBLE__=8`, `__LDBL_MANT_DIG__=53`), while the diagnostic
naively accumulates 129,280 positive double weights and tests the result
against a fixed 64-double-epsilon tolerance. The two-game collection test
cannot exercise that full-scale failure. Repeat/reverse/worker equality and
the other final nonthrowing flags remain equally compatible with the sealed
393-byte transcript.

`REVIEW.md` was reread through its 08:29 cycle before finalizing this
classification. Its OSC-1 continuation-only treatment was independently
rejected as a standalone challenger at 47.8% (95% interval 45.7–50.0), while
its registered Blue-mirror instrument improved to 55.9%. That supports the
user-derived continuation mechanism and a future jointly declared composite,
but it does not reinterpret this failed density audit.

Next: complete an independent read-only audit of the sealed transcript and
implementation. If the exact failed boolean is not recoverable after process
exit, add fail-closed per-invariant diagnostics and reproduce the failure only
with a nonreserved engineering seed. Any new RB0-0 sealed run must use a new
previously unused seed, may change only the causally established mechanical
defect, and must keep the declared hypothesis, estimands, weights, scopes,
thresholds, qualifications, sample size, and analysis frozen. Separately, the
user’s own-spell Counterspell, payable Force Spike, and Flying-Men chump reports
license a preregistered card-agnostic continuation-policy diagnostic; they do
not license silently changing Learned while the mechanism gate is still being
specified.

### RB0-E1 full-scale mechanical preflight (declared; no science)

This is an engineering diagnosis of RB0-0's proven exit-2 observability defect,
not a replay-weight experiment. Engineering seed `202607260902` and generation
coordinate `20` were searched in the working tree and all Git history and were
unused at declaration. It is explicitly not the quarantined RB0-0 seed and
cannot produce, accept, reject, or expose a scientific result.

#### Falsifiable mechanical hypothesis

On this arm64 host, `long double` has the same 53-bit mantissa as `double`.
RB0-0's naive full-corpus and per-group accumulation therefore exceeds at
least one fixed `64 * double-epsilon * max(1, expected_mass)` identity
tolerance at the declared 129,280-row scale. All exact weights themselves,
schedule/trace/RO4/tail/hidden identities, artifact binding, repeated
construction, reversed input, and one-versus-four-worker equality will pass.

#### Frozen preflight

Add a separate mechanical-only preflight executable and make target. It must:

- load only the exact immutable Environment-v3 C16 T800/S424242 artifact with
  fingerprint
  `68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`;
- use seed `202607260902`, generation `20`, 60 balanced 40-game blocks,
  K=1/H=4, exploration `0.05`, 500-turn bound, and the same canonical
  four-capture repeat/reverse/one-versus-four-worker construction as RB0-0;
- print only artifact identity, row/game counts, hashes, every named
  mechanical boolean, hidden changed-state count, weight finite/global/actor/
  turn flags, each maximum mass error with its unchanged 64-epsilon tolerance,
  and exact repeat/reverse/worker equality;
- never call `score_records`, `evaluate_gate`, a trainer, gameplay benchmark,
  probe scorer, or any human/TSV scientific report writer; never print bias,
  Brier, log loss, qualification, confidence interval, MDE, or gate values;
- snapshot the artifact before and after, remain deterministic under fixed
  input, and make failure flags observable rather than collapsing them behind
  one exception;
- have focused tests proving a synthetic failed invariant remains named in
  output and proving the route rejects the quarantined seed.

The exact first diagnostic command will be:

```sh
sh tools/capture_once.sh \
  /Users/andrewjohnson/proj/magic-ai-vibes/build/experiments/rb0-e1-arm64-mechanics-before \
  ./build/rb0-mechanical-preflight
```

The hypothesis passes only if the full preflight shows one or more weight-mass
identity failures while every other named mechanical flag passes. If confirmed,
the only permitted fix is deterministic compensated or pairwise accumulation
inside diagnostics; calculated row weights, 64-epsilon thresholds, estimands,
scopes, gates, and sample size remain byte-for-byte unchanged. Add a
>=129,280-row adversarial numerical regression, then rerun the same engineering
seed under a different capture prefix to isolate that mechanical fix. If the
hypothesis fails, fix only the different exact flag exposed by the preflight.
Only a clean post-fix preflight can license preregistration of one fresh sealed
RB0-0 seed; it cannot itself license a learning change or mechanism claim.

#### Pre-fix result: mechanical hypothesis confirmed

Run 2026-07-26 from committed source `fe24471` after rereading `REVIEW.md`
through its 10:17 cycle. The exact declared command was:

```sh
sh tools/capture_once.sh \
  /Users/andrewjohnson/proj/magic-ai-vibes/build/experiments/rb0-e1-arm64-mechanics-before \
  ./build/rb0-mechanical-preflight
```

The embedded configuration was seed `202607260902`, generation `20`, 60
balanced blocks = 2,400 physical games and 4,800 actor-games per capture,
K=1/H=4, exploration `0.05`, and four canonical/repeat/reverse/one-worker
captures. Each capture contained exactly 128,922 trace-perspective rows and
zero rootless actor-games. The exact C16 artifact remained unchanged with
parent fingerprint
`68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`
and content digest
`ee89a4aa1e91bbfc3d1ca6144139a86910bbe3e7d465de369f3b5e52471132fe`.

The hypothesis was confirmed exactly. In all four captures the naïvely summed
global mass missed its identity by
`2.1245796233415604e-08`, exceeding the unchanged
`1.8320918115932727e-09` tolerance. Actor mass passed with maximum error
`3.1974423109204508e-14` versus tolerance
`3.8168579408193183e-13`; turn mass passed with maximum error
`4.4408920985006262e-16` versus tolerance at that row
`2.2452105534231285e-14`.

Every other named invariant passed: artifact snapshots and schema; exact
game/actor/row accounting; schedule balance and all hash shapes; trace, RO4,
and terminal-tail identities; hidden repartition, changed-state, grouping,
target, weight, and scoring identities; finite-positive weights; actor and
turn mass; and full repeat/reverse/one-versus-four-worker capture equality.
Only `weight.global-mass`, its aggregate `weight.identity`, and therefore
`mechanical.complete` failed. The route exited `2`, as required for a named
mechanical diagnosis; it exposed no scientific metric or gate.

The immutable transcript is
`build/experiments/rb0-e1-arm64-mechanics-before.complete.txt`, SHA-256
`049f05d1b4cdcc800580b2f9a5a2a11ef87917e86a6aa65cfd83fa5b33fa43bc`;
the independent sidecar matches and records exit `2`. Wall time was 535.03
seconds (`user 1179.94`, `sys 6.74`).

Decision: apply only deterministic compensated accumulation to the diagnostic
global/actor/turn mass summaries. Do not change hierarchical row weights,
their hashes, the 64-epsilon tolerance, squared-weight/Kish calculation,
scientific accumulation, estimands, scopes, gates, schedules, or sample size.
Add the declared >=129,280-row adversarial regression first. The exact
post-fix engineering capture, using the same permitted engineering seed but
a new collision-free prefix, is:

```sh
sh tools/capture_once.sh \
  /Users/andrewjohnson/proj/magic-ai-vibes/build/experiments/rb0-e1-arm64-mechanics-after-compensated \
  ./build/rb0-mechanical-preflight
```

It passes only if every named invariant passes and the exact row-weight,
schedule, trace, RO4, target, feature, grouping, scoring, and artifact hashes
remain those printed above. A clean result licenses only a fresh RB0-0 sealed
seed declaration.

#### Post-fix result: mechanical preflight passed

Run 2026-07-26 from committed repair `e484fd3` after independent code review
and after rereading `REVIEW.md` through its 10:32 RB0-diagnosis cycle. The
exact prospectively frozen command was:

```sh
sh tools/capture_once.sh \
  /Users/andrewjohnson/proj/magic-ai-vibes/build/experiments/rb0-e1-arm64-mechanics-after-compensated \
  ./build/rb0-mechanical-preflight
```

The route exited `0` with all 97 named mechanical records passing. In every
capture, compensated global mass error is exactly `0` against the unchanged
`1.8320918115932727e-09` tolerance; actor error is
`3.5527136788005009e-15` against
`3.8168579408193183e-13`; turn error remains
`4.4408920985006262e-16` against
`2.2452105534231285e-14`. All artifact snapshots, accounting, schedule,
trace/RO4/tail, hidden-repartition, finite-weight, repeat, reverse, and worker
flags pass.

Configuration, artifact metadata, all four count rows, and all 32
schedule/trace/outcome/feature/grouping/RO4/weight/scoring hash rows are
byte-identical to the pre-fix transcript. Independent SHA-256s over each
capture's eight hash rows match before and after:

- canonical:
  `179d55c0f2ec64284d0a4765d31005f8809b979134e8ef76d6545e615eb53631`;
- repeat:
  `48c64d73bb8469dcd7e6dc30064e0e61656f6127279ed3edce4f5934dc1d10e9`;
- reverse:
  `6c2970c5da865886d093537f36739397b6c844f318e10f6f49c14a40ec2975c5`;
- single-worker:
  `a62f46faa3d0261ccd8a22f6d4a52a5ade94c70e6dc13f705961e79f685e2df4`.

The immutable post-fix transcript is
`build/experiments/rb0-e1-arm64-mechanics-after-compensated.complete.txt`,
SHA-256
`4432f4a20da98588aac5de58b6339fbdeb3a1029e946dab5d62b38eaf1a0e871`;
the independent sidecar matches and records exit `0`. Wall time was 524.11
seconds (`user 1177.10`, `sys 6.62`).

Decision: the observability/numerical defect is repaired. This licenses
declaration of one fresh RB0-0 sealed seed with the original hypothesis,
estimands, weights, scopes, gates, thresholds, qualifications, sample size,
and analysis unchanged. It does not itself support replay weighting or any
Learned-policy change.

### Calendar-8 composite authorization (user delegated; accepted)

Recorded 2026-07-26 after rereading `REVIEW.md` through its 08:32 cycle. The
user explicitly delegated the closed-family decision: “i dont know if we
shiuld reopen, u decide.” We decide to reopen calendar-turn-eight targets
**only as one fixed component of a future jointly preregistered multi-family
composite**, not as a standalone CT8 retry, best-cell salvage, or threshold
reinterpretation.

The evidence for that narrow decision is unusually convergent:

- sealed CT8-0 cut early-Green bias from `+0.032105` to `+0.007353`, with
  `CT8-RO4=-0.024752` and a 95% interval wholly below zero;
- it improved pooled loss and every registered constituent comparison, fixed
  Blue pessimism, and passed every mechanical and all-deck material-bias guard;
- its only miss was RU crossing from a small positive bias to a small negative
  bias, increasing absolute bias by only `0.002371` beyond the later
  prospectively justified `0.010` safety band;
- independent OSC-1 then confirmed a disjoint continuation mechanism:
  self-counter pruning raised its registered Blue mirror to 55.9%, although
  the standalone five-deck challenger remained rejected at 47.8%.

This authorization does not promote CT8, continuation pruning, or replay
weighting individually. The future composite may include calendar-8 and the
confirmed card-agnostic continuation component. It may include hierarchical
replay weighting only if a repaired, newly sealed RB0 audit passes its frozen
mechanism gates. Before any fit, the exact composite recipe, common training
shard, artifact identity, offline all-five gates, direct C16 comparison,
Handcoded milestone, sample size, MDE, and fixed eight-seed certification
panel must be declared together. Green, Red, Blue, White, and RU Aggro retain
equal status; the final Learned-is-king thresholds are unchanged.

### PD0: exact Pass-dominance Learned filter (declared)

Declared 2026-07-26 after the user observed old-web G0/K2 cast Braingeyser
targeting itself for X=0. `REVIEW.md` was reread through its 08:32 cycle before
this declaration. Diagnostic seed `202607260947` and large-regression smoke
seed `202607260948` were searched in the working tree and all Git history and
were unused.

Braingeyser X=0 is legal Old School Magic and remains in the engine's legal
action set. The report has two separable causes: choosing any X=0 branch over
Pass is a hold-versus-waste value error; choosing self rather than opponent is
an exact tie because X=0 has the same resolved effect on either player and the
state-only Value critic does not encode a noncreature spell's player target.
The older Disintegrate X=0 field report showed the same no-effect spend class.
The existing eval-only DC1 canonical-settlement comparator already proves
Pass strictly dominates Disintegrate X=0 while payable Force Spike is
incomparable, with exact ledgers and hidden-repartition checks.

#### Falsifiable hypothesis

A card-agnostic exact-settlement filter can remove actions strictly dominated
by Pass at both deployed Learned Value roots and its K=0 Value continuations.
It will eliminate Braingeyser/Disintegrate X=0 resource burns without changing
legal Magic actions, filtering payable Force Spike, filtering productive X
spells, peeking at hidden information, using card names/values, or causing a
large all-five gameplay regression.

#### Frozen treatment and controls

- Add an explicit research flag, default off. Off must remain bit-identical and
  consume no RNG. The filter applies only to Learned Value priority selection,
  at the deployed root and its depth-zero Value-mirror continuations. It does
  not change `legal_priority_actions`, resolution, Human, Random, Monte Carlo,
  Deep Monte Carlo, HandcodedPolicy, or Learned Actor.
- When Pass is legal, settle Pass once and each non-Pass candidate through the
  same bounded forced-pass stack/window procedure to an identical comparison
  context. Compare only information available to the acting learner: its own
  hand identities and sizes plus public zones/state. Opponent hidden hand and
  library identities may not enter the comparator.
- A candidate is filtered only if every non-cost observable effect and stop
  context is identical to Pass, its rules-derived resource ledger is
  componentwise no better, and at least one resource component is strictly
  worse. Opponent resource or public-state changes make branches incomparable.
  Pass and every incomparable action are always retained. No card ID/name,
  handcrafted value, target-specific weight, learned prediction, or stochastic
  threshold may enter this proof.
- Reuse or production-neutrally extract the already tested DC1 settlement and
  ledger semantics; production engine code must not depend on probe code.
  Bound work explicitly and reject malformed/nonsettling comparisons rather
  than filtering them.

Focused acceptance fixtures:

1. A Blue second-main state exposes Pass, Braingeyser X=0 targeting each
   player, and productive X=1 targeting self. Both X=0 actions must be proved
   strictly dominated and filtered; X=1 must remain incomparable and legal.
2. Existing nonlethal Disintegrate X=0 is dominated, while lethal/productive X
   remains.
3. Live and payable Force Spike remain incomparable and selectable.
4. Counterspell targeting the learner's own useful creature spell remains
   incomparable; own-counter cleanup is the separately confirmed OSC
   controller-relation component, not a reason to weaken this exact proof.
5. Opponent hand/library repartition preserves comparator result, filtered
   option keys, selected action, and deterministic hashes.
6. Default-off G0/K2 and frozen C16/K8 action scores/selections remain
   bit-identical. Treatment-on reports their before/after option sets and
   cannot select an action the comparator proved dominated.
7. From a priority root immediately after the learner has put one
   Counterspell above an opposing Air Elemental, a second Counterspell
   targeting that same Air Elemental is Pass-dominated: both forced-pass
   branches remove the Air Elemental, while the second-counter branch spends
   one additional card and two additional blue mana and leaves that redundant
   Counterspell to fizzle. A response targeting a different stack object whose
   survival changes the settled public state remains incomparable.

This seventh fixture is a prospective field-report amendment made before
either reserved PD0 seed was consumed. The user observed the exact redundant
double-counter stack in the C16 web client after this experiment was first
declared; the independent `REVIEW.md` 09:02 cycle then measured roughly six
deployed redundant counters per 20-game K=8 mixed run. The observation does
not change the treatment, comparator, thresholds, commands, or interpretation:
it is a new validation case for the already frozen card-agnostic settlement
proof, not a Counterspell-specific rule.

The diagnostic uses seed `202607260947` only for paired information-set worlds
and reports G0/K2 plus exact frozen C16/K8. This is a mechanism/proof gate, not
a claim that C16 already handles Braingeyser correctly. Add an exclusive
`--diagnose-value-pass-dominance` route: it accepts only the exact diagnostic
seed, trains deterministic G0 T800/S424242 in memory at K=2, load-only checks
the exact frozen C16 T800/S424242 fingerprint at K=8, writes or refreshes no
artifact, and prints before/after fixture option sets, selections, dominance,
and hidden-information checks. Exit `0` is a complete mechanism pass, `1` a
complete mechanism rejection, and `2` infrastructure/incomplete evidence.
The exact command is:

```sh
./build/old-school-sim --diagnose-value-pass-dominance \
  --seed 202607260947
```

The smoke seed `202607260948` is reserved for exactly two 240-game paired
all-five configurations, run in this fixed order. First, the unfiltered
C16/K8 control:

```sh
/usr/bin/time -p ./build/old-school-sim --benchmark --games 4 \
  --seed 202607260948 --train-seed 424242 --train-games 800 \
  --challenger learned-value-c16 --baseline learned-value-c16 \
  --learned-rollouts 8
```

Second, the otherwise identical challenger-only treatment:

```sh
/usr/bin/time -p ./build/old-school-sim --benchmark --games 4 \
  --seed 202607260948 --train-seed 424242 --train-games 800 \
  --challenger learned-value-c16 --baseline learned-value-c16 \
  --learned-rollouts 8 --value-pass-dominance
```

This prospective command clarification was made before the smoke seed was
used. The CLI must accept the seed for exactly these off/off and on/off
configurations; it must reject every other use. The treatment flag applies to
the challenger seat only.

The paired smoke is sensitive only to a large effect. Reject the treatment if
its aggregate win rate is below 40%; do not accept or reject a few-point change
or a 48-game deck slice. Also compare fixed-work wall time to identical C16/K8
control and reject an overhead above 25%. Passing every exact fixture,
hidden-information/default-off safeguard, and the large-regression/runtime
screen accepts PD0 only as a candidate component for the jointly declared
composite. It does not promote a bot or relax any five-deck Learned-is-king
gate.

### Audit-common mechanical extraction (declared)

Declared 2026-07-26 after the user requested deduplication of the frozen audit
helpers and after rereading `REVIEW.md` through its 09:32 cycle. This is a
mechanical source cleanup, not a learning experiment, audit rerun, or
reinterpretation of any sealed result.

Falsifiable hypothesis: a leaf-only `audit_common` layer can replace the
byte-identical or semantically identical copies of deterministic formatting,
TSV sanitization, probability validation, bit-identity checks, content hashing,
optional-index/observation hashing, lower-hex digest validation, and
64-epsilon mass tolerance without changing one byte of existing audit-test
output or any focused audit/preflight result. Frozen serializers retain their
existing field order and domain tags; scientific reducers, estimands, gates,
thresholds, seeds, schedules, and CLI routes are untouched.

Before editing, rebuild and capture the stdout SHA-256 of the four existing
audit test binaries. After editing, rebuild them and require exact equality:

```sh
make test-terminal-weight-eval test-turn-alignment-audit \
  test-target-factorial-audit test-replay-weight-audit
for test_binary in \
  old-school-terminal-weight-eval-tests \
  old-school-turn-alignment-audit-tests \
  old-school-target-factorial-audit-tests \
  old-school-replay-weight-audit-tests
do
  zsh -o pipefail -c \
    "./build/$test_binary | shasum -a 256"
done
make test-rb0-mechanical-preflight
```

Add focused common-helper characterization tests, keep `-Werror` clean, and
run the affected audit tests under ASan/UBSan before accepting the extraction.
Any changed stdout hash, digest, report row, invariant, or test result rejects
the cleanup until the exact mechanical discrepancy is explained.

The proposed `old-school-audits` binary split is deliberately deferred. TA4,
CT8, and RB0 have frozen commands naming `old-school-sim`; moving those routes
while PD0 and the repaired RB0 are live would widen this cleanup into a CLI and
evidence-contract migration. Reconsider the split only after the current
preflight and PD0 captures, under its own command-compatibility declaration.

#### Result: accepted

Completed 2026-07-26 after rereading `REVIEW.md` through its 10:02 cycle.
The extraction moved deterministic real formatting, TSV sanitization,
probability validation, bit-identity comparisons, content hashing,
scheduled-task/optional-index/observation hashing, lower-hex validation,
soft log loss, strict-sign comparison, 64-epsilon mass tolerance, and the
shared TSV estimate-row formatter into `audit_common`. The last byte-identical
TA4/RB0 `hash_task` adapters and Terminal/TA4/CT8 estimate-writer lambdas were
also replaced by tested common adapters. Audit-specific record serializers,
domain tags, schedules, collectors, reducers, gates, thresholds, and report
skeletons remain local and unchanged. RB0's prose `write_estimate` is not the
same serializer and deliberately remains local.

The four pre-edit and post-edit stdout SHA-256 values are exactly equal:

- terminal-weight evaluator:
  `ed3ea3b065dba195a9498683b363f02b05636b4d37e021c5f90ee9bd41031be0`;
- TA4:
  `da50ff56bc7d3aa74cf51ab6e5c63e0954825125a1099c5482a7b3e10cb97d4d`;
- CT8:
  `1f40f81cad6f33326b2282b8c8c915146ba24a2f0b4e1a2aff2393861841ecf0`;
- RB0:
  `68d0cd78cc5311afef63478dae7a34c1ce007b5b6651ed82b1a64f4e9641e380`.

The focused strict suites passed 8/8 common-helper tests, 10/10 terminal,
10/10 TA4, 7/7 CT8, 13/13 RB0, and 6/6 mechanical-preflight tests. The
simulator and preflight executables built C++20 `-Werror` clean. All six
focused suites independently rebuilt at `-O1 -g` with
`-fsanitize=address,undefined -fno-omit-frame-pointer` and passed with
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`, with no sanitizer diagnostic.

`make test` then passed the full current matrix: 131 engine/bot, 27
learned-iteration, 40 probe-corpus, 11 probe-metric, 25 probe-runner, 8
common-helper, 10 terminal, 10 TA4, 7 CT8, 13 RB0, 6 preflight, 12 web-bridge,
90 web-client, and 48 certification tests, plus CLI and capture-once suites.
The representative command
`./build/old-school-sim --games 5 --seed 42 --bots random` completed all ten
five-deck matchups without a draw or turn-limit failure. No audit route,
preflight capture, reserved diagnostic, or scientific seed was run.

The four large audit implementations fell from 8,807 to 8,285 lines; the
common production module/header add 286 lines, for a net 236-line production
reduction before counting the new characterization tests. A read-only split
assessment also confirmed that the five audit translation units contribute
about 352,720 bytes, or 17.36%, of the simulator's current text segment, so a
separate `old-school-audits` binary remains worthwhile. It is still deferred:
the repaired RB0 declaration canonically names
`./build/old-school-sim --audit-replay-weights`. After that route is consumed
or retired, migrate the four audit flags together, preserve reserved-seed
fencing in the simulator, split the CLI contract tests, and make the simulator
emit a direct moved-command error. Do not add a process-exec compatibility
shim or rewrite historical commands.

Decision: accept this only as a source cleanup. Exact frozen evidence output
and all scientific semantics are unchanged. The next research events remain
the cleared RB0-E1 mechanical capture and the frozen PD0 diagnostic/smoke.
