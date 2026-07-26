#!/usr/bin/env node

import readline from "node:readline";

function manaCost({ generic = 0, green = 0, red = 0 } = {}) {
  return { generic, green, red, blue: 0, white: 0 };
}

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
const giantGrowth = {
  id: "card-3",
  name: "Giant Growth",
  type: "instant",
  cost: manaCost({ green: 1 }),
  costLabel: "G",
  power: 0,
  toughness: 0,
  flying: false,
};
const mountain = {
  id: "card-1",
  name: "Mountain",
  type: "land",
  cost: manaCost(),
  costLabel: "",
  power: 0,
  toughness: 0,
  flying: false,
};
const lightningBolt = {
  id: "card-5",
  name: "Lightning Bolt",
  type: "instant",
  cost: manaCost({ red: 1 }),
  costLabel: "R",
  power: 0,
  toughness: 0,
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

const humanBear = {
  permanentId: 110,
  card: grizzlyBears,
  tapped: false,
  summoningSick: false,
  damage: 0,
  power: 2,
  toughness: 2,
};
const opponentOrcs = {
  permanentId: 210,
  card: ironclawOrcs,
  tapped: false,
  summoningSick: false,
  damage: 0,
  power: 2,
  toughness: 2,
};
const firstForest = {
  permanentId: 101,
  card: forest,
  tapped: true,
};
const secondForest = {
  permanentId: 102,
  card: forest,
  tapped: false,
};
const opponentMountain = {
  permanentId: 201,
  card: mountain,
  tapped: true,
};
const openingHand = [
  giantGrowth,
  forest,
  grizzlyBears,
  forest,
  grizzlyBears,
  giantGrowth,
  forest,
];

const bottomGrowth = {
  stackId: 301,
  kind: "spell",
  controller: 0,
  card: giantGrowth,
  xValue: 0,
  label: "Your Giant Growth → Ironclaw Orcs #210",
  target: {
    player: 1,
    creature: 210,
    label: "Ironclaw Orcs #210",
  },
};
const topBolt = {
  stackId: 302,
  kind: "spell",
  controller: 1,
  card: lightningBolt,
  xValue: 0,
  label: "Opponent's Lightning Bolt → Grizzly Bears #110",
  target: {
    player: 0,
    creature: 110,
    label: "Grizzly Bears #110",
  },
};
const responseGrowth = {
  stackId: 303,
  kind: "spell",
  controller: 0,
  card: giantGrowth,
  xValue: 0,
  label: "Your Giant Growth → Grizzly Bears #110",
  target: {
    player: 0,
    creature: 110,
    label: "Grizzly Bears #110",
  },
};

function playerState({
  hand = openingHand,
  forests = [firstForest, secondForest],
} = {}) {
  return {
    life: 17,
    librarySize: 28,
    handSize: hand.length,
    graveyard: [],
    exile: [],
    lands: forests,
    creatures: [humanBear],
    artifacts: [],
    enchantments: [],
    manaPool: manaCost(),
    landPlayedThisTurn: true,
    extraTurns: 0,
    hand,
  };
}

function opponentState() {
  return {
    life: 20,
    librarySize: 28,
    handSize: 5,
    graveyard: [],
    exile: [],
    lands: [opponentMountain],
    creatures: [opponentOrcs],
    artifacts: [],
    enchantments: [],
    manaPool: manaCost(),
    landPlayedThisTurn: true,
    extraTurns: 0,
  };
}

function state({
  stack = [bottomGrowth, topBolt],
  human = playerState(),
} = {}) {
  return {
    turnNumber: 4,
    activePlayer: 0,
    startingPlayer: 0,
    phase: "first_main",
    observer: 0,
    players: [human, opponentState()],
    stack,
  };
}

function write(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

function priorityDecision(id, options) {
  write({
    type: "decision",
    state: options.state,
    decision: {
      id,
      kind: "priority",
      phase: "first_main",
      options: options.choices,
    },
  });
}

const initialState = state();
write({ type: "status", message: "Loading targeted-stack fixture" });
write({
  type: "event",
  state: initialState,
  event: {
    kind: "priority_action",
    player: 1,
    phase: "first_main",
    label: "Opponent: Cast Lightning Bolt → Grizzly Bears #110",
  },
});
priorityDecision("target-stack-priority-1", {
  state: initialState,
  choices: [
    { index: 0, label: "Pass priority", kind: "pass" },
    {
      index: 1,
      label: "Cast Giant Growth → Grizzly Bears #110",
      kind: "cast_giant_growth",
      card: giantGrowth,
      target: {
        player: 0,
        creature: 110,
        label: "Grizzly Bears #110",
      },
    },
  ],
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
    process.stderr.write("invalid targeted-stack action\n");
    process.exitCode = 2;
    input.close();
    return;
  }

  if (
    step === 0 &&
    action.decisionId === "target-stack-priority-1" &&
    action.index === 1
  ) {
    step = 1;
    const responseState = state({
      stack: [bottomGrowth, topBolt, responseGrowth],
      human: playerState({
        hand: openingHand.slice(1),
        forests: [
          firstForest,
          { ...secondForest, tapped: true },
        ],
      }),
    });
    write({
      type: "event",
      state: responseState,
      event: {
        kind: "priority_action",
        player: 0,
        phase: "first_main",
        label: "You: Cast Giant Growth → Grizzly Bears #110",
      },
    });
    priorityDecision("target-stack-priority-2", {
      state: responseState,
      choices: [{ index: 0, label: "Pass priority", kind: "pass" }],
    });
    return;
  }

  if (
    step === 1 &&
    action.decisionId === "target-stack-priority-2" &&
    action.index === 0
  ) {
    step = 2;
    const resolvedState = state({
      human: playerState({
        hand: openingHand.slice(1),
        forests: [
          firstForest,
          { ...secondForest, tapped: true },
        ],
      }),
    });
    write({
      type: "event",
      state: resolvedState,
      event: {
        kind: "stack_resolved",
        player: 0,
        phase: "first_main",
        label: "Resolved your Giant Growth → Grizzly Bears #110",
      },
    });
    priorityDecision("target-stack-priority-3", {
      state: resolvedState,
      choices: [{ index: 0, label: "Pass priority", kind: "pass" }],
    });
    return;
  }

  process.stderr.write(`unexpected targeted-stack action: ${line}\n`);
  process.exitCode = 3;
  input.close();
});
