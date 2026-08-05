#!/usr/bin/env node

import readline from "node:readline";

const args = process.argv.slice(2);
const valueAfter = (flag, fallback = null) => {
  const index = args.indexOf(flag);
  return index < 0 ? fallback : args[index + 1];
};

const writeJson = (value) => {
  process.stdout.write(`${JSON.stringify(value)}\n`);
};

if (args.includes("--evolve-json")) {
  if (args.includes("--evolution-ignore-sigterm")) {
    process.on("SIGTERM", () => {});
  }
  const delay = Number(valueAfter("--evolution-delay-ms", "0"));
  if (Number.isFinite(delay) && delay > 0) {
    await new Promise((resolve) => setTimeout(resolve, delay));
  }
  if (args.includes("--evolution-fail")) {
    process.stderr.write("fixture evolution failure\n");
    process.exit(7);
  }
  if (args.includes("--evolution-oversize")) {
    process.stdout.write("x".repeat(8_192));
    process.exit(0);
  }

  const generations = Number(valueAfter("--generations", "3"));
  const population = Number(valueAfter("--population", "11"));
  const gamesPerOpponent = Number(valueAfter("--games", "1"));
  const cardIds = Array.from(
    { length: 40 },
    (_, index) => (index % 2 === 0 ? 0 : 2),
  );
  if (args.includes("--evolution-invalid")) cardIds[0] = 1;
  const opponentDecks = [
    ["rg-berserk", "RG Berserk"],
    ["atog", "Atog"],
    ["br-midrange", "BR Midrange"],
    ["robots", "Robots"],
    ["white-weenie", "White Weenie"],
    ["uwr", "Lion-dib-bolt"],
  ];
  const games = gamesPerOpponent * 4;
  const fitness = {
    games,
    wins: games / 2,
    losses: games / 2,
    draws: 0,
    winRate: 50,
  };
  const aggregateWins = args.includes("--evolution-inconsistent-total")
    ? (games * opponentDecks.length) / 2 + 1
    : (games * opponentDecks.length) / 2;
  writeJson({
    type: "evolution_result",
    schemaVersion: 1,
    seed: valueAfter("--seed"),
    pilot: valueAfter("--evolve-pilot"),
    parameters: {
      generations,
      population,
      gamesPerOpponent,
    },
    deck: {
      size: 40,
      cardIds,
      cards: [
        { id: 0, name: "Forest", count: 20 },
        { id: 2, name: "Grizzly Bears", count: 20 },
      ],
    },
    fitness: {
      games: games * opponentDecks.length,
      wins: aggregateWins,
      losses: games * opponentDecks.length - aggregateWins,
      draws: 0,
      winRate:
        (100 * aggregateWins) /
        (games * opponentDecks.length),
    },
    byOpponent: opponentDecks.map(([deck, name]) => ({
      deck,
      name,
      fitness,
    })),
    top: [
      {
        cards: [
          { id: 0, name: "Forest", count: 20 },
          { id: 2, name: "Grizzly Bears", count: 20 },
        ],
        fitness: {
          games: games * opponentDecks.length,
          wins: aggregateWins,
          losses: games * opponentDecks.length - aggregateWins,
          draws: 0,
          winRate:
            (100 * aggregateWins) /
            (games * opponentDecks.length),
        },
      },
      {
        cards: [
          { id: 0, name: "Forest", count: 19 },
          { id: 2, name: "Grizzly Bears", count: 21 },
        ],
        fitness: {
          games: games * opponentDecks.length,
          wins: aggregateWins - 1,
          losses:
            games * opponentDecks.length - aggregateWins + 1,
          draws: 0,
          winRate:
            (100 * (aggregateWins - 1)) /
            (games * opponentDecks.length),
        },
      },
    ],
    generationBestWinRates: Array.from(
      { length: generations },
      (_, index) => 50 + index,
    ),
  });
  process.exit(0);
}

writeJson({
  type: "decision",
  state: {
    turn: 1,
    received: {
      humanDeck: valueAfter("--human-deck"),
      opponentDeck: valueAfter("--opponent-deck"),
      humanDeckCards: valueAfter("--human-deck-cards"),
      opponentDeckCards: valueAfter("--opponent-deck-cards"),
    },
    players: [
      { hand: ["Forest"], life: 20 },
      { handSize: 7, life: 20 },
    ],
  },
  decision: {
    id: "priority-1",
    kind: "priority",
    options: [{ index: 0, label: "Pass", kind: "pass" }],
  },
});

readline.createInterface({
  input: process.stdin,
  crlfDelay: Infinity,
});
