# Old School Arena web roadmap

This is the durable track for making the browser game playable, legible, and
trustworthy. Update the status and evidence log whenever web behavior changes.
A green data-contract test is necessary, but it does not by itself prove that
React rendered the state clearly.

The web track uses three verification levels:

1. **Contract** — structured state, legal-choice plumbing, session lifetime,
   and hidden information are checked without a browser.
2. **Structural UI** — the production client builds and focused source/layout
   invariants guard known regressions such as hand/action overlap.
3. **Rendered journey** — a real browser demonstrates that the controls are
   visible, readable, and usable at the target viewport.

Only level 3 can close a visual milestone. The first two levels are fast gates,
not substitutes for looking at and playing the game.

## Current priorities

### P0 — A player can always see and use their hand

Status: **complete**

Acceptance criteria:

- A newly started game shows every card in the human hand without scrolling
  the whole page or opening debug mode.
- Human hand cards have readable names and mana costs at a 1440 × 900 viewport.
- Legal hand actions are visibly actionable; illegal cards do not look
  clickable.
- Playing a land or casting a spell removes the right card from hand and
  updates the battlefield or stack immediately.
- The opponent hand stays hidden unless `Debug reveal` was explicitly enabled.

The contract proves that all seven face-up card objects reach the client, the
structural gate proves that the hand is not truncated and has a separate
layout row from the action tray, and the repaired client has passed fresh
rendered checks at both target viewports.

### P1 — Complete, unambiguous game flow

Status: **adapter journey, real-engine smoke, and rendered stack/hand flow
green; full combat/result journey pending**

Acceptance criteria:

- Setup exposes all five decks and all six opponent policies.
- The current player, phase, priority holder, and required choice are obvious.
- Land play, spell cast, priority pass, stack resolution, attackers, blockers,
  damage order, game over, and rematch can each be completed without guessing.
- Clicking a playable hand card opens a large, non-committing inspection view
  with links to every corresponding engine action; Escape closes it.
- A playable hand card can be dragged onto any matching exact legal action,
  including separate target-specific choices, while illegal cards and
  unrelated actions never become drop targets.
- An action can be submitted only once; stale or illegal actions produce a
  recoverable explanation.
- Game over names the winner and reason, and rematch starts a fresh session
  while retaining the selected matchup.

#### Preregistered Arena-surface priority slice

Hypothesis: replacing the horizontal priority-option wall with
engine-addressed board surfaces will make card play unambiguous without
reimplementing rules in React. The first acceptance slice is deliberately
bounded to Giant Growth targeting permanent `110` and a non-targeted Forest
play in the deterministic stack fixture.

- A selected or dragged hand card may match only current priority options with
  the same structured card ID and no `sourcePermanent`.
- `target.creature`, player-only `target.player`, and `spellTarget` route to
  exact permanent, player-HUD, and stack-object IDs respectively. An option
  with none of those routes to one explicit play/cast zone.
- A surface submits only when one exact option remains. Multiple options after
  card and destination selection open a compact engine-label/parameter chooser
  instead of being guessed.
- Engine `sourcePermanent` identifies an actionable origin. Selecting that
  permanent then routes any structured `target` or `spellTarget` to its exact
  destination; only a targetless unique ability may submit or activate through
  the source/generic activation zone. Pass priority remains a compact dedicated
  engine option.
- Click opens the large card inspection and selects the card; closing the
  inspection preserves selection and reveals legal board surfaces. Drag uses
  the same selection model. Unrelated surfaces never accept the choice.
- The bridge's permanent IDs, player seats, and stack IDs are sufficient for
  this slice. Hand cards currently expose definition IDs rather than stable
  instance IDs, and priority options expose no origin-zone/hand-instance ID.
  Duplicate copies are therefore intentionally equivalent until the engine
  gains per-copy state; stable instance/origin IDs are a prerequisite for that
  future rules work.

Before accepting the slice, the fixture contract must prove both exact actions
and the structural gate must prove generic ID routing with no card-name/type
policy. Rendered checks remain required for click selection, permanent/player/
stack highlighting, the play zone, and a real pointer drag.

### P2 — Arena-quality board readability

Status: **stack/targets rendered at both target viewports; broader visual work
in progress**

