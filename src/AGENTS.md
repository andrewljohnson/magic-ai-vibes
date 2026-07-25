# Engine and Bot Implementation Notes

These instructions apply to files under `src/`.

## Rules engine invariants

- Legal action generation is the authority; policies choose only from those
  actions.
- Spells and abilities use the stack. Both players receive priority and two
  consecutive passes are required to resolve the top object or end a window.
- A player who takes an action receives priority again.
- Counterspell targets a stack object, not a card or player.
- Preserve deterministic PRNG use for fixed seeds.
- Rules changes must not be hidden inside a bot policy.

## Policy boundaries

- `Handcrafted` may use explicit hand-written card knowledge.
- Monte Carlo variants use legal random continuations.
- `Learned` follows the isolation rules in the repository `AGENTS.md`.
- Keep search budgets in `BotConfig` and account for rollout work in reported
  bot statistics.

## Performance

This is a simulation engine. Avoid unbounded trees, accidental quadratic
copies in inner loops, and recursive search without an explicit horizon.
Measure promising policy changes with both win rate and rollouts/decisions per
game.
