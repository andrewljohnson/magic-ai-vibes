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

Status: **complete — native targeted drag and the full rendered journey are
green at both target viewports; adapter journey, real-engine smoke, and setup
horizontal fit are also green**

Acceptance criteria:

- Setup exposes all five decks and all nine opponent policies, including
  explicit Learned Value C16, its best-response-attacks challenger, the
  stack-discipline diagnostic, and G0 identities.
- The current player, phase, priority holder, and required choice are obvious.
- Land play, spell cast, priority pass, stack resolution, attackers, blockers,
  damage order, game over, and rematch can each be completed without guessing.
- Public permanents expose a stable instance cue, and damage-order rows repeat
  that cue so duplicate card names remain distinguishable after reordering.
- Clicking a playable hand card selects and enlarges it in place while
  highlighting only its legal board destinations; Escape clears selection.
- Double-clicking submits only a unique non-targeted play. A playable card can
  also be dragged to its exact permanent, player, stack, or own-battlefield
  destination, while illegal cards and unrelated surfaces never accept it.
- During blocking, attacking creatures occupy a dedicated combat lane;
  blockers are selected or dragged on the battlefield, and every persistent
  pairing can be reviewed or removed before confirmation.
- Cleanup discards select exact hand positions, so duplicate copies are
  independently selectable and the confirm control requires exactly the
  engine-requested count.
- `Bluff mode` is off by default. When enabled before a match, pass-only
  priority and empty-attacker decisions remain visible instead of auto-running.
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
  with none of those routes to the near player's battlefield surface.
- A surface submits only when one exact option remains. Multiple options after
  card and destination selection open a compact engine-label/parameter chooser
  instead of being guessed.
- Engine `sourcePermanent` identifies an actionable origin. Selecting that
  permanent then routes any structured `target` or `spellTarget` to its exact
  destination; a targetless unique ability may submit from the source. Pass
  priority remains a compact dedicated engine option.
- A single click selects and raises the card inside the hand; double-click
  immediately submits only a unique targetless route. Drag uses the same
  selection model. There is no inspection modal, selected-card block,
  play/cast dock, or wall of legal-action cards.
- The bridge's permanent IDs, player seats, and stack IDs are sufficient for
  this slice. Hand cards currently expose definition IDs rather than stable
  instance IDs, and priority options expose no origin-zone/hand-instance ID.
  Duplicate copies are therefore intentionally equivalent until the engine
  gains per-copy state; stable instance/origin IDs are a prerequisite for that
  future rules work.

Before accepting the slice, the fixture contract must prove both exact actions
and the structural gate must prove generic ID routing with no card-name/type
policy. Rendered checks remain required for click selection, permanent/player/
stack highlighting, the own-battlefield destination, and a real pointer drag.

#### Preregistered keyboard/non-native interaction slice

Hypothesis: every current human decision can remain engine-addressed and
become keyboard-completable without restoring an action wall or depending on
native drag support.

- Enter on a legal hand card selects that exact rendered copy and exposes
  `aria-pressed`. Enter again may submit only when the engine provides one
  targetless battlefield route; multiple exact variants open the existing
  parameter chooser, and targeted actions remain selected until an exact
  public destination is activated.
- The highlighted own battlefield remains the non-native mouse fallback for a
  selected play route and forwards through the same exact-option resolver as
  drop. Keyboard activation reuses that resolver from the selected card and
  never guesses among targets or parameter variants.
- Attacker, blocker, and block-target cards remain native buttons with visible
  focus. Damage-order movement names the public permanent ID, so duplicate
  cards have distinct accessible controls.
- All keyboard paths preserve the engine's untouched decision ID, option
  index, and opaque permanent IDs. The client adds no card-name/type policy.
- Acceptance requires source/contract assertions plus a 1280 × 720
  keyboard-only rendered journey through land, creature, attacker,
  damage-order, blocker, and confirmation controls. This slice makes no claim
about native pointer drag.

#### Preregistered journey-fixture liveness regression

Hypothesis: the reported disabled-control stall is caused by a scripted
fixture branch that advertises a legal action but neither emits a settlement
nor terminates, rather than by React request-state cleanup or the production
game bridge.

- Reproduce on a fresh seed-42 journey session by submitting the advertised
  first-main `Pass priority` option and inspecting both the pending HTTP
  request and the session's public status.
- The advertised pass must settle promptly into an authoritative next fixture
  snapshot. Any genuinely unsupported scripted action must terminate the
  fixture child promptly so the server can surface a failed session instead of
  waiting the 120-second production action timeout.
- A deterministic contract must cover the pass branch and the failure
  liveness invariant. Acceptance requires `make test-web-ui`, `make test-web`
  because the fixture/session contract changes, and a rendered 1280 × 720
  replay of the original pass interaction. Port `4173` remains untouched.

#### Preregistered setup-drawer horizontal-fit slice

Hypothesis: the 1280px setup overflow comes from a drawer-local grid retaining
its wide viewport layout inside the fixed 920px panel; sizing that content to
its actual drawer width will remove horizontal scrolling without hiding or
shrinking any match control.

- First identify the exact overflowing descendant and record its
  client/scroll width and computed grid before changing CSS.
- At 1280 × 720 the 920px setup drawer and every content section have equal
  client/scroll widths. Both seat setups, every simulation field, both mode
  toggles, and `Start match` remain visible through ordinary vertical
  scrolling; no content is clipped and the 11px text floor is unchanged.
- Closing setup restores the identical board rectangle and exact opener focus.
  The page itself remains free of horizontal overflow.
- Acceptance requires a focused layout regression, `make test-web-ui`, and a
  rendered check on the deterministic `4176` journey. No server or action
  contract changes are in scope.

#### Preregistered forced-empty-blockers slice

Hypothesis: when the authoritative engine emits a `blockers` decision with no
legal blocker choices, submitting its exact empty declaration immediately will
remove a meaningless `No blocks` click without hiding any genuine player
choice.

- The trigger is only `decision.kind === "blockers"` with an empty
  engine-provided `choices` array. The browser does not inspect card names,
  card types, battlefield contents, or reconstruct blocking legality.
- The automatic request preserves the engine's opaque `decisionId` byte for
  byte and sends exactly `pairs: []`, at most once for that decision.
- Bluff mode does not pause this declaration because an empty blocker choice
  set has no alternative action. A failed automatic request exposes the
  original blocker decision and recoverable error instead of retrying in a
  loop.
- Reproduce and verify with a fixed interaction fixture at seed `42`, no human
  creatures, one opposing attacker, and an opaque string decision ID.
  Acceptance requires a focused source/contract regression, `make test-web-ui`,
  `make test-web` because the fixture changes, and a real-browser 1280 × 720
  smoke proving there is one exact action request and no visible `No blocks`
  control.

### P2 — Arena-quality board readability

Status: **stack controller/targets and the concise Chronicle rendered at both
target viewports; setup legibility and explicit card-state/keyword cues
rendered at 1280 × 720; broader visual work in progress**

#### Preregistered Learned-policy provenance slice

Hypothesis: showing an explicit date, lifecycle status, and concise training
lineage beside each Learned pilot will let players distinguish the frozen C16
control from per-match G0 and Actor recipes without implying that an
unpromoted research challenger is deployed.

Status: **contract and production build complete; rendered acceptance pending
because this session could neither bind an ephemeral localhost test port nor
open the user-blocked live port 4173**

- C16 must identify its exact 2026-07-26 artifact freeze, K8/H4 deployment,
  Value/self-play lineage, and current role as the frozen research control.
- G0 and Actor must identify their 2026-07-24 recipe dates and say explicitly
  that the selected training games and seed produce a model for the match.
- Actor must be described as the separate direct policy-head experiment;
  neither it nor G0 may be presented as a newer C16 generation.
- Dates and lifecycle labels must come from server metadata, survive the
  fallback metadata path, and render in the selected pilot summary without
  changing the engine-authoritative model identity in the REPRO panel.
- Acceptance requires focused server/client contract assertions,
  `make test-web-ui`, `make test-web`, and a rendered setup-drawer smoke at
  1280 × 720 proving the provenance text is readable without horizontal
  overflow.

#### Preregistered defender-best-response challenger exposure slice

Hypothesis: exposing the already frozen defender-best-response attack
challenger as a separate pilot will let human play-testing distinguish its
combat choices from canonical C16 without changing either model identity or
silently promoting a 240-game screen.

Status: **implementation, contract gates, live-server restart, and rendered
smoke complete; human strategic play-test in progress**

- The separate stable ID is `learned-value-c16-adversarial-blocks`, displayed
  as `Learned C16 · Best-Response Attacks`.
- It loads the exact canonical C16 artifact and retains K8/H4 Value search.
  Its sole policy difference is attack-set aggregation: sampled legal blocks
  use the defender's minimum-scoring best response rather than their mean.
- Metadata dates the challenger `2026-07-28` and says explicitly that it is an
  exploratory challenger: its 127–113 fast screen awaits human play-testing
  and is not a promotion.
- The Node boundary treats its generation, training-game, and training-seed
  identity exactly like C16. The C++ boundary sets
  `value_adversarial_blocks=true` only for this policy ID; all other web
  policies retain the default `false`.
- Acceptance requires focused C++ parser/config tests, Node normalization and
  bridge-argument tests, client fallback/setup assertions, the complete
  five-deck × eight-policy journey matrix, `make test-web-ui`, and
  `make test-web`. This metadata/setup change makes no broader layout claim.
- The rebuilt live server was restarted at `http://127.0.0.1:4173`. A real
  in-app-browser smoke at 1280 × 720 showed all eight policy options, selected
  the challenger, displayed its July 28 provenance and non-promotion status,
  kept T800/S424242 read-only, and started an RU Aggro mirror at turn 1 with
  `Learned C16 · Best-Response Attacks` visibly identified as the opponent.

#### Preregistered stack-discipline diagnostic exposure slice