Acceptance criteria:

- At 1200–1800 px wide, hand, both battlefields, life totals, phase, stack, and
  the current prompt fit into a stable visual hierarchy.
- Tapped, summoning-sick, selected, attacking, blocking, damaged, and targeted
  permanents have distinct states that do not rely on color alone.
- The stack is prominent only while non-empty, with controller and target
  readable.
- The event log explains every state transition exercised by the journey gate.
- Board updates do not jump the hand away from the pointer or keyboard focus.

### P3 — Model inspection and reproducibility

Status: **planned**

Acceptance criteria:

- The selected deck, policy, game seed, training seed, and rollout settings are
  visible from the match.
- “Opponent thinking” is shown for long decisions and never leaves the game
  permanently disabled after an error.
- Debug reveal is visually unmistakable and cannot silently carry into a normal
  match.
- A bug report can copy a compact reproduction containing matchup, seeds,
  settings, turn, phase, and latest event.

### P4 — Accessibility and resilient layout

Status: **planned**

Acceptance criteria:

- Every game choice is reachable and operable by keyboard with visible focus.
- Prompts and game results are announced to assistive technology.
- Text and interactive-state contrast meet WCAG AA.
- The game remains usable at 1024 px wide and at 200% zoom.
- Reduced-motion preferences are respected.

### P5 — Rendered interaction harness

Status: **deterministic manual target/stack fixture available; automation planned**

The current repository harness deliberately adds no browser dependency, but
the hand regression shows that contract tests alone are insufficient. The next
harness increment should drive the deterministic journey through the rendered
client at 1440 × 900 and 1280 × 720 using stable semantic selectors. It should
assert visibility and interaction, not pixel snapshots. Tool choice remains
open until the client selectors and game flow settle.

The first manual fixture is launchable without waiting for a naturally
occurring target stack:

```sh
PORT=4174 make web-target-stack
```

Open <http://127.0.0.1:4174>, select Green versus Red with seed `42`, and
start the match. At both 1440 × 900 and 1280 × 720 verify:

1. The human Grizzly Bears and opponent Ironclaw Orcs each show a readable
   `TARGET` badge plus a non-color-only dashed outline.
2. The two-object stack names Lightning Bolt as next and identifies Grizzly
   Bears as its target.
3. `Cast Giant Growth → Grizzly Bears #110` is a visible legal response.
4. Activating that response adds the third stack object exactly once, keeps
   both target badges visible, changes the hand from seven cards to six, and
   taps the remaining Forest.
5. Passing priority resolves the response and returns a fresh legal priority
   decision.

## Deterministic journey gate

Run:

```sh
make test-web-ui
```

This command type-checks and builds the client, checks the focused hand and
target/stack UI regressions, then drives the production session manager through
a deterministic, production-shaped bridge fixture. It does not bind a port,
parse terminal artwork, or depend on animation timing.

The gate runs the full journey for every combination of:

- Deck: Green, Red, Blue, White, RU Aggro
- Opponent: Random, Monte Carlo, Deep Monte Carlo, Handcoded Policy, Learned
  Value, Learned Actor

The 30-case matrix proves that every advertised deck/policy selection survives
normalization and session setup. It does **not** claim that the fixture ran the
real deck or policy; real engine coverage is a separate gate below. Each matrix
case must prove:

1. Setup preserves the selected deck, policy, game seed, and training seed.
2. The first snapshot contains a seven-card, face-up human hand made of
   renderable card objects; the opponent exposes only hand size.
3. A legal Forest play moves one card from hand to the land zone.
4. Two Forests legally pay for Grizzly Bears and put a production-shaped spell
   object on a non-empty stack.
5. Passing priority emits `priority_action`, resolution emits
   `stack_resolved`, and the creature moves to the battlefield.
6. A ground creature can be blocked by ground creatures; damage-order and a
   later blocker decision accept only their legal permanent IDs.
7. The game reaches a structured result with no pending decision.
8. `Replay seed` creates a new session ID while retaining the exact matchup,
   settings, and seed.

The full integration command remains:

```sh
make test-web
```

It additionally exercises the C++ bridge tests, port-free HTTP routing/session
tests, and a representative production session-manager journey that starts the
real `build/old-school-web-bridge`, observes its hidden-safe seven-card hand,
and plays a legal land. This is the fast real-engine smoke, not the rendered
browser gate.

