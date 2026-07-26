#!/usr/bin/env node

import readline from "node:readline";

const write = (message) => {
  process.stdout.write(`${JSON.stringify(message)}\n`);
};

const duplicateForest = {
  id: "card-1",
  name: "Forest",
  type: "Basic Land",
};
const mountain = {
  id: "card-2",
  name: "Mountain",
  type: "Basic Land",
};

write({
  type: "decision",
  state: {
    turnNumber: 4,
    activePlayer: 0,
    startingPlayer: 0,
    phase: "cleanup",
    stack: [],
    players: [
      {
        life: 20,
        librarySize: 30,
        handSize: 3,
        hand: [duplicateForest, duplicateForest, mountain],
        graveyard: [],
        exile: [],
        lands: [],
        creatures: [],
        artifacts: [],
        enchantments: [],
      },
      {
        life: 20,
        librarySize: 30,
        handSize: 2,
        graveyard: [],
        exile: [],
        lands: [],
        creatures: [],
        artifacts: [],
        enchantments: [],
      },
    ],
  },
  decision: {
    id: "cleanup-1",
    kind: "cleanup_discard",
    count: 2,
    options: [
      { index: 0, card: duplicateForest },
      { index: 1, card: duplicateForest },
      { index: 2, card: mountain },
    ],
  },
});

const input = readline.createInterface({
  input: process.stdin,
  crlfDelay: Infinity,
});

input.on("line", (line) => {
  let action;
  try {
    action = JSON.parse(line);
  } catch {
    process.stderr.write("invalid test action\n");
    process.exitCode = 2;
    input.close();
    return;
  }

  if (
    action.decisionId === "cleanup-1" &&
    Array.isArray(action.indices) &&
    JSON.stringify(action.indices) === JSON.stringify([0, 1])
  ) {
    write({
      type: "event",
      state: {
        turnNumber: 5,
        activePlayer: 1,
        startingPlayer: 0,
        phase: "untap",
        stack: [],
        players: [
          {
            life: 20,
            librarySize: 30,
            handSize: 1,
            hand: [mountain],
            graveyard: [duplicateForest, duplicateForest],
            exile: [],
            lands: [],
            creatures: [],
            artifacts: [],
            enchantments: [],
          },
          {
            life: 20,
            librarySize: 30,
            handSize: 2,
            graveyard: [],
            exile: [],
            lands: [],
            creatures: [],
            artifacts: [],
            enchantments: [],
          },
        ],
      },
      event: {
        kind: "cards_discarded",
        player: 0,
        cards: [duplicateForest, duplicateForest],
        label: "You discarded Forest, Forest",
      },
    });
    write({
      type: "game_over",
      state: {
        turnNumber: 5,
        activePlayer: 1,
        startingPlayer: 0,
        phase: "untap",
        stack: [],
        players: [
          {
            life: 20,
            librarySize: 30,
            handSize: 1,
            hand: [mountain],
            graveyard: [duplicateForest, duplicateForest],
            exile: [],
            lands: [],
            creatures: [],
            artifacts: [],
            enchantments: [],
          },
          {
            life: 0,
            librarySize: 30,
            handSize: 2,
            graveyard: [],
            exile: [],
            lands: [],
            creatures: [],
            artifacts: [],
            enchantments: [],
          },
        ],
      },
      result: { winner: 0, reason: "life", turns: 5 },
    });
    input.close();
    return;
  }

  process.stderr.write(`unexpected test cleanup action: ${line}\n`);
  process.exitCode = 3;
  input.close();
});
