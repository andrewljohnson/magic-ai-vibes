#!/usr/bin/env node

import readline from "node:readline";

const args = process.argv.slice(2);
const valueAfter = (flag) => {
  const index = args.indexOf(flag);
  return index < 0 ? null : args[index + 1];
};

const write = (message) => {
  process.stdout.write(`${JSON.stringify(message)}\n`);
};

write({ type: "status", message: "Shuffling libraries" });
const opponentPolicy = valueAfter("--opponent-policy");
const learnedGenerations = valueAfter("--learned-generations");
if (opponentPolicy?.startsWith("learned-")) {
  const generation = Number(learnedGenerations ?? "0");
  write({
    type: "status",
    message: `Fake learned model generation ${generation} ready`,
    model: {
      family:
        opponentPolicy === "learned-actor"
          ? "learned-actor"
          : "learned-value",
      generation,
      searchWorlds: Number(valueAfter("--learned-rollouts") ?? "1"),
      horizonTurns: 4,
      source: generation === 16 ? "frozen-artifact" : "trained-for-match",
      fingerprint:
        generation === 16
          ? "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f"
          : "0000000000000000000000000000000000000000000000000000000000000000",
    },
  });
}
write({
  type: "decision",
  state: {
    turn: 1,
    received: {
      humanDeck: valueAfter("--human-deck"),
      opponentDeck: valueAfter("--opponent-deck"),
      opponentPolicy,
      seed: valueAfter("--seed"),
      trainGames: valueAfter("--train-games"),
      trainSeed: valueAfter("--train-seed"),
      rollouts: valueAfter("--rollouts"),
      deepRollouts: valueAfter("--deep-rollouts"),
      learnedRollouts: valueAfter("--learned-rollouts"),
      learnedGenerations,
      debugReveal: args.includes("--debug-reveal"),
      bluffMode: args.includes("--bluff-mode"),
    },
    players: [
      { hand: ["Flying Man"], life: 20 },
      { handSize: 7, life: 20 },
    ],
  },
  decision: {
    id: "priority-1",
    kind: "priority",
    phase: "first_main",
    options: [
      { index: 0, label: "Pass", kind: "pass" },
      { index: 1, label: "Cast Flying Man", kind: "cast" },
    ],
  },
});

let step = 0;
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
    step === 0 &&
    action.decisionId === "priority-1" &&
    action.index === 1
  ) {
    step = 1;
    write({
      type: "event",
      state: {
        turn: 1,
        players: [
          { hand: [], battlefield: ["Flying Man"], life: 20 },
          { handSize: 7, life: 20 },
        ],
      },
      event: { kind: "spell_resolved", label: "Flying Man resolved" },
    });
    write({
      type: "decision",
      state: {
        turn: 2,
        players: [
          { hand: [], battlefield: ["Flying Man"], life: 20 },
          { handSize: 7, life: 20 },
        ],
      },
      decision: {
        id: 2,
        kind: "attackers",
        eligible: [11, 12],
      },
    });
    return;
  }

  if (
    step === 1 &&
    String(action.decisionId) === "2" &&
    Array.isArray(action.ids) &&
    action.ids.length === 1 &&
    action.ids[0] === 11
  ) {
    step = 2;
    write({
      type: "decision",
      state: {
        turn: 2,
        players: [
          { hand: [], battlefield: ["Flying Man", "Hill Giant"], life: 20 },
          { handSize: 7, battlefield: ["Grizzly Bears"], life: 18 },
        ],
      },
      decision: {
        id: "blockers-3",
        kind: "blockers",
        attackers: [21],
        choices: [
          { blocker: 31, legalAttackers: [21] },
          { blocker: 32, legalAttackers: [21] },
        ],
      },
    });
    return;
  }

  if (
    step === 2 &&
    action.decisionId === "blockers-3" &&
    Array.isArray(action.pairs) &&
    JSON.stringify(action.pairs) === JSON.stringify([[21, 31]])
  ) {
    step = 3;
    write({
      type: "decision",
      state: {
        turn: 2,
        players: [
          { hand: [], battlefield: ["Flying Man", "Hill Giant"], life: 20 },
          { handSize: 7, battlefield: ["Grizzly Bears"], life: 18 },
        ],
      },
      decision: {
        id: 4,
        kind: "damage_order",
        attacker: 21,
        blockers: [31, 32],
      },
    });
    return;
  }

  if (
    step === 3 &&
    String(action.decisionId) === "4" &&
    Array.isArray(action.ids) &&
    JSON.stringify(action.ids) === JSON.stringify([32, 31])
  ) {
    step = 4;
    write({
      type: "game_over",
      state: {
        turn: 3,
        players: [
          { hand: [], battlefield: ["Flying Man"], life: 20 },
          { handSize: 7, life: 0 },
        ],
      },
      result: { winner: 0, reason: "life", turns: 3 },
    });
    input.close();
    return;
  }

  process.stderr.write(`unexpected test action: ${line}\n`);
  process.exitCode = 3;
  input.close();
});