Hypothesis: exposing the already screened Pass-dominance composition as a
separate behavior diagnostic will let the owner test its marginal-effect
filter in real play without replacing the stronger attack-only pilot or
overstating a tied 60-game screen.

Status: **implementation, contract gates, live-server restart, and rendered
smoke complete; human behavior play-test in progress**

- The separate stable ID is `learned-value-c16-stack-discipline`, displayed as
  `Learned C16 · Stack Discipline`.
- It loads the exact canonical C16 artifact at K8/H4 and enables both existing
  rules-only treatments: defender-best-response attack aggregation and
  Pass-dominance filtering. No rules or policy logic changes in this slice.
- Its description says narrowly that the marginal-effect filter rejects an
  action when Pass reaches the same public outcome with strictly fewer
  resources. It does not promise that every redundant-looking counter is
  removed.
- Metadata dates the pilot `2026-07-28` and says explicitly:
  behavior diagnostic, 30–30 fast screen, performance gate not passed,
  awaiting human play-test, and not promoted. The attack-only pilot remains a
  separate selection.
- The Node boundary treats it exactly like frozen C16 for generation,
  training-game, and training-seed identity. The C++ boundary sets both
  `value_adversarial_blocks=true` and `value_pass_dominance=true` only for
  this ID; every other web policy resets the Pass-dominance bit to `false`.
  Both boundaries also reject a non-K8 search, and the bridge rejects
  Pass-dominance without defender-best-response attacks before emitting
  session output.
- Acceptance requires focused C++ parser/config tests, Node normalization and
  bridge-argument tests, client fallback/setup assertions, the complete
  five-deck × nine-policy journey matrix, `make test-web-ui`, and
  `make test-web`. This slice makes no broader rendered-layout claim.
- Evidence on 2026-07-28: the focused bridge suite passed 19/19; focused
  metadata, setup, and 45-case journey checks passed 81/81; `make test-web-ui`
  passed 97/97 (including all 45 deck/policy journeys); and `make test-web`
  passed the 19/19 C++ bridge suite plus 118/118 Node tests. The rebuilt live
  server was then restarted. A real in-app-browser smoke at 1280 × 720 showed
  all nine policies, selected Stack Discipline, displayed its exact
  marginal-effect description plus the 30–30/failed-gate/non-promotion
  provenance, retained the frozen T800/S424242 controls, and started an RU
  Aggro mirror at turn 1 with `Learned C16 · Stack Discipline` visibly
  identified as the opponent. Human behavior validation remains explicitly
  open.

Acceptance criteria:

- At 1200–1800 px wide, hand, both battlefields, life totals, phase, stack, and
  the current prompt fit into a stable visual hierarchy.
- Tapped, summoning-sick, selected, attacking, blocking, damaged, and targeted
  permanents have distinct states that do not rely on color alone.
- The stack is prominent only while non-empty, with controller and target
  readable.
- The event log explains every state transition exercised by the journey gate.
- Board updates do not jump the hand away from the pointer or keyboard focus.

#### Preregistered explicit stack-controller slice

Hypothesis: rendering each stack object's public controller as an explicit
`YOU` or `OPPONENT` chip will make stack ownership readable without relying on
bridge-authored prose or teaching the client card-specific rules.

- The cue derives only from `entry.controller` and the configured human seat.
  Missing or invalid controller data produces no invented label.
- Each cue has a visible text label plus an accessible
  `Controlled by You/Opponent` name. Targets, resolving order, cards, and action
  routing remain unchanged.
- At both 1280 × 720 and 1440 × 900, the fixed Green/human versus Red/Learned
  Value seed-42 target-stack fixture must show `OPPONENT`, then `YOU` from top
  to bottom initially; after the Giant Growth response it must show `YOU`,
  `OPPONENT`, `YOU`.
- Controller chips must remain inside their stack entries and must not overlap
  the `NEXT`/`+N` order cues. Acceptance requires pure/source regressions,
  `make test-web-ui`, and the existing dual-viewport
  `make test-web-rendered` journey.

#### Preregistered setup-text legibility slice

Hypothesis: raising only the New Match drawer's explanatory text and field
labels from the 11px interface floor to the existing 12px label token will
make configuration readable at 1280 × 720 without changing the proven
five-column layout or creating horizontal overflow.

- Header copy, Deck/Pilot labels, policy descriptions, simulation field
  labels, toggle names/descriptions, and footer guidance use at least
  `--type-label` (12px). Compact eyebrows and non-content metadata may retain
  the 11px floor.
- No grid columns, control sizes, drawer width, or card/board typography change
  in this slice.
- Acceptance requires a focused typography/layout regression,
  `make test-web-ui`, and a rendered `4176` check at 1280 × 720 confirming
  readable hierarchy, drawer client/scroll width equality, all controls
  reachable by vertical scroll, and unchanged board geometry after dismissal.

#### Preregistered explicit damage-cue slice

Hypothesis: labeling marked damage as `DMG N` instead of the bare symbol `−N`
will distinguish damage from a power/toughness modifier while retaining the
compact card-state hierarchy.

- The visible token uses the engine-provided positive marked-damage value and
  the accessible card name continues to say `N damage marked`.
- Sickness, stack target, current legal destination, combat stats, and all
  card geometry remain unchanged.
- Acceptance requires a focused source regression, `make test-web-ui`, and a
  refreshed 1280 × 720 `4174` inspection showing distinct
  `SICK`/`DMG 1`/`TARGET`/`CHOOSE` cues without overlap or overflow.

#### Preregistered explicit flying-cue slice

Hypothesis: replacing the unexplained star-only treatment on flying cards with
the literal public keyword `FLYING` will make evasion legible at a glance
without changing card geometry or teaching the browser any card-specific
rules.

- The cue is derived only from the engine-provided `card.flying` flag. Flying
  cards show `FLYING` inside the existing illustration field; non-flying cards
  retain the existing neutral or land glyph and gain no keyword.
- The shared public tooltip/accessible name continues to include `Flying`.
  Card names, costs, combat stats, permanent IDs, status badges, and action
  routing remain unchanged.
- Acceptance requires a focused source regression, `make test-web-ui`, and a
  real-engine 1280 × 720 Blue-versus-Red seed-42 render showing the opening
  Air Elemental keyword without hand overlap, clipping, or page overflow.

#### Preregistered concise Chronicle slice

Hypothesis: grouping the Chronicle's public events by turn and hiding routine
priority passes by default will make meaningful game actions easier to scan
without discarding any public history.

- The bridge exposes the selected priority action's rules-level kind as public
  scalar metadata. The client identifies passes only from that structured kind,
  never by parsing event prose or inspecting cards.
- An unchecked checkbox labeled `Show priority passes` is visible in the
  Chronicle heading. Turning it on reveals every pass in original order;
  turning it off hides them again without changing the authoritative log.
- Among the currently visible events, `Turn N` appears only on the first event
  after the turn number changes. If earlier events in that turn are filtered,
  the first remaining visible event owns the turn marker.
- The event count describes the visible list, and event numbering remains
  consecutive within that visible list so hidden passes leave no unexplained
  ordinal gaps.
- Acceptance requires focused bridge/helper/source regressions,
  `make test-web-ui`, `make test-web`, and rendered checks at 1280 × 720 and
  1440 × 900 proving the default and toggled views are readable and preserve
  page geometry.

### P3 — Model inspection and reproducibility

Status: **explicit Learned-model identity, acceptance criteria, public-log
privacy boundary, and ephemeral deck evolution complete; broader model
inspection remains a future enhancement**

Acceptance criteria:

- The selected deck, policy, game seed, training seed, and rollout settings are
  visible from the match.
- “Opponent thinking” is shown for long decisions and never leaves the game
  permanently disabled after an error.
- Debug reveal is visually unmistakable and cannot silently carry into a normal
  match.
- A bug report can copy a compact reproduction containing matchup, seeds,
  settings, turn, phase, and latest event.

#### Preregistered ephemeral deck-evolution menu slice

Hypothesis: exposing the existing engine evolution loop in a separate bounded
web workflow will let a player generate, inspect, and immediately play a deck
without turning match setup into a deck editor or introducing durable server
state.

- A top-level `Evolve deck` control opens a distinct dialog from `New match`.
  It exposes engine-owned generation count, population, paired repetitions,
  seed, and a whitelisted Handcoded Policy or frozen Learned Value C16 pilot.
- Generation runs in a separate bounded child process using
  `evolve_deck`; the browser never implements fitness, mutation, card-pool, or
  legality rules. The server validates strict resource caps, allows only one
  active evolution per process, bounds output and runtime, and never invokes a
  shell.
- The completed result shows its exact 40-card manifest, aggregate fitness,
  per-metagame results, generation trace, seed, and pilot. `Save for this
  session` stores only that server-produced result in Node process memory.
- A saved deck receives an opaque ID, appears in both seat selectors and deck
  manifests, and is passed to the C++ bridge as the exact validated card-ID
  vector. It survives new matches but disappears when the Node server restarts;
  no filesystem, database, cookie, or browser-storage persistence is added.
- The normal metadata and game APIs remain engine-authoritative and never
  accept a client-authored arbitrary deck list. Opponent hidden-card identities
  remain private during play.
- Acceptance requires focused C++ JSON/custom-deck tests, Node validation,
  lifecycle, save, and play-through contract tests, `make test-web-ui`,
  `make test-web`, and a real-Chromium 1280 × 720 and 1440 × 900 smoke proving
  the separate dialog, result manifest, session-only save notice, and saved
  deck selection without horizontal overflow.

#### Preregistered explicit Learned-model identity slice

Hypothesis: replacing the web's ambiguous, freshly trained `learned-value`
selection with explicit G0 and immutable C16 choices will make manual bot
feedback reproducible and will prevent a weak test model from being mistaken
for the frozen research baseline.

- Setup advertises `Learned Value C16` and `Learned Value G0` as distinct
  policies. The normal/default Learned opponent is C16 with K=8; G0 remains an
  explicit trainable option for fast fixtures and smoke tests.
