# Old School Arena web client

This local React client talks to a Node session server, which starts one
structured C++ engine bridge per game. The browser never invents legal actions:
every button comes from the engine's current legal-action list.

From the repository root:

```sh
make web
```

Then open <http://127.0.0.1:4173>. `Ctrl-C` stops the server and its active game
processes.

Useful development commands:

```sh
npm --prefix web run build
npm --prefix web test
make test-web
```

For a deterministic visual check of a multi-object stack, public permanent
targets on both battlefields, and a legal instant response:

```sh
PORT=4174 make web-target-stack
```

Open <http://127.0.0.1:4174>, choose Green versus Red with seed `42`, and
start the match. This uses a production-shaped test bridge rather than the
game engine; it is only a rendering/interaction fixture. The first decision
can cast Giant Growth in response, growing the stack from two objects to
three while preserving the engine-shaped numeric permanent IDs.

The normal view keeps the opponent hand hidden. The match setup has an
explicitly labeled debug reveal for behavior inspection.

When you have priority, playable cards in your hand glow. Click one to select
and raise it in place; its exact legal battlefield, player, and stack
destinations are highlighted. Double-click a card with one non-targeted route
to play it immediately, or drag it onto your battlefield. Targeted cards can
be dragged directly onto the highlighted permanent, player, or spell.
When the engine still exposes several exact options after origin and
destination selection, a compact parameter chooser appears. Pass priority
remains a separate compact control.

During blocking, attackers move into the combat lane. Select or drag a glowing
blocker onto an attacker, then confirm the displayed pairings or choose
`No blocks`. Cleanup discards are selected directly from the hand; duplicate
copies remain distinct by their exact hand positions.

`Bluff mode` is off by default. Enable it before starting a match when you want
the game to pause for pass-only priority windows and empty attack declarations
instead of automatically advancing those forced choices.

For a deterministic browser journey covering a land play, creature spell and
stack resolution, attacker selection, damage ordering, blocking, game over,
and seed replay:

```sh
PORT=4176 make web-journey
```

Open <http://127.0.0.1:4176> and start any matchup with Bluff mode off. This
production-shaped fixture uses the same session server and action validation as
the normal client, but fixed Green battlefield states make the complete
interaction sequence reproducible.

To inspect the real in-flight UI without waiting on a trained bot, launch the
same opening with an intentional 650 ms delay on its first `Pass priority`:

```sh
PORT=4177 make web-delayed-journey
```

The ordinary journey remains instantaneous. In delayed mode,
`Opponent thinking…` stays visible and non-blocking during the bounded
request, then clears when `Declare attackers` arrives.
