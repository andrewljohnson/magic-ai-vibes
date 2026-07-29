#!/usr/bin/env node

import readline from "node:readline";

function write(value) {
  process.stdout.write(`${JSON.stringify(value)}\n`);
}

write({
  type: "decision",
  state: {
    turnNumber: 1,
    activePlayer: 0,
    startingPlayer: 0,
    phase: "first_main",
    players: [
      { life: 20, handSize: 7, librarySize: 33 },
      { life: 20, handSize: 7, librarySize: 33 },
    ],
    stack: [],
  },
  decision: {
    id: "timeout-priority-1",
    kind: "priority",
    phase: "first_main",
    options: [{ index: 0, label: "Pass priority", kind: "pass" }],
  },
});

const input = readline.createInterface({
  input: process.stdin,
  crlfDelay: Infinity,
});

input.on("line", () => {
  // Intentionally stay silent so the server's bounded action timeout fires.
});