- The normalized match config carries the selected generation and Learned
  rollout count to the bridge. C16 accepts only its canonical T800/S424242/C16
  identity, loads only
  `build/model-cache/old-school-value-challenger-v3-c16-t800-s424242.bin`
  through the canonical artifact loader, verifies fingerprint
  `68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`,
  and never trains, refreshes, or substitutes a model. A missing, stale, or
  mismatched artifact fails closed with a separate-generation command.
- G0 continues to train from the exact public training games/seed. Both model
  paths emit structured public identity containing family, generation, K,
  fixed H=4, and the actual model fingerprint; the server preserves it as
  match metadata.
- The live REPRO surface displays the actual generation and fingerprint and
  includes them in its selectable exact text. This metadata is model-public
  and contains no state, hand, or hidden card identity.
- Acceptance requires C++ parser/load-only/fingerprint regressions, Node
  normalization/bridge-contract regressions, `make test-web`, `make
  test-web-ui`, and rendered setup/REPRO checks at 1280 × 720 and
  1440 × 900. The rendered checks must identify C16 and its exact fingerprint;
  fixture-only G0 coverage cannot establish the C16 loading claim.

#### Preregistered compact reproduction slice

Hypothesis: one compact, selectable match summary will make live bot behavior
reproducible without exposing hidden game state or expanding the fixed board
layout.

- The live header exposes both configured deck/policy pairs, game seed,
  training games/seed, normal/deep/Learned rollout counts, Bluff mode, and
  debug-reveal state.
- Values come only from the server-normalized public match configuration;
  opponent hand contents and all other private state are excluded.
- A one-line machine-readable form uses exact deck/policy IDs and setting
  names. It is rendered in a read-only selectable field that selects on focus,
  avoiding a clipboard API claim.
- The collapsed control and open panel remain readable at 1280 × 720 without
  moving the battlefield or hand. Acceptance requires a pure formatter test,
  structural source/layout assertions, `make test-web-ui`, and a rendered
  1280 × 720 check.

#### Preregistered in-flight opponent indicator slice

Hypothesis: replacing the decision-covering busy mask with a small status
indicator will make bot latency clear while keeping the last legal decision
visible and recoverable.

- `Opponent thinking…` appears only while the existing human-action request is
  in flight and the match is playing. It does not infer a new phase or claim
  engine state the response has not delivered.
- The indicator lives outside the decision surface with polite status
  semantics. Existing single-flight and disabled-button guards continue to
  prevent duplicate submissions without hiding the current controls.
- Success and every error path clear the same request state in `finally`.
  Ordinary failures retain the prior legal snapshot; stale-state failures keep
  the existing refresh path. The error toast remains dismissible after controls
  recover.
- Acceptance requires focused lifecycle/rendering assertions and
  `make test-web-ui`. A rendered claim requires a genuinely delayed request;
  an instantaneous fixture cannot substitute for that evidence.

#### Preregistered delayed-fixture acceptance slice

Hypothesis: a bounded, opt-in delay on the now-supported journey pass branch
will make the healthy in-flight lifecycle directly observable and distinguish
it from the reproduced dead-child stall without slowing ordinary tests or
changing production behavior.

- A separate stable launch command reuses the journey fixture with a fixed
  sub-second delay. The ordinary `web-journey` path remains instantaneous.
- The action contract proves the delayed response cannot settle before its
  declared floor and still reaches the same authoritative
  `declare_attackers` decision.
- At 1280 × 720, `Opponent thinking…` must appear during the request without
  covering or removing the last decision, then disappear after the response
  while controls recover. No fixture timing is inferred as engine phase.
- Acceptance requires `make test-web-ui`, `make test-web`, and a rendered
  delayed-action check. Port `4173` remains untouched.

#### Preregistered debug-reveal safety slice

Hypothesis: a persistent config-driven warning plus a fail-closed new-match
default will prevent debug hand visibility from being mistaken for normal play.

- A playing match whose normalized config has `debugReveal=true` shows
  `DEBUG REVEAL ON` in the persistent header. No public or hidden zone
  inspection controls the warning.
- A normal match renders no warning. Opening `New match` always initializes
  reveal off, even from a debug match; the user must explicitly opt in again.
- `Replay seed` remains an exact replay of the current config and therefore
  retains reveal state together with its persistent warning. It is not a new
  setup.
- Acceptance requires focused source/config assertions, `make test-web-ui`,
  and a 1280 × 720 debug-on → new-setup-off → normal-match rendered check.

#### Preregistered live reproduction-state slice

Hypothesis: appending a deliberately narrow public-state context to the exact
REPRO text will make bug reports identify the decision being observed without
changing the match's stable setup identity or leaking hidden cards.

- The selectable text appends current turn number, exact engine phase,
  publicly knowable priority holder, and the latest normalized public event.
  The setup prefix remains byte-identical as snapshots advance.
- The formatter accepts only those scalar context fields; it is not passed a
  player, hand, zone, stack, or whole snapshot object. A current human priority
  decision reports `you`; non-priority decisions report `none`.
- Every new snapshot recomputes the text in the existing read-only, select-on-
  focus field. Missing state or events use explicit stable fallbacks.
- Acceptance requires pure before/after formatter assertions, source/privacy
  assertions, `make test-web-ui`, and a rendered 1280 × 720 check that the open
  field updates after a deterministic fixture action without overflowing or
  moving the board.

#### Preregistered public-event formatting boundary

Hypothesis: replacing the event log's object-serialization fallback with a
typed public-message formatter will preserve useful labeled events while
preventing incidental engine payload fields from becoming visible text.

- Only string entries and the explicit public `message`, `text`, or `label`
  fields may become event prose. Scalar turn/player/kind metadata may continue
  to control public labels and styling, but it is never serialized.
- An object without public prose renders the neutral `Game event`; extra
  hand/card/zone-shaped properties do not affect the result.
- Acceptance requires pure hidden-shaped formatter cases, a source assertion
  that MatchLog uses the helper with no object stringify fallback,
  `make test-web-ui`, and a rendered 1280 × 720 `4176` check that ordinary
  labeled journey events remain readable with unchanged layout.

### P4 — Accessibility and resilient layout

Status: **prompt, result/setup focus, bounded contrast, public card-state
naming, and recoverable error alerts rendered green; reduced-motion
structurally green with rendered emulation unavailable; native keyboard
activation calibration and remaining criteria planned**

Acceptance criteria:

- Every game choice is reachable and operable by keyboard with visible focus.
- Prompts and game results are announced to assistive technology.
- Text and interactive-state contrast meet WCAG AA.
- The game remains usable at 1024 px wide and at 200% zoom.
- Reduced-motion preferences are respected.

#### Preregistered reduced-motion slice

Hypothesis: honoring the operating-system reduced-motion preference at the
stylesheet boundary will remove perpetual spinner/pulse and hover transition
motion without hiding status text, selection outlines, or legal controls.

- Under `prefers-reduced-motion: reduce`, animations and transitions finish
  effectively immediately and smooth scrolling is disabled.
- The DOM and visibility of thinking, live, selected, targeted, and combat
  cues remain unchanged; only their motion changes.
- Normal-motion rendering remains untouched.
- Acceptance requires a focused stylesheet/source regression,
  `make test-web-ui`, and a rendered emulated-reduced-motion check of computed
  spinner/live-indicator animation plus visible status content at 1280 × 720.

#### Preregistered prompt-announcement slice

Hypothesis: a narrow atomic live-region summary for each engine decision plus
an explicitly named/described result dialog will make required choices and
match outcomes discoverable without repeatedly announcing every button and
card in the decision dock.

- The live region contains only current public turn/phase, decision heading,
  and helper text. It updates from the engine decision ID and never includes
  either hand or opponent-private state.
- The interactive dock itself is no longer a live region; its controls retain
  their existing labels, disabled state, and keyboard behavior.
- The game-over dialog references its visible result title and reason as its
  accessible name and description.
- Acceptance requires focused source/privacy assertions,
  `make test-web-ui`, and rendered DOM/accessible-attribute inspection at
  1280 × 720. This does not claim a full screen-reader product audit.

#### Preregistered result-dialog focus slice

Hypothesis: moving focus to the named result dialog when a match finishes will
make the outcome immediately discoverable while leaving both explicit next
actions in the normal tab order.

- The dialog container receives programmatic focus only when a snapshot enters
  `finished` or `error`; it references the existing visible result title and
  reason and does not auto-activate either action.
- `Change matchup` and `Replay seed` remain ordinary buttons reachable after
  the focused container. This slice adds no focus trap.
- Escape is consumed without dismissing the result because there is no valid
  completed-game surface underneath to return to; focus remains where the user
  placed it inside the dialog.
- Acceptance requires focused lifecycle/semantics assertions,
  `make test-web-ui`, and a rendered 1280 × 720 completion of the existing
  journey fixture checking initial focus, name/description, Tab access to both
  actions, and non-dismissal on Escape.

#### Preregistered setup-dialog focus slice

Hypothesis: focusing and containing keyboard navigation within the New Match
dialog, then restoring the exact opener on dismissal, will prevent setup
controls from becoming mixed with the still-rendered game underneath.

- Opening setup from a live match focuses its visible Close button without
  changing the selected matchup. The dialog's accessible name references the
  visible `Set the table` heading instead of a separate label.
- Tab and Shift+Tab cycle through enabled controls inside the drawer; the
  clickable backdrop is omitted from sequential keyboard navigation because
  the visible Close button provides that action.
- Escape dismisses setup and restores focus to the exact connected element
  that opened it. Starting a replacement match does not try to focus an
  unmounted opener.
- Acceptance requires focused lifecycle/semantics assertions,
  `make test-web-ui`, and a rendered 1280 × 720 check on the deterministic
  `4176` journey for initial focus, name, forward/backward wrap, Escape focus
  restoration, and unchanged board geometry/no horizontal overflow.

#### Preregistered subdued-text contrast slice