## Optional browser smoke

After a behavior or layout change:

```sh
make web
```

Open <http://127.0.0.1:4173> and use game seed `42`, training seed `424242`,
and 800 training games. Verify these six rotations so every deck and policy is
seen without manually testing all 30 pairs:

| Human deck | Opponent deck | Opponent policy |
| --- | --- | --- |
| RU Aggro | RU Aggro | Learned Value |
| Blue | White | Handcoded Policy |
| White | Red | Deep Monte Carlo |
| Green | Blue | Monte Carlo |
| Red | Green | Random |
| Blue | RU Aggro | Learned Actor |

For the first match, complete the full deterministic-journey checklist. For the
remaining rotations, confirm setup, visible hand, one legal action, policy
label, and clean rematch. Record any failure here before changing code.

## Iteration loop

For each web issue:

1. Reproduce it with a fixed matchup and seeds.
2. Add or tighten a structured contract assertion when the failure is visible
   in engine/session data.
3. Make the smallest UI or bridge fix.
4. Run `make test-web-ui`.
5. Run `make test-web` when bridge or server behavior changed.
6. Perform and record the browser smoke for rendering or interaction changes;
   do not close a visual milestone from source assertions alone.
7. Update the milestone status and append the command plus result below.

## Evidence log

- 2026-07-25 — Added the port-free 30-match journey gate and explicit visible
  human-hand/hidden-opponent-hand contract. Validation command:
  `make test-web-ui`; production build and all 32 contract tests passed.
- 2026-07-25 — Ran `make test-web`; 4 real C++ bridge tests and all 39 web
  build, layout, journey, and HTTP/session tests passed.
- 2026-07-25 — Closed P0 after reproducing the hidden-hand overlap in a real
  browser, reserving separate hand and decision-tray rows, and rerunning the
  match at 1280 × 720 and 1440 × 900. All seven opening cards were readable,
  the two rows had non-overlapping bounds, clicking Island focused its exact
  legal action, and playing it moved Island to the battlefield while the hand
  changed from seven cards to six.
- 2026-07-25 — Temporarily reopened P0 during independent harness review so
  source assertions could not substitute for a post-hardening rendered check.
- 2026-07-25 — Replaced the nominal journey's illegal Mountain-paid Flying Men
  and ground-blocking-a-flier sequence with a mana-legal Forest/Grizzly Bears
  transcript, real bridge event names, production stack fields, coherent
  attacker/damage-order/blocker ownership, and exact-seed replay assertions.
  `make test-web-ui` passed all 38 build, hand-layout, target/stack, and
  30-case adapter checks.
- 2026-07-25 — Added a port-free production session test that starts the real
  C++ bridge at seed 42, verifies a visible seven-card human hand and hidden
  opponent hand, then plays a legal land and observes `priority_action`.
  Converted HTTP routing tests to a no-listener request harness so they run in
  restricted environments. `make test-web` passed 4 C++ bridge tests and all
  44 Node build/session/UI tests.
- 2026-07-25 — Closed P0 again after reloading the final hardened build in a
  real browser at 1280 × 720. The seven-card hand remained fully visible above
  the decision tray, legal lands remained marked `PLAY`, the opponent hand
  remained hidden, and setup correctly advertised all five decks and six bot
  policies.
- 2026-07-25 — Removed the contradictory `No attacks` / `Attack with none`
  choice. Empty legal-attacker decisions now submit the sole legal declaration
  once and render no prompt; when attackers are available, `No attacks`
  remains explicit and the primary control stays disabled as `Select
  attackers` until the player selects at least one creature. The fixed
  decision-ID guard covers React Strict Mode replays; if the automatic request
  fails, that decision becomes visible with one recoverable `Continue — no
  attackers` action rather than retrying in a loop or stranding the match.
  `npm --prefix web run build` passed and `make test-web-ui` passed all 39
  build, combat-control, layout, target/stack, and 30-case journey checks. The
  rendered P1 check remains open.
