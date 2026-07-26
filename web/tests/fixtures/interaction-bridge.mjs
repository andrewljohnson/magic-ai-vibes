#!/usr/bin/env node

import readline from "node:readline";

function manaCost({ generic = 0, green = 0, red = 0, white = 0 } = {}) {
  return { generic, green, red, blue: 0, white };
}

const plains = {
  id: "card-21",
  name: "Plains",
  type: "land",
  cost: manaCost(),
  costLabel: "",
  power: 0,
  toughness: 0,
  flying: false,
};
const moat = {
  id: "card-22",
  name: "Moat",
  type: "enchantment",
  cost: manaCost({ generic: 2, white: 2 }),
  costLabel: "2WW",
  power: 0,
  toughness: 0,
  flying: false,
};
const forest = {
  id: "card-0",
  name: "Forest",
  type: "land",
  cost: manaCost(),
  costLabel: "",
  power: 0,
  toughness: 0,
  flying: false,
};
const grizzlyBears = {
  id: "card-2",
  name: "Grizzly Bears",
  type: "creature",
  cost: manaCost({ generic: 1, green: 1 }),
  costLabel: "1G",
  power: 2,
  toughness: 2,
  flying: false,
};
const ironclawOrcs = {
  id: "card-14",
  name: "Ironclaw Orcs",
  type: "creature",
  cost: manaCost({ generic: 1, red: 1 }),
  costLabel: "1R",
  power: 2,
  toughness: 2,
  flying: false,
};
const hillGiant = {
  id: "card-9",
  name: "Hill Giant",
  type: "creature",
  cost: manaCost({ generic: 3, red: 1 }),
  costLabel: "3R",
  power: 3,
  toughness: 3,
  flying: false,
};

const openingHand = [
  plains,
  plains,
  moat,
  forest,
  grizzlyBears,
  forest,
  moat,
  forest,
  grizzlyBears,
];
const keptHand = openingHand.slice(2);
const humanBlockers = [
  {
    permanentId: 110,
    card: grizzlyBears,
    tapped: false,
    summoningSick: false,
    damage: 0,
    power: 2,
    toughness: 2,
  },
  {
    permanentId: 111,
    card: grizzlyBears,
    tapped: false,
    summoningSick: false,
    damage: 0,
    power: 2,
    toughness: 2,
  },
];
const attackers = [
  {
    permanentId: 210,
    card: ironclawOrcs,
    tapped: true,
    summoningSick: false,
    damage: 0,
    power: 2,
    toughness: 2,
  },
  {
    permanentId: 211,
    card: hillGiant,
    tapped: true,
    summoningSick: false,
    damage: 0,
    power: 3,
    toughness: 3,
  },
];

function playerState(hand = openingHand) {
  return {
    life: 20,
    librarySize: 24,
    handSize: hand.length,
    hand,
    graveyard: hand === openingHand ? [] : [plains, plains],
    exile: [],
    lands: [
      {
        permanentId: 101,
        card: plains,
        tapped: true,
      },
      {
        permanentId: 102,
        card: forest,
        tapped: true,
      },
    ],
    creatures: humanBlockers,
    artifacts: [],
    enchantments: [],
    manaPool: manaCost(),
    extraTurns: 0,
  };
}

function opponentState() {
  return {
    life: 20,
    librarySize: 25,
    handSize: 5,
    graveyard: [],
    exile: [],
    lands: [],
    creatures: attackers,
    artifacts: [],
    enchantments: [],
    manaPool: manaCost(),
    extraTurns: 0,
  };
}

function state(phase, hand = openingHand) {
  return {
    turnNumber: 6,
    activePlayer: 1,
    startingPlayer: 0,
    phase,
    observer: 0,
    players: [playerState(hand), opponentState()],
    stack: [],
  };
}

function write(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

write({ type: "status", message: "Loading interaction fixture" });
write({
  type: "decision",
  state: state("cleanup"),
  decision: {
    id: "interaction-cleanup-1",
    kind: "cleanup_discard",
    count: 2,
    options: openingHand.map((card, index) => ({ index, card })),
  },
});

const input = readline.createInterface({
  input: process.stdin,
  crlfDelay: Infinity,
});
let step = 0;

input.on("line", (line) => {
  let action;
  try {
    action = JSON.parse(line);
  } catch {
    process.stderr.write("invalid interaction-fixture action\n");
    process.exitCode = 2;
    input.close();
    return;
  }

  if (
    step === 0 &&
    action.decisionId === "interaction-cleanup-1" &&
    Array.isArray(action.indices) &&
    JSON.stringify(action.indices) === JSON.stringify([0, 1])
  ) {
    step = 1;
    write({
      type: "event",
      state: state("declare_blockers", keptHand),
      event: {
        kind: "cards_discarded",
        player: 0,
        phase: "cleanup",
        cards: [plains, plains],
        label: "You discarded Plains, Plains",
      },
    });
    write({
      type: "decision",
      state: state("declare_blockers", keptHand),
      decision: {
        id: 7002,
        kind: "blockers",
        attackers: [210, 211],
        choices: [
          { blocker: 110, legalAttackers: [210] },
          { blocker: 111, legalAttackers: [210, 211] },
        ],
      },
    });
    return;
  }

  if (
    step === 1 &&
    String(action.decisionId) === "7002" &&
    Array.isArray(action.pairs)
  ) {
    step = 2;
    write({
      type: "event",
      state: state("combat_damage", keptHand),
      event: {
        kind: "blockers_declared",
        player: 0,
        phase: "declare_blockers",
        pairs: action.pairs,
        label:
          action.pairs.length === 0
            ? "You declared no blockers"
            : `You declared ${action.pairs.length} blockers`,
      },
    });
    write({
      type: "game_over",
      state: state("combat_damage", keptHand),
      result: { winner: 0, reason: "fixture_complete", turns: 6 },
    });
    input.close();
    return;
  }

  process.stderr.write(`unexpected interaction-fixture action: ${line}\n`);
  process.exitCode = 3;
  input.close();
});
