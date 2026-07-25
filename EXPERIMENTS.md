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
