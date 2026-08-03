# AGENTS.md

## Project goal

A fast, deterministic Old School Magic engine, a browser arena, and a
ladder of bots: Random, Monte Carlo, Deep Monte Carlo, the Handcoded
Policy baseline, and the Self-Play Zero (SPZ) champion. Keep rules
correct, evaluation credible, and the implementation small.

## Build and verification

```sh
make            # binaries
make test       # every C++ suite plus the web tests
```

Use C++20 and keep `-Wall -Wextra -Wpedantic -Werror` clean. Preserve
deterministic behavior for a fixed seed.

## New-deck protocol

Every new deck addition ends with two verification passes, not just
compiling tests:

1. **Watch it play.** Generate replays of the new deck under Random,
   Handcrafted, and SPZ pilots against several opponents (the replay
   tool + scout digests). Read the game logs for weird behavior:
   uncastable cards, never-activated abilities, misfiring triggers,
   nonsense sequencing. A deck is not "added" until someone has
   actually observed it being played sensibly.
2. **Make Handcrafted competent with it.** The rules bot is the
   benchmark opponent and the only sanctioned home for card strategy:
   give its valuations and action scores whatever the new cards need
   (mulligan source lists, cast priorities, ability usage) and check
   its win rate with the deck against the field is not embarrassing.

## No card-specific behavior patches

Do not hard-code narrowly targeted play rules into the learned bot
(e.g. "never animate Mishra's Factory in this phase"). When the bot
misplays a card, find the general mechanism — a search inconsistency,
a feature the net cannot observe, a credit-assignment gap — and fix
that, gated by a paired experiment. Card-specific prunes hide the
defect, rot as the pool grows, and teach us nothing. The Handcrafted
baseline bot is the only place card-specific strategy belongs.

## Evaluation discipline

- Use the paired benchmark (`selfplay-zero benchmark`) for bot-strength
  claims; it balances deck, seat, and play/draw and reports Wilson bounds
  plus a pilot-skill comparison per deck.
- Development screens (a few hundred games) detect only large effects;
  reserve 2,000+ paired games on a fresh seed for milestone claims.
- Training seeds and evaluation seeds must be separate and reported.

## SPZ isolation

SPZ never uses hand-authored card values or card-specific policy
switches. It consumes perspective-safe observations only (its own hand,
public zones, both decklists) plus public game events, and it learns
exclusively from self-play outcomes. Rules-level pruning (pass-dominance,
no-upside-versus-pass) is allowed; card knowledge is not.

## Web client

- The engine owns legal actions and hidden information; the browser must
  not recreate rules or expose private card identities.
- Run `make test-web` for bridge/server changes and `make test-web-ui`
  for client changes.