Hypothesis: raising the shared faint-text token to WCAG AA contrast and using
it for the two hard-coded phase/log exceptions will make small supporting
labels readable without flattening the gold active-state and primary-text
hierarchy.

- `--faint` reaches at least 4.5:1 against `--panel-2`, the lightest dark
  surface on which the token is used. Main phase names and event indices use
  that token instead of lower-contrast one-off colors.
- Active phase, selected/legal states, card identity, and primary body text
  retain their existing colors. This slice does not claim a whole-application
  WCAG audit.
- Acceptance requires a computed contrast regression, `make test-web-ui`, and
  a rendered 1280 × 720 `4176` inspection showing readable subdued phase,
  event-log, and setup labels while preserving hierarchy, board geometry, and
  horizontal fit.

#### Preregistered public card-state naming slice

Hypothesis: deriving both the tooltip and accessible name from one public card
description will make screen-reader card information match the visible card
without introducing a second source of state truth.

- A card name includes its public type, mana cost, flying keyword,
  power/toughness, and permanent instance ID when present. Public permanent
  state adds tapped, summoning sickness, and marked damage; stack targeting
  and current legal destination/origin cues are included when active.
- Selection remains exposed by `aria-pressed`; opponent hand identities and
  all other private state remain outside the formatter.
- Acceptance requires focused source assertions for every field,
  `make test-web-ui`, and rendered 1280 × 720 checks on existing deterministic
  fixtures for ordinary hand cards plus tapped, sick, targeted, and legal
  action card states, with unchanged visible layout.

#### Preregistered recoverable-error announcement slice

Hypothesis: giving action/setup failure toasts atomic alert semantics and an
explicit dismiss action will make a failed request discoverable and recoverable
without moving focus or replacing the last authoritative game snapshot.

- A visible failure toast has `role=alert`, is atomic, shows the server/client
  message, and provides a native `Dismiss` button. The non-error
  shuffling/training toast remains a polite status and has no dismiss action.
- Existing action lifecycle behavior is unchanged: ordinary failures retain
  the prior legal snapshot, stale-state failures refresh it, and every path
  clears acting state in `finally`. Showing an error never steals focus or
  auto-submits another action.
- Acceptance requires focused lifecycle/semantics assertions,
  `make test-web-ui`, and an isolated 1280 × 720 rendered network-failure check
  confirming the alert appears over the still-rendered decision, controls are
  re-enabled, Dismiss works by keyboard, and the page does not overflow.

### P5 — Rendered interaction harness

Status: **target/stack drag automated at both target viewports; real-engine
auto-pass/Bluff behavior automated at 1280 × 720; deterministic
cleanup/blocking and full-flow browser fixtures available; broader automation
planned**

#### Preregistered real-engine auto-pass regression

Hypothesis: the production browser can distinguish engine-forced priority from
an engine-authored empty attacker choice without recreating card or combat
legality in JavaScript.

- The fixed real-engine match is Green/human versus Red/Random at game seed
  `42`, one training game, training seed `424242`, and the default `2/8/2`
  rollout settings (unused by the Random opponent). Its opening Forest is the
  only non-Pass legal action.
- With Bluff mode off, playing that Forest returns the engine's empty
  `attackers` decision directly rather than exposing a sole-Pass first-main
  decision. The client submits that exact decision ID with `ids: []` once and
  reaches the next authoritative human decision.
- With Bluff mode on, the same land play retains the engine's sole-Pass
  first-main decision. Passing it retains the sole-Pass beginning-combat
  decision; passing again exposes the empty `attackers` decision, which stays
  visibly paused with no automatic fourth action request.
- The regression observes action requests and responses rather than waiting a
  fixed duration. It asserts only engine-emitted decision kinds, option counts,
  eligible attacker IDs, and exact opaque response payloads.
- Acceptance requires `make test-web-ui` and `make test-web-rendered`. No
  bridge, server, or action-contract change is in scope.

The repository-pinned Playwright gate is intentionally separate from the fast
port-free tests. `make test-web-rendered` drives the target-stack fixture
through Chromium at both target viewports and a real-engine auto-pass/Bluff
match at 1280 × 720, using stable semantic selectors and authoritative action
responses. The next harness increment should drive the deterministic full
journey at 1440 × 900 and 1280 × 720 in the same way, asserting visibility and
interaction rather than pixel snapshots.

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
3. Selecting Giant Growth marks Grizzly Bears `CHOOSE` as its exact legal
   destination without displaying a parallel legal-action card.
4. Dragging Giant Growth onto Grizzly Bears adds the third stack object exactly
   once, keeps
   both target badges visible, changes the hand from seven cards to six, and
   taps the remaining Forest.
5. Passing priority resolves the response and returns a fresh legal priority
   decision.

The interaction fixture makes cleanup and blocking equally reproducible:

```sh
PORT=4175 make web-interaction
```

Open <http://127.0.0.1:4175>, select White versus Red with seed `42`, and
start the match. At both target viewports verify:

1. `Bluff mode` begins unchecked in setup.
2. Nine visible hand cards include two separately selectable Plains copies;
   selecting exactly those two shows `DISCARD` on each and enables the single
   `Confirm discard` control.
3. Confirmation emits a public `cards_discarded` event and advances to a
   blocker decision.
4. The opponent's two attackers move out of its permanent row into a sideways
   `ATTACKING` combat lane nearest the midline.
5. Both legal blockers glow. Blocker `110` accepts only attacker `210`;
   blocker `111` accepts `210` or `211`. Assigning both to `210` displays two
   removable pairing chips and `BLOCKED ×2`.
6. `No blocks` submits the empty declaration; with one or more pairings,
   `Confirm block(s)` submits untouched numeric engine IDs.

The complete flow fixture exposes the existing port-free journey through the
real browser surface:

```sh
PORT=4176 make web-journey
```

Open <http://127.0.0.1:4176>, start any matchup with Bluff mode off, and verify:

1. Play Forest and cast Grizzly Bears. The Chronicle records the cast, forced
   pass, and stack resolution without exposing a pass-only decision.
2. On turn four, attack with exactly one Grizzly Bears. The opponent declares
   two blockers and the browser asks for their damage order.
3. Reverse the two blockers and confirm. The Chronicle records both the order
   and combat resolution.
4. On the opponent's following attack, assign the remaining Grizzly Bears as
   a blocker and confirm.
5. The match-complete dialog reports the empty-library win. `Replay seed`
   creates a fresh session at the opening land decision with the same setup.

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
  Value C16, Learned C16 · Best-Response Attacks, Learned C16 · Stack
  Discipline, Learned Value G0, Learned Actor

The 45-case matrix proves that every advertised deck/policy selection survives
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
and 800 training games. Verify these nine rotations so every deck and policy
is seen without manually testing all 45 pairs:

| Human deck | Opponent deck | Opponent policy |
| --- | --- | --- |
| RU Aggro | RU Aggro | Learned Value C16 |
| Blue | White | Handcoded Policy |
| White | Red | Deep Monte Carlo |
| Green | Blue | Monte Carlo |
| Red | Green | Random |
| Blue | RU Aggro | Learned Actor |
| RU Aggro | Blue | Learned C16 · Best-Response Attacks |
| White | Green | Learned Value G0 |
| Blue | Blue | Learned C16 · Stack Discipline |

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
- 2026-07-25 — Replaced the legal-action wall, inspection modal, selected-card
  block, and play/cast dock with engine-addressed Arena surfaces. Legal hand
  cards and `sourcePermanent` origins now glow without `PLAY` badges; a click
  selects in place, a unique targetless double-click submits once, and exact
  permanent/player/stack/battlefield drops share structured destination
  routing. Drag legality is written to a synchronous origin reference before
  React renders highlights, and every destination keeps a stable drop handler.
  Priority UI collapses to a 78px Pass/stack/ambiguity strip. The near
  battlefield accepts targetless play clicks on any non-button point.
- 2026-07-25 — Added first-class browser cleanup, Bluff mode, and board-driven
  blocking. Cleanup selections use exact option indices rather than card IDs,
  including duplicate copies, and the Node boundary rejects stale, wrong-count,
  duplicate, fractional, and out-of-range positions. Bluff mode defaults off,
  passes `--bluff-mode` per match when selected, and prevents the client's
  empty-attacker auto-submit. Blocking moves declared attackers into a
  sideways combat lane, derives glowing blockers and legal targets only from
  `decision.choices`, preserves opaque IDs, supports multiple blockers on one
  attacker and reassignment/removal, and exposes `No blocks` plus one confirm
  action instead of detached selectors. White card faces now use a light
  surface with dark ink in normal, compact, selected, and rotated contexts.
  Added `PORT=4175 make web-interaction` as a deterministic duplicate-cleanup
  → numeric-blocking rendered harness. `make test-web-ui` passed 53/53; `make
  test-web` passed 7/7 C++ bridge tests and 60/60 Node tests. Rendered evidence
  is recorded below; only the 1440 × 900 repetition remains pending.
- 2026-07-25 — Independent real-browser smoke at 1280 × 720 passed both
  deterministic fixtures. On `4175`, Bluff mode rendered unchecked; the
  nine-card cleanup hand showed `0/2`, the first two duplicate Plains became
  separately pressed with `DISCARD` labels, and confirmation logged `You
  discarded Plains, Plains`, reduced hand size to seven, and raised graveyard
  count to two. The blocker state moved numeric attackers `210` and `211` into
  the sideways `ATTACKING` lane; blockers `110` and `111` glowed, `110`
  exposed only its one legal target, and assigning both blockers to `210`
  produced `BLOCKED ×2`, two removable `Grizzly Bears → Ironclaw Orcs` chips,
  and a working `Confirm 2 blocks`. Plains and Moat remained high-contrast.
  On `4174`, only the two legal Giant Growth copies glowed, no `PLAY` or action
  cards appeared, a single click enlarged the exact copy and highlighted only
  Grizzly Bears, and the stack rail stayed visible. The 1440 × 900 repetition
  remains open because the available browser viewport was fixed at 1280 × 720.
