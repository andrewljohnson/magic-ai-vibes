# AGENTS.md

## Project goal

Build a fast, deterministic early-Magic engine and a sequence of increasingly
strong bots. Preserve correct game rules and credible bot evaluation while
keeping the implementation small enough to understand and extend.

## Required workflow

1. Read `EXPERIMENTS.md` before changing bot logic.
2. State a falsifiable hypothesis before a meaningful tuning experiment.
3. After every meaningful benchmark or stability run, update
   `EXPERIMENTS.md` with:
   - the code/configuration tested;
   - the exact command, seed, and sample size;
   - aggregate and per-deck results;
   - whether the change was accepted or rejected;
   - the next experiment.
4. Keep failed experiments in the notebook. Do not rewrite history to show
   only successful runs.
5. Use the paired benchmark for bot-strength claims. It must balance deck,
   policy seat, and play/draw.
6. Use seed `424242` and 200 paired games only as a large-regression smoke
   screen. Do not accept or reject effects of a few percentage points from
   that screen or from its 50-game deck slices.
7. Validate milestone candidates with a frozen trained model, an explicitly
   separate evaluation seed, and a predeclared sample size/MDE. Use at least
   2,000 paired games for claims about roughly three-point effects, then run
   the fixed evaluation-seed panel `101, 202, 303, 404, 505, 606, 707, 808`.

## Evaluation discipline

- Training randomness and evaluation randomness must have separate seeds and
  both must be reported. A benchmark seed must not silently retrain a new
  model.
- Preserve the strongest accepted clean policy as the champion. New research
  policies remain challengers until they beat the champion under the full
  gates; never replace a champion because of one small development screen.
- Before end-to-end tuning, maintain held-out, deck-balanced decision probes
  labeled by a deeper information-set reference. Report policy ranking
  agreement per deck plus value loss/calibration when changing learning code.
- Treat 200 paired games as sensitive only to large changes (about ten
  percentage points near a 50% win rate). Prefer fast offline metrics for
  iteration and reserve 2,000+ paired games for milestone decisions.
- Compare trained generations against frozen previous generations or a
  champion league. Touch Handcrafted only at milestones, not as training data
  or a per-step tuning oracle.

## Learned Value isolation

Learned Value is the clean learned-policy research bot.

- Card identities may be encoded as neutral input features for the learner's
  own hand and public zones. The network must learn their values from data.
- Never expose opponent hand identities; only public information such as its
  hand size is available.
- Do not use card names, card-specific policy switches, hand-authored feature
  weights, or `handcrafted_card_value` in its policy.
- Do not train it against Handcrafted or use Handcrafted as its search
  opponent.
- Learned search assumes a Learned mirror opponent.
- Rules-level information is allowed: legal actions, card identity, own-hand
  contents, public zones, mana costs, card types, power/toughness, targets,
  and resolved state changes.
- Random play, Learned self-play, terminal outcomes, temporal-difference
  targets, and the learner's own search choices are valid training data.
- If a proposed experiment would weaken this isolation, document it as a
  separate bot instead of silently changing Learned Value.

## Definition of a stronger Learned Value bot

For the current four-deck environment, acceptance requires:

- more than 50% direct wins against Handcrafted in aggregate;
- a Wilson 95% lower confidence bound above 50% in the final pooled paired
  benchmark;
- more direct wins than Handcrafted for every Learned challenger deck in the
  pooled results;
- the largest mixed-field win-rate lift over Random for Learned on Green,
  Red, Blue, and White in the seeded stability run;
- no losing validation seed in aggregate;
- all unit/integration tests and sanitizer checks passing.

## Build and verification

Common commands:

```sh
make
make test
make stability
./build/alpha-sim --benchmark --games 20 --seed 424242 \
  --challenger learned --baseline handcrafted --train-games 800
```

Use C++20 and keep `-Wall -Wextra -Wpedantic -Werror` clean. Before declaring
work complete, run the full test suite, a representative CLI simulation, the
bot validation harness, and AddressSanitizer/UndefinedBehaviorSanitizer.

## Change discipline

- Preserve deterministic behavior for a fixed seed.
- Prefer bounded search and explicit limits so high-volume simulation remains
  practical.
- Keep rules, policies, training, and reporting separable.
- Add tests for new engine behavior and regression tests for fixed bugs.
- Never weaken a test or acceptance threshold merely to make an experiment
  pass.