- 2026-07-25 — Real-browser combat smoke at seed 42 confirmed the zero-attacker
  path emits `You declared no attackers` without displaying either redundant
  control, advances through the opponent's empty combat, and returns control
  on turn three. P1 remains open for the full creature-selection, blocking,
  damage-order, stack, game-over, and rematch journey.
- 2026-07-25 — Added a fixed seed-42 RU Aggro-versus-Blue targeted-stack
  regression and made stack priority legible without inventing client-side
  rules. While the stack is non-empty, the decision dock now says `Respond to
  the stack`, names the next object and its public target, and explains that
  passing continues toward resolution. Publicly targeted permanents on either
  battlefield receive both a dashed outline and a `TARGET` label, and the
  stack rail repeats the target beside the resolving object. `make
  test-web-ui` passed the production build and all 39 focused/matrix tests.
  No controllable browser was available in this environment, so this is
  structural evidence only; the 1440 × 900 and 1280 × 720 rendered stack
  interaction remains open and P1 is not closed.
- 2026-07-25 — Added `PORT=4174 make web-target-stack`, a deterministic
  production-shaped Green-versus-Red browser fixture with two public
  permanent targets, a two-object stack, and a legal Giant Growth response.
  Its contract confirms hidden opponent cards, target-to-permanent ownership
  on both sides, the response growing the stack from two objects to three,
  hand and mana updates, and the subsequent stack resolution. This removes
  the need to wait for a rare live-game stack before checking the rendered
  target treatment.
- 2026-07-25 — The rendered journey exposed a client-only opaque-ID bug that
  the direct contract harness had missed: attacker, blocker, and damage-order
  selections used normalized string keys as outbound IDs, so a numeric bridge
  decision such as `[101, 102]` received `["101"]` and remained stuck while
  advancing. Selection lookup now stays string-normalized only inside React,
  then restores the exact engine-provided scalar before every request.
  Browser-shaped regressions cover numeric and string attacker IDs, blocker
  pairs, and untouched numeric damage order.
- 2026-07-25 — Replaced pervasive microtext with an absolute 11px rendered
  floor, 12px primary labels and deck rows, 12px base card names, and 13px
  hand-card names. Enlarged battlefield card width and the stack rail/card
  treatment, removed all down-scaling (including tapped permanents), and kept
  the short-viewport hand/decision geometry fixed. The only zero-size
  declaration is documented narrow-screen text hiding beside a 17px icon.
  `make test-web-ui` passed the production build and all 43 layout, type-floor,
  target-stack, opaque-ID, and 30-case journey checks. Rendered typography
  verification at 1440 × 900 and 1280 × 720 remains required.
- 2026-07-25 — Added non-committing click inspection and exact-action drag/drop
  for playable hand cards. Both affordances are derived solely from the
  current priority decision's structured card IDs: an inspection link only
  focuses its exact tray action, while a drop submits that action's untouched
  engine option index. Multiple target-specific choices for one card remain
  separate visible drop targets. The current bridge identifies card
  definitions rather than unique hand copies, which is sufficient while
  duplicate copies have no private per-instance state; stable hand-instance
  IDs are required before adding such state. The decision dock now constrains
  its grid child to the reserved row and uses a 150px decision region so
  content scrolls instead of escaping below the viewport. `make test-web-ui`
  passed the production build and all 44 focused/matrix tests; `make test-web`
  passed 4 C++ bridge tests and all 50 Node build/session/UI tests.
- 2026-07-25 — Reloaded the targeted-stack fixture in a real browser. At
  1280 × 720 the decision heading had equal 150px client/scroll heights, its
  paragraph ended at viewport pixel 720, and the legal action occupied pixels
  586–706. At 1440 × 900 the same measurements were 150/150, pixel 900, and
  pixels 766–886. Clicking playable Giant Growth displayed the 220 × 306
  inspector; Escape closed it. Clicking the inspector's legal-action link
  closed and focused `priority-option-target-stack-priority-1-1` without
  changing the seven-card hand or two-object stack. Only the two playable
  Growth cards had `draggable=true`. Activating the exact option separately
  grew the stack from two objects to three, reduced the hand from seven cards
  to six, and tapped the remaining Forest; passing then reduced the stack to
  two. Native pointer drag was unavailable in the browser wrapper, so the
  exact drop path remains contract-tested and explicitly awaits a user/manual
  drag smoke.
