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

When you have priority, playable cards in your hand are marked `PLAY`. Click
one to inspect a large version without taking an action; the listed choices
only focus the corresponding exact action in the tray. You can also drag the
card directly onto one of the highlighted `DROP TO PLAY` actions. When one
spell has several legal targets, each target remains a separate drop choice.