- 2026-07-25 — Fresh real-engine browser smoke separated normal automatic
  progress from deliberate Bluff-mode pauses. With Bluff mode off, playing the
  turn-one land advanced through empty combat, second main, and the opponent's
  turn to the next human turn without surfacing forced choices. With Bluff mode
  on, the browser visibly paused on the sole first-main priority pass, the
  empty-attacker declaration, end-combat priority, and the sole second-main
  pass. The empty-attacker prompt now says `Bluff mode paused before declaring
  no attackers.`; interruption/recovery wording is reserved for a failed
  automatic request. A focused source regression covers that distinction and
  `make test-web-ui` passed 53/53.
- 2026-07-25 — Promoted the already contract-tested full journey to a
  launchable browser fixture at `PORT=4176 make web-journey`. It deterministically
  covers land play, a creature on the stack and resolution, selecting one of
  two attackers, ordering two blockers, the reciprocal blocker decision,
  match completion, and replaying the exact seed. The fixture route and stable
  command have a focused regression. `make test-web-ui` passed 54/54 and,
  because the fixture server route changed, `make test-web` passed 7/7 C++
  bridge tests plus 61/61 Node tests. Its rendered journey remains open.
- 2026-07-25 — The first `4176` browser run found fixture drift: after the
  Bears cast, the synthetic bridge exposed a one-option `Pass toward
  resolution` prompt that the production bridge suppresses when Bluff mode is
  off. The fixture now records the forced pass as an event and advances
  directly through stack resolution to `Declare attackers`; its 30-match
  contract asserts that transition. A fresh 1280 × 720 run passed the corrected
  cast boundary with no forced control, then completed the already verified
  board-selected attacker, two-blocker damage order, reciprocal block,
  empty-library result, and new-session seed replay flow. `make test-web-ui`
  passed 54/54 and `make test-web` passed 7/7 C++ bridge tests plus 61/61 Node
  tests. The 1440 × 900 repetition remains open.
- 2026-07-25 — The 1280 × 720 journey also exposed that two identical
  `Grizzly Bears` damage-order rows looked unchanged after reordering. Every
  public permanent now shows its engine-provided instance ID on the card, and
  each damage-order row repeats that ID with current public power/toughness.
  The rendered fixture showed board cues `#201`/`#202`, tray rows `#201 · 2/2`
  and `#202 · 2/2`, and an unmistakable swap to `#202` then `#201` after
  `Move later`; controls remained readable within the viewport. Fixture and
  source regressions use only public permanent state, and `make test-web-ui`
  passed 55/55. The 1440 × 900 repetition remains open.
- 2026-07-25 — Implemented the preregistered keyboard mechanics without
  claiming rendered acceptance. A legal hand card now exposes selected state
  through `aria-pressed`; Enter/Space on that selected card reuses the exact
  targetless resolver, submitting one route or opening the existing chooser
  for multiple variants. Targeted cards still require an exact public
  destination. Damage-order buttons include the affected permanent ID in
  their accessible name, and all combat origins/destinations remain native
  buttons with visible-focus styling. `make test-web-ui` passed 56/56. The
  available browser wrapper left `document.activeElement` on `BODY` under Tab
  and focus attempts, so a real rendered keyboard-only journey could not be
  performed; this slice remains open and no native-drag claim was made.
  `make test-web` passed 7/7 C++ bridge tests plus 63/63 Node tests.
- 2026-07-25 — The new public IDs exposed a rules error in the synthetic
  journey transcript: ordering blocker `#202` first incorrectly left `#202`
  alive. The fixture now removes that lethal first blocker, carries survivor
  `#201` into the reciprocal attack, and removes both Bears when they trade.
  Its 30-match contract asserts the survivor and final empty creature zones;
  this was a fixture-only correction and did not change production engine
  combat.
- 2026-07-25 — Completed the bounded P3 reproduction-summary slice. The live
  `REPRO` panel shows both deck/policy pairs, game and training seeds, training
  games, all three rollout counts, Bluff state, and reveal state from the
  normalized public config. Its exact ID form is a read-only selectable field;
  there is deliberately no clipboard API/button and no hand or other private
  state. At 1280 × 720 the rendered panel measured x=201.9, y=58.5, 600 ×
  284.2 px with equal client/scroll dimensions (598 × 282), no page overflow,
  and no movement of the 1042 × 112 hand region when opened. The formatter and
  fixed-layout/source regressions passed in `make test-web-ui` 58/58.
- 2026-07-25 — Replaced the decision-covering action mask with a non-blocking
  `Opponent thinking…` status driven only by the existing in-flight `acting`
  state. The last legal decision stays visible; the established disabled
  buttons and synchronous single-flight guard still reject duplicates.
  Success, ordinary errors, stale-state refreshes, and refresh failures all
  clear the indicator through the same `finally`, while errors retain or
  refresh the snapshot and leave the dismissible toast. Focused assertions
  cover lifecycle ordering, ordinary-error snapshot retention, status
  semantics, and non-intercepting layout; `make test-web-ui` passed 59/59.
  There is no rendered timing claim because the available fixtures respond
  immediately rather than providing a genuine delayed request.
- 2026-07-26 — Implemented the preregistered debug-reveal safety slice. A
  persistent red `DEBUG REVEAL ON` header warning is derived only from the
  server-normalized current match config; normal matches omit it. `New match`
  always initializes reveal off, including when opened from a debug match,
  while `Replay seed` deliberately preserves the exact debug configuration
  and warning. Focused assertions cover the fail-closed initial/setup paths,
  config-only warning source, exact replay behavior, and unmistakable styling;
  `make test-web-ui` passed 60/60. A fresh rendered 1280 × 720 journey on the
  `4176` fixture passed: the reveal match showed the persistent red warning,
  the REPRO `REVEAL` badge, and visible opponent debug cards; `New match`
  opened with the reveal checkbox unchecked; starting that normal replacement
  removed both reveal indicators and rendered exactly seven hidden opponent
  cards with no opponent identities. The old debug match intentionally
  remained visible and warned behind the setup modal until replacement.
- 2026-07-26 — Implemented the preregistered live reproduction-state slice.
  The selectable exact text now appends current public turn, engine phase,
  priority holder (`You` only for an authoritative human priority decision),
  and only the latest normalized public event message. A typed scalar-only
  context keeps hands and zone payloads outside the formatter; an event object
  without a public message is deliberately omitted instead of serialized.
  Pure tests prove the setup prefix stays byte-identical while snapshot
  context advances, explicit missing-state fallbacks, and rejection of extra
  hidden-card-shaped inputs. Source assertions prove each snapshot feeds the
  existing read-only select-on-focus field. A pure decision-kind contract
  additionally proves priority reports `You`, every non-priority combat or
  cleanup decision reports `None`, and absent public state reports `Unknown`;
  it never infers an opponent holder. `make test-web-ui` passed 60/60.
  A rendered 1280 × 720 run on the live `4173` fixture passed. Initially the
  suffix was `turn=1 | phase=first_main | priority-holder=You |
  latest-event="You started turn 1"`; after double-clicking Island and
  automatic progress to turn three, the still-open panel updated to
  `turn=3 | phase=first_main | priority-holder=You |
  latest-event="You started turn 3"` while the prefix through `reveal=off`
  stayed byte-identical. The textarea measured 566 × 79.56 px with equal
  564 × 78 client/scroll dimensions, the 1280 × 720 page had no overflow, and
  the hand stayed fixed at x=238, y=531, 1042 × 112 px. Debug reveal remained
  off and the opponent remained seven hidden cards.
- 2026-07-26 — Reproduced and fixed the journey-fixture liveness failure. On a
  fresh seed-42 `4176` session, submitting the advertised decision 1/index 0
  `Pass priority` returned zero bytes for 2.008 seconds; a concurrent GET
  showed `status=advancing`, `decision=null`, and `Opponent thinking`. The
  scripted fixture had implemented only index 1, and its unexpected-action
  path closed `readline` without destroying child stdin, leaving the server to
  wait its 120-second production timeout. The pass branch now emits the
  authoritative turn-three `declare_attackers` snapshot with Bears `#102`
  eligible, while an actually unsupported fixture branch destroys stdin and
  becomes a failed session promptly. The focused contract settled in
  40–110 ms; the existing server's post-fix HTTP request returned 200 in
  0.002345 seconds. `make test-web-ui` passed 61/61; `make test-web` passed
  7/7 C++ bridge tests and 68/68 Node tests. A fresh rendered 1280 × 720 check
  passed: 350 ms after clicking Pass, the thinking indicator was gone and
  `Declare attackers` showed only `#102`; REPRO reported `turn=3`,
  `phase=declare_attackers`, `priority-holder=None`, and latest event
  `You: Pass priority`. The 566 × 79.56 px textarea had equal 78 px
  client/scroll heights and the 1280 × 720 page had no overflow. Port `4173`
  remained untouched.
- 2026-07-26 — Closed the rendered in-flight indicator gap with an opt-in
  delayed journey fixture. `PORT=4177 make web-delayed-journey` reuses the
  ordinary journey but holds the supported opening pass for a fixed 650 ms;
  ordinary `web-journey` remains instantaneous. Its contract observed a real
  250 ms floor and the same authoritative attackers settlement. `make
  test-web-ui` passed 62/62; `make test-web` passed 7/7 C++ bridge tests and
  69/69 Node tests. In a rendered 1280 × 720 run, roughly 411 ms after the
  click the browser visibly showed `Opponent thinking…` and `Resolving your
  action`; the prior Pass control remained present and disabled, and the main
  board stayed at x=238, y=64, 1042 × 656 px. By roughly 861 ms wall time
  including inspection overhead, the banner was gone, `Declare attackers`
  was authoritative, and Bears `#102` was eligible. The page remained exactly
  1280 × 720 with no overflow throughout. Port `4173` remained untouched.
