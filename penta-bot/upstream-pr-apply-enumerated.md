# Add `Game::apply_enumerated` for callers that already enumerated

**Open at:** https://github.com/andrewljohnson/penta/pull/new/apply-enumerated

## What

`apply` validates through `is_legal_action`, which for an ordinary action is
`legal_actions(player).contains(action)` — a second full enumeration of the
list the caller usually just chose from.

`apply_enumerated(player, legal, action)` validates by containment against a
list the caller supplies instead. This is the same shortcut
`apply_observed_action` already takes for callers holding a
`PlayerObservation`, offered to callers holding only the action list.

## Why

A search picks its move from `legal_actions` a moment before applying it, so
every ply enumerates twice and discards one. Enumeration is the expensive
part. Profiled on a native ISMCTS, protocol 29, one episode of ~44,000 plies:

| | share of search time |
|---|---|
| `legal_actions` | 28% |
| `apply` | 44% |

Routing the apply through the already-enumerated list cut **total search time
by 30%** (7.7s → 5.4s on the profiled episode).

A native search does not build an observation for a ply it has no choice at —
**63% of plies in that episode had exactly one legal action** — so it holds
the action list but not a `PlayerObservation`, and cannot use
`apply_observed_action`.

## Safety

- `ChooseDecision` still goes through full validation, because a decision
  observation exposes a bounded selection schema rather than every
  combination, so the submitted options must be checked directly.
- An action absent from the supplied list is refused with
  `ActionError::NotLegal`, so a stale list cannot smuggle an illegal action
  through — there is a test for exactly this.
- The caller must not mutate the game between enumerating and applying, the
  same contract `apply_observed_action` documents.
- Additive: `apply` is untouched and the protocol epoch is unaffected.

## Tests

- `apply_enumerated_matches_apply` — accepts what `apply` accepts and reaches
  the same position (same legal actions, step, priority).
- `apply_enumerated_rejects_an_action_outside_the_list`.

`cargo clippy --locked --all-targets -- -D warnings` and `cargo fmt --check`
are clean.
