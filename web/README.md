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

The normal view keeps the opponent hand hidden. The match setup has an
explicitly labeled debug reveal for behavior inspection.