- 2026-07-26 — Audited the preregistered reduced-motion slice. The existing
  stylesheet already applies a final `prefers-reduced-motion: reduce` boundary
  that changes scrolling to `auto`, reduces animations and transitions to
  0.01 ms, and limits animation iteration to one without hiding content.
  Added a focused regression proving the normal spinner/live pulse declarations
  remain, the reduced override covers elements and pseudo-elements, and it
  contains no `display:none`, hidden visibility, or zero-opacity rule; the
  thinking text remains unconditional on media queries. `make test-web-ui`
  passed 63/63. The available browser API exposed evaluation and locators but
  no media emulation or CDP control, so computed reduced-motion rendering could
  not be tested honestly; no class injection was substituted and this slice
  remains rendered-pending. The normal live `4173` view remained usable with
  `LIVE` visible.
- 2026-07-26 — Narrowed decision announcements to an atomic, polite,
  screen-reader-only public prompt instead of marking the entire interactive
  dock live. Each engine decision replaces a keyed summary containing only
  public turn, formatted phase, heading, and helper text; card and zone
  contents remain outside it. Result dialogs now reference their visible title
  and reason as accessible name and description. `make test-web-ui` passed
  64/64. In a rendered 1280 × 720 inspection on live `4173`, the dock had no
  `aria-live`, exactly one descendant `role=status` had `aria-live=polite` and
  `aria-atomic=true`, and its text was `Turn 1, First Main. Priority actions.
  Choose a legal action from the battlefield or pass priority.` The dock stayed
  at x=238, y=643, 1042 × 78 px and the page remained exactly 1280 × 720 with
  controls and board unchanged. A rendered finished-result dialog was not
  convenient in this smoke, so its naming remains structurally tested rather
  than claimed as rendered acceptance.
- 2026-07-26 — Implemented and rendered the preregistered result-dialog focus
  mechanics. A finished or failed snapshot moves
  focus to the named and described dialog container without auto-activating an
  outcome action. `Change matchup` and `Replay seed` remain ordinary buttons
  in DOM order with no Tab interception or focus trap. Escape is consumed
  without dismissing the completed result and does not move focus. A focused
  lifecycle/semantics regression plus the complete client build passed in
  `make test-web-ui` 65/65; `git diff --check` is clean. A rendered 1280 × 720
  `4176` journey completed the full land/cast/attack/order/block flow. The
  centered result had no horizontal overflow (`scrollWidth=clientWidth=1280`);
  `.game-over-layer` was the active element with role `dialog`, `tabindex=-1`,
  visible accessible name `You won`, and visible description `The losing
  player tried to draw from an empty library.` Both enabled native buttons
  appeared in the intended DOM order, and focused `Change matchup` had a
  visible gold focus ring. Escape left the dialog open and that exact button
  focused. The available browser wrapper did not advance native focus when it
  dispatched Tab, so the no-trap source regression and rendered enabled DOM
  order are green but a live Tab traversal is deliberately not claimed.
- 2026-07-26 — Implemented and rendered the preregistered setup-dialog focus
  lifecycle. Opening setup records the exact active
  element and focuses its visible Close button; cleanup restores only a still-
  connected opener. The dialog is named by its visible `Set the table`
  heading. Its backdrop remains pointer-dismissible but is removed from
  sequential keyboard navigation, while explicit Tab/Shift+Tab boundary
  handling contains focus among enabled drawer controls. Escape closes through
  the existing setup callback. The complete client build and focused
  lifecycle/semantics regression passed in `make test-web-ui` 66/66. A
  refreshed `4176` browser run passed at exactly 1280 × 720. The live board
  stayed at `{x:238,y:108,w:1042,h:422}` before and after setup, with
  `scrollWidth=clientWidth=1280`. The dialog's referenced visible title
  resolved to `Set the table`; its visible Close control received initial
  focus; the backdrop had `tabindex=-1`; and its 12 enabled controls ran from
  Close through `Start match`. Tab on the last control wrapped to that exact
  Close control, and Shift+Tab on Close wrapped back to `Start match`.
  Closing and a separate Escape dismissal each restored focus to the unique,
  exact top-bar `New match` opener while removing the dialog.
- 2026-07-26 — Implemented and rendered the preregistered subdued-text
  contrast change. The shared faint token is now
  `#829089`, which measures 5.11:1 against `--panel-2`; event indices and
  inactive phase names use that token instead of lower-contrast one-off
  colors. Active gold, primary ink, selection, and card colors are unchanged.
  A computed WCAG AA regression plus the complete client build passed in
  `make test-web-ui` 67/67. A refreshed 1280 × 720 `4176` check found the exact
  computed colors (`rgb(130, 144, 137)` supporting,
  `rgb(241, 211, 138)` active), and the muted phase/event/setup labels were
  visibly readable without competing with gold or primary text. The board
  stayed `{x:238,y:108,w:1042,h:422}` and the page remained 1280/1280.
  Opening setup exposed a separate real layout failure: its 904px client was
  1590px wide internally with a visible horizontal scrollbar and cramped
  right/lower controls. That failure is explicitly not hidden by this
  contrast pass and moved immediately into the P1 horizontal-fit slice.
- 2026-07-26 — The setup-drawer horizontal-fit hypothesis was **refuted at
  diagnosis**: the five-column `.simulation-settings` grid was already
  contained at 840px (`156.484 142.266 142.266 142.266 170.703`). The actual
  protruders were its two visually hidden checkbox inputs. A generic
  `.simulation-settings input { width: 100% }` rule made each absolutely
  positioned checkbox 904px wide; one extended 687.28px beyond the drawer and
  the other 56px. The fix leaves the proven grid untouched, constrains only
  those native toggle inputs to 1 × 1px, and gives their adjacent visible
  tracks the standard gold focus outline. The complete client build and a
  focused sizing/focus regression passed in `make test-web-ui` 68/68. A
  refreshed 1280 × 720 `4176` rendered recheck passed: the drawer became
  `clientWidth=scrollWidth=904` with vertical scrolling only
  (`clientHeight=720`, `scrollHeight=786`); both checkbox inputs were exactly
  1 × 1px and contained within its right edge. All 12 enabled controls
  remained present, with `Start match` reachable by ordinary vertical scroll.
  Focusing each checkbox drew the adjacent track's exact
  `rgb(241, 211, 138) solid 2px` outline at a 3px offset, visibly clear in the
  rendered view. Escape removed setup, restored the exact opener, preserved
  the board at `{x:238,y:108,w:1042,h:422}`, and left the page at 1280/1280.
- 2026-07-26 — Implemented the preregistered setup-text legibility change
  and accepted it after rendered verification. Header guidance, Deck/Pilot and
  simulation labels, policy descriptions, Shuffle, toggle names/descriptions,
  and footer guidance now use the existing 12px `--type-label` token. Compact
  eyebrows/metadata remain at the 11px floor; grids, drawer width, controls,
  board/card typography, and the contained 1 × 1px toggles are unchanged. The
  complete client build and a focused typography/layout regression passed in
  `make test-web-ui` 69/69. On refreshed `4176` at 1280 × 720 every intended
  selector computed to 12px while `MATCH ROOM` and seat subtitles remained
  intentionally compact at 11px. The rendered guidance was materially easier
  to read and stayed subordinate to white headings and gold section labels.
  The two-seat/settings hierarchy had no clipping; the drawer stayed 904/904
  wide with 12 controls. Focusing `Start match` brought it fully into view at
  y=633.6–681.6 via ordinary vertical scroll (`scrollTop=89`,
  `scrollHeight=809`). Escape restored the exact opener, kept the board at
  `{x:238,y:108,w:1042,h:422}`, and left the page at 1280/1280.
- 2026-07-26 — Implemented the preregistered public-event formatting boundary
  and accepted it after rendered verification. MatchLog now calls a typed helper that
  reads only a string entry or explicit `message`, `text`, and `label` prose;
  numeric turn/player and string kind remain metadata. An event with no public
  prose becomes `Game event`, and a hidden-shaped object carrying
  `Ancestral Recall`, `Counterspell`, and `Lightning Bolt` in incidental
  card/hand/graveyard fields produced none of those names or field keys. The
  REPRO latest-event helper shares the explicit-message reader but preserves
  its `undefined` fallback. The complete client build and pure/source privacy
  regression passed in `make test-web-ui` 70/70. On refreshed reveal-off
  `4176` at 1280 × 720, the Chronicle remained ordered and readable:
  `You started turn 3`, `You: Play Forest`, `You: Cast Grizzly Bears`,
  `You: Pass priority`, `Resolved your Grizzly Bears`, and
  `You started turn 4`. No JSON braces, field-name serialization,
  `Ancestral Recall`, or neutral `Game event` appeared; the opponent remained
  one hidden seven-card hand. The board was x=238, y=64, 1042 × 656 and page
  client/scroll/inner widths were all 1280.
- 2026-07-26 — Implemented the preregistered public card-state naming change
  and accepted it after rendered verification. CardFace now builds its tooltip and
  accessible name from one list of already-rendered/public props: identity,
  permanent ID, type, cost, flying, creature power/toughness, tapped,
  summoning-sick, marked-damage, stack-target, and legal
  destination/origin cues; selection remains `aria-pressed`. An initial gate
  caught capitalization drift in the established `permanent #…` wording, so
  the implementation was corrected rather than weakening that regression.
  During exact-label preparation, the fixture also exposed that bridge
  noncreatures carry zero power/toughness fields and were visibly rendered as
  0/0. A pure public-type predicate now shows combat stats for creature types,
  with field-presence fallback only when type is absent; tests cover creature,
  artifact creature, land, instant, and missing-type cases. The existing
  `4174` target fixture's Bears #110 now carries fixture-only
  `summoningSick=true` and one marked damage so every state can be rendered
  without changing legality or action payloads. `make test-web-ui` passed
  72/72; `make test-web` passed 7/7 C++ bridge tests and 79/79 Node tests. On a
  fresh 1280 × 720 `4174` session, exact hand names were
  `Giant Growth, instant, Cost G`, `Forest, land` (with no 0/0), and
  `Grizzly Bears, creature, Cost 1G, 2 power, 2 toughness`. Tapped Forest
  #101 added `Tapped`; Bears #110 included its permanent ID, creature/cost/
  stats, `Summoning sick`, `1 damage marked`, and stack-targeted state.
  Selecting an enabled Giant Growth set its `aria-pressed=true` and added
  `Legal destination for the selected action` to #110. Visible
  `SICK`, `−1`, `TARGET`, and `CHOOSE` cues all matched. The opponent remained
  one hidden-hand group; page widths stayed 1280/1280/1280 and the stack-side
  main board stayed x=238, y=64, 782 × 656 with no overflow.
