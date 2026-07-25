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