- 2026-07-26 — Implemented the preregistered explicit damage cue without
  claiming rendered acceptance. The compact red badge now renders
  `DMG {markedDamage}` rather than the ambiguous `−{markedDamage}`; the shared
  public card name still says `{markedDamage} damage marked`, and no engine
  value or other state cue changed. A focused regression plus the complete
  client build initially passed in `make test-web-ui` 72/72, but the first
  rendered 1280 × 720 gate **failed**: `DMG 1` overlapped `SICK` by 5.39px,
  and both bottom state badges intersected `CHOOSE` vertically by 1px. The
  visible wording was therefore not accepted. State badges now have an
  explicit 17px height and separated vertical lanes: damage 35px from the
  card bottom, sickness 14px, and the action cue −7px. A geometric regression
  proves at least a 2px gap between adjacent lanes; the corrected
  `make test-web-ui` passed 73/73. The refreshed `4174` rendered gate then
  passed. On Bears #110, `TARGET` was x=552.008/y=333/60.984 × 17,
  `DMG 1` x=542.5/y=374/46.438 × 17, `SICK`
  x=583.547/y=395/37.953 × 17, and `CHOOSE`
  x=550.203/y=416/64.594 × 17; all six pairwise intersection areas were
  exactly zero. Its accessible name retained `1 damage marked`, and the
  selected Giant Growth remained pressed. The 72 × 88 card stayed readable,
  main board remained x=238, y=64, 782 × 656, and viewport/body widths stayed
  1280 with no overflow.
- 2026-07-26 — Implemented and rendered the preregistered explicit flying cue.
  Flying cards now show the literal `FLYING` keyword inside their existing
  illustration field, derived only from `card.flying`; non-fliers retain the
  neutral/land glyph. The existing shared public description still supplies
  `Flying` to both tooltip and accessible name. A focused source/CSS
  regression plus the complete client build passed in `make test-web-ui`
  74/74. A real-engine Blue-versus-Red/Random match at seed 42 passed at
  1280 × 720: opening Air Elemental's exact accessible label was
  `Air Elemental, creature, Cost 3UU, Flying, 4 power, 4 toughness`; its sole
  `FLYING` cue was x=600.689/y=588.433/58.311 × 21.799 and wholly contained
  inside the x=599.181/y=580.572/61.328 × 37.522 card field. Exactly one hand
  card had the cue and all six non-fliers had none. The seven-card fan retained
  its intentional adjacent overlap while nonadjacent cards did not intersect;
  the word remained visibly readable. Hand and decision dock shared only their
  y=643 edge with zero intersection area, the opponent hand stayed hidden, and
  viewport/document/body widths remained 1280 with no overflow.
- 2026-07-26 — Implemented the preregistered recoverable-error announcement
  slice and passed the complete client build plus focused lifecycle/semantics
  assertions in `make test-web-ui` 75/75. Failure to create a match and failure
  to submit an action now produce atomic alerts with the exact error and a
  native `Dismiss` button; the non-error shuffling/training toast remains a
  polite status. An isolated `4179` journey session was deliberately made
  unreachable only after its opening priority decision rendered. Submitting
  Pass produced an immediate `role=alert`, `aria-atomic=true`,
  `Failed to fetch` toast at x=1089.703/y=657/168.297 × 41. The prior Turn 3
  priority prompt, two legal Forest cards, and Pass remained rendered and
  enabled, all thinking/training status cleared, exact focus could move to
  Dismiss, pointer dismissal removed the alert without changing the decision,
  and viewport/document/body dimensions remained 1280 × 720 with no overflow.
  Rendered keyboard activation is deliberately **inconclusive**: both semantic
  and physical Enter/Space attempts failed on Dismiss, but the same methods
  also failed on the ordinary native `New match` button in a calibration.
  Because this is a browser-control limitation rather than evidence specific
  to the toast, no redundant key handler was added; the native-button source
  gate is green, while that rendered subgate remains open.
- 2026-07-26 — Added the first automated rendered-interaction gate:
  `make test-web-rendered` builds the production client, starts the existing
  target-stack bridge on an ephemeral localhost port, and drives a stepped
  native pointer drag in Chromium at both 1280 × 720 and 1440 × 900. The fixed
  configuration is Green/human versus Red/Learned Value, game seed `42`,
  training seed `424242`, 800 training games, Bluff mode off, and reveal off.
  Before release, the legal Giant Growth became the selected dragging card and
  Bears `#110` became the rendered exact destination. Release produced exactly
  one action request, changed hand size 7→6, grew the stack 2→3, tapped Forest
  `#102`, retained both public `TARGET` cues, and showed no alert. Passing
  priority produced the second exact request, the fresh
  `target-stack-priority-3` decision, and stack size 3→2. Both viewports kept
  hand, decision dock, and stack rail wholly rendered, with zero hand/dock
  intersection and document/body width exactly equal to the viewport.
  The first rendered run also found that the console's fixed hand and decision
  tracks plus its one-pixel top border extended the dock to y=721 and y=901.
  Replacing that space-consuming border with the same inset divider fixed the
  clipping without changing either track height; a focused structural
  regression now preserves it. Final results: `make test-web-ui` 75/75,
  `make test-web` 9/9 C++ bridge tests plus 82/82 Node tests, and
  `make test-web-rendered` 2/2 rendered journeys. Port `4173` was untouched.
- 2026-07-26 — Closed the auto-pass rendered-coverage gap with two
  response-driven Playwright journeys against the production server and real
  C++ bridge. Both use Green/human versus Red/Random, game seed `42`, one
  training game, training seed `424242`, and the default `2/8/2` rollout
  settings (unused by Random) at 1280 × 720. With Bluff off, playing the
  opening Forest returned decision `2` as engine-authored `attackers` with no
  eligible IDs; the client then submitted exactly
  `{decisionId: 2, ids: []}` and reached the next human first main on turn 3.
  No sole-Pass first-main or beginning-combat decision was exposed. With Bluff
  on, the same play retained the sole-Pass first-main decision, a deliberate
  pass retained the sole-Pass beginning-combat decision, and the next pass
  left the zero-eligible attacker prompt visibly paused with `Bluff mode paused
  before declaring no attackers.` Exactly three deliberate action requests
  were observed and no fourth auto-submit occurred.
  The gate listens for authoritative HTTP responses and DOM transitions; it
  contains no fixed sleep or browser-side legality model. The rendered target
  now builds the real bridge as an explicit prerequisite. Final results:
  `make test-web-ui` 75/75 and `make test-web-rendered` 4/4. The first
  sandboxed rendered invocation was infrastructure-only `listen EPERM`;
  rerunning the identical target with localhost permission passed.
- 2026-07-26 — Closed P1 with the pending real-browser full-journey repetition
  at 1440 × 900 on the deterministic journey fixture (`PORT=4186 make
  web-journey`). `Bluff mode` began unchecked. Double-clicking Forest logged
  `You: Play Forest`; double-clicking Grizzly Bears auto-passed the empty
  timing windows and reached turn-four attackers. Selecting permanent `#101`
  and `Attack with 1` led to two opposing blockers. Moving `#201` later made
  the visible damage order `#202`, then `#201`; confirming it advanced to the
  opponent attack. Selecting blocker `#102`, then attacker `#201`, and
  confirming the block produced the focused `You won` dialog with the
  empty-library reason, 5 turns, and 13 public events. `Replay seed` returned
  a fresh session to the exact turn-three First Main opening decision with a
  seven-card hand, two playable cards, and the same seed/config; the Chronicle
  reset from 13 entries to one. The opponent remained a seven-card hidden fan
  with no identities. Viewport/body/document widths were
  1440/1440/1440 with no horizontal overflow. Every fixed region remained
  visible: Chronicle `{x:0,y:64,w:238,h:836}`, main
  `{x:238,y:64,w:1202,h:836}`, opponent
  `{x:258,y:120,w:1162,h:264}`, human
  `{x:258,y:408,w:1162,h:264}`, hand
  `{x:238,y:680,w:1202,h:142}`, decision dock
  `{x:238,y:822,w:1202,h:78}`, and hidden-hand fan
  `{x:893,y:117,w:103,h:43}`. Supporting gates on the same tree were
  `make test-web-ui` 75/75 and `make test-web-rendered` 4/4.
- 2026-07-26 — Implemented and rendered the preregistered explicit
  stack-controller slice. The initial fixed target-stack run failed at both
  1280 × 720 and 1440 × 900 with no `.stack-controller` cues despite public
  controller fields; the two interaction journeys were otherwise green.
  Stack entries now derive a visible `YOU`/`OPPONENT` chip and accessible
  `Controlled by You/Opponent` name only from the public controller seat and
  configured human seat. Missing or invalid seats return no invented label.
  In the corrected dual-viewport journey, top-to-bottom ownership read
  `OPPONENT, YOU` on the original two-object stack; dragging Giant Growth to
  Bears `#110` changed it to `YOU, OPPONENT, YOU`; passing toward resolution
  restored `OPPONENT, YOU`. Every chip remained wholly inside its own stack
  object with zero intersection against `NEXT`/`+N`, while the existing exact
  action count, targets, hand/stack transitions, and overflow gates remained
  green. Final results: `make test-web-ui` 75/75 and
  `make test-web-rendered` 4/4. The rendered server used ephemeral localhost
  ports; port `4173` was untouched.
- 2026-07-26 — Implemented and rendered the preregistered
  forced-empty-blockers slice. A fixed seed-42 interaction fixture exposes one
  opposing attacker, no human creatures, `choices: []`, and opaque decision ID
  `forced-empty-blockers:opaque/17`; its pre-fix rendered reproduction failed
  after receiving zero action requests and leaving `No blocks` visible. The
  client now reads only that authoritative empty choice set, submits exactly
  `{decisionId: "forced-empty-blockers:opaque/17", pairs: []}` once, and does
  so even with Bluff mode enabled. The corrected 1280 × 720 Chromium journey
  reached the fixture's second-main priority, showed one enabled
  `Pass priority`, no `No blocks`, no alert, and no horizontal overflow.
  `make test-web-ui` passed 77/77; `make test-web` passed 9/9 C++ bridge tests
  and 84/84 Node tests. The first complete rendered run had a transient timeout
  in the unchanged 1440 × 900 Giant Growth journey; that exact journey passed
  alone in 2.33 seconds, and an identical full rerun passed all 5/5 rendered
  journeys in 8.23 seconds. The in-app browser had no available browser
  binding, so the required render evidence comes from the repository's real
  Chromium/Playwright gate on ephemeral localhost ports; port `4173` was
  untouched.
- 2026-07-26 — Completed the preregistered explicit Learned-model identity
  slice. Setup now advertises seven policies with separate `Learned Value C16`
  and `Learned Value G0` choices; C16 is the normal default at K=8/H=4, while
  G0 remains an explicit per-match training path. The web bridge accepts and
  reports generation separately from rollout width. Its C16 branch requires
  exact T800/S424242/C16 metadata, resolves the artifact relative to the bridge
  executable rather than the Node process's `web/` working directory, loads
  only through the canonical artifact reader, and verifies exact fingerprint
  `68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f`;
  it contains no train/refresh/substitution fallback. Missing, stale,
  noncanonical, and wrong-fingerprint paths fail closed with an actionable
  separate CLI generation command. Structured, whitelisted model identity
  survives the server boundary and the REPRO panel shows actual family,
  generation, K/H, source, and the full fingerprint without receiving any
  state or hidden-card payload. `make test-web` passed 12/12 C++ bridge tests
  and 90/90 Node tests; `make test-web-ui` passed 82/82 including the 35-case
  five-deck × seven-policy matrix. The first rendered invocation was
  infrastructure-only `listen EPERM`; the permitted identical full rerun
  passed 7/7 Playwright journeys. At both 1280 × 720 and 1440 × 900, a real
  bridge launched from the web workflow loaded the exact C16 artifact, the
  opponent HUD visibly named `Learned Value C16`, and the open REPRO panel
  visibly contained `learned-value C16 · K8/H4` plus the complete fingerprint,
  with the panel wholly inside the viewport and no page overflow.
- 2026-07-26 — Implemented and rendered the preregistered concise Chronicle
  slice. Priority-action events now carry the bridge's existing rules-level
  action kind as a public scalar, and the client filters only the structured
  `priority_action` + `pass` combination; it does not parse prose or inspect
  card payloads. `Show priority passes` is an accessible native checkbox that
  starts unchecked. The visible event list and count omit passes by default,
  use consecutive display ordinals, and emit `Turn N` only when the turn
  changes among those visible events, so a filtered leading pass cannot consume
  the marker. Focused regressions include deliberately misleading prose and
  filtered first-in-turn cases. `make test-web-ui` passed 84/84;
  `make test-web` passed 13/13 C++ bridge tests and 92/92 Node tests. The first
  rendered invocation was infrastructure-only `listen EPERM`; the permitted
  identical rerun passed all 7/7 real-Chromium journeys. In the fixed seed-42
  target-stack journey at both 1280 × 720 and 1440 × 900, the checkbox
  visibly began unchecked, one pass stayed hidden, checking it revealed that
  pass in its original order, unchecking hid it again, ordinals/counts changed
  3→4→3 without gaps, and the four same-turn events retained exactly one
  `Turn 4` marker. Hand/stack transitions, two exact action requests, and
  horizontal-overflow gates remained green. The delegated browser runner had
  no available binding, so the repository gate used ephemeral localhost
  ports. A separate in-app-browser smoke then opened the live port `4173` at
  1280 × 720: after one first-main pass the default Chronicle showed only
  `You started turn 1` and `You declared no attackers`, with consecutive
  `01`/`02` ordinals and one `Turn 1` marker. Checking the control revealed
  all six intervening priority passes in order, changed the visible count to
  eight with `01`–`08` ordinals, preserved the single turn marker, and kept
  document/viewport width at exactly 1280.
- 2026-07-26 — Implemented the Learned-policy provenance slice without
  changing the deployed model. C16 now advertises its 2026-07-26 artifact
  freeze, 16-generation bootstrapped Value lineage, K8/H4 deployment, and
  research-control status. G0 and Actor advertise their 2026-07-24 recipe
  dates, per-match training lifecycle, and distinct Value versus direct
  policy-head lineages. Server metadata and the client fallback carry the same
  structured date/label/lifecycle fields; the selected pilot renders a
  semantic `time` element, readable lifecycle label, and full description.
  `make test-web-ui` passed 85/85. `make test-web` passed 13/13 C++ bridge
  tests and 93/93 Node tests. The new 1280 × 720 Playwright assertion is
  present, but the rendered run is infrastructure-incomplete: the sandbox
  rejected every ephemeral `127.0.0.1` listener with `EPERM`, the permitted
  rerun could not be authorized because the execution service reported its
  usage limit, and the existing in-app browser policy explicitly rejected
  port 4173. No alternate browser or port was used. The visual milestone
  remains open until that exact rendered check can run.
- 2026-07-26 — Completed the preregistered ephemeral deck-evolution menu
  slice. The separate `Evolve deck` dialog submits only bounded
  generations/population/repetitions/seed/pilot settings to a no-shell child
  running the existing C++ `evolve_deck` loop. Its versioned one-line result
  carries the exact 40 numeric card IDs plus a checked compact manifest,
  aggregate and five-opponent W-L-D/fitness, and generation trace. Handcoded
  Policy and load-only frozen C16 are the only pilots. Node validates every
  parameter, card identity/count, result total, percent, opponent row, and
  request/result identity; limits output/runtime, permits one child at a time,
  and retains completed results and saved decks only in process memory.
  Timeout/oversize teardown now keeps the concurrency gate closed through
  actual child exit, using bounded SIGTERM-to-SIGKILL escalation. Saved deck
  IDs are opaque, client-authored manifests are rejected, and the exact
  engine-produced vector—not a browser reconstruction—is forwarded to either
  match seat. The C++ boundary also serializes seeds as decimal strings so no
  JSON-number rounding can corrupt identity.

  Focused and integrated gates passed: 18/18 C++ bridge tests,
  `make test-web-ui` 87/87, and `make test-web` 106/106 Node/UI tests,
  including 11 evolution server lifecycle/protocol cases. The first rendered
  invocation failed only because the sandbox denied every ephemeral localhost
  listener with `EPERM`; the permitted identical rerun passed 10/10 real
  Chromium journeys. At both 1280 × 720 and 1440 × 900 it generated a
  two-generation 40-card result, rendered both manifest rows, both trace rows,
  and all five opponent rows, saved it with the explicit restart-lifetime
  notice, reopened `New match`, and selected the returned opaque deck ID for
  both seats with document/body width equal to the viewport. A final
  production-server/real-bridge smoke evolved a 75% candidate at seed 42,
  saved all 40 cards into dynamic metadata, and started a game with that
  opaque deck at a live `priority` decision. No filesystem, database, cookie,
  localStorage, or sessionStorage persistence was introduced.
- 2026-07-28 — Exposed the defender-best-response attack challenger under the
  stable ID `learned-value-c16-adversarial-blocks` and visible name
  `Learned C16 · Best-Response Attacks`. The C++ parser and pure policy
  translation prove canonical C16 and the challenger are identical across
  every `BotConfig` field except `value_adversarial_blocks`; incompatible
  programmatic configurations fail before session output. Node pins both
  frozen policies to T800/S424242/C16, forwards the distinct policy ID, and
  preserves the same generation-16 K8/H4 model identity. Metadata records the
  July 28 exploratory lifecycle, 127–113 fast screen, pending human
  play-test, and non-promotion status. `make test-web-ui` passed the production
  client build and 92/92 UI/contract tests, including the complete 40-case
  five-deck × eight-policy journey matrix. `make test-web` passed 19/19 C++
  bridge tests and 112/112 Node/client/session tests. The rebuilt live server
  was then restarted and a real in-app-browser smoke at 1280 × 720 selected
  the new pilot, verified its dated exploratory provenance and pinned
  T800/S424242 controls, and started an RU Aggro mirror at turn 1 with the
  correct opponent policy shown. Broader human strategic play-testing remains
  the purpose of this exposure.
- 2026-07-28 — Exposed `Learned C16 · Stack Discipline` as a separate,
  explicitly non-promoted behavior diagnostic. It reuses exact frozen C16
  K8/H4 plus the attack treatment and enables the existing rules-only
  Pass-dominance filter; attack-only remains separately selectable. Metadata
  reports the exact 30–30 screen and failed performance gate. Parser/config
  tests fail closed on non-C16, non-K8, or Pass-dominance-without-attack
  combinations. `make test-web-ui` passed 97/97 including the complete
  45-case five-deck × nine-policy journey matrix; `make test-web` passed
  19/19 C++ bridge and 118/118 Node/client/session tests. After the live
  restart, a real 1280 × 720 in-app-browser smoke selected the diagnostic,
  verified its precise provenance and frozen controls, and started an RU
  Aggro mirror at turn 1 with the correct opponent label. The owner's manual
  counter-sequencing test is now the open gate.
