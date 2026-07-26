#!/usr/bin/env node

import readline from "node:readline";

const args = process.argv.slice(2);
const valueAfter = (flag) => {
  const index = args.indexOf(flag);
  return index < 0 ? null : args[index + 1];
};
const requestedDelay = Number(valueAfter("--fixture-delay-ms") ?? 0);
const fixtureDelayMs =
  Number.isSafeInteger(requestedDelay) && requestedDelay >= 0
    ? requestedDelay
    : 0;

function manaCost({ generic = 0, green = 0 } = {}) {
  return { generic, green, red: 0, blue: 0, white: 0 };
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
const ironrootTreefolk = {
  id: "card-4",
  name: "Ironroot Treefolk",
  type: "creature",
  cost: manaCost({ generic: 4, green: 1 }),
  costLabel: "4G",
  power: 5,
  toughness: 4,
  flying: false,
};

const openingHand = [
  forest,
  grizzlyBears,
  giantGrowth,
  grizzlyBears,
  giantGrowth,
  ironrootTreefolk,
  forest,
];
const postLandHand = openingHand.slice(1);
const postCastHand = openingHand.slice(2);
const revealedOpponentHand = [grizzlyBears, giantGrowth];
const debugReveal = args.includes("--debug-reveal");

const oldForestOne = {
  permanentId: 90,
  card: forest,
  tapped: false,
};
const oldForestTwo = {
  permanentId: 91,
  card: forest,
  tapped: false,
};
const playedForest = {
  permanentId: 100,
  card: forest,
  tapped: false,
};
const newBear = {
  permanentId: 101,
  card: grizzlyBears,
  tapped: false,
  summoningSick: false,
  damage: 0,
  power: 2,
  toughness: 2,
};
const oldBear = {
  permanentId: 102,
  card: grizzlyBears,
  tapped: false,
  summoningSick: false,
  damage: 0,
  power: 2,
  toughness: 2,
};
const firstOpponentBear = {
  permanentId: 201,
  card: grizzlyBears,
  tapped: false,
  summoningSick: false,
  damage: 0,
  power: 2,
  toughness: 2,
};
const secondOpponentBear = {
  permanentId: 202,
  card: grizzlyBears,
  tapped: false,
  summoningSick: false,
  damage: 0,
  power: 2,
  toughness: 2,
};

function humanPlayer({
  hand = openingHand,
  lands = [oldForestOne, oldForestTwo],
  creatures = [oldBear],
  life = 20,
  landPlayedThisTurn = false,
} = {}) {
  return {
    life,
    librarySize: 29,
    handSize: hand.length,
    graveyard: [],
    exile: [],
    lands,
    creatures,
    artifacts: [],
    enchantments: [],
    manaPool: manaCost(),
    landPlayedThisTurn,
    extraTurns: 0,
    hand,
  };
}

function opponentPlayer({
  life = 20,
  librarySize = 29,
  creatures = [firstOpponentBear, secondOpponentBear],
} = {}) {
  return {
    life,
    librarySize,
    handSize: 7,
    graveyard: [],
    exile: [],
    lands: [],
    creatures,
    artifacts: [],
    enchantments: [],
    manaPool: manaCost(),
    landPlayedThisTurn: false,
    extraTurns: 0,
    ...(debugReveal ? { revealedHand: revealedOpponentHand } : {}),
  };
}

function state({
  turnNumber = 3,
  activePlayer = 0,
  phase = "first_main",
  human = humanPlayer(),
  opponent = opponentPlayer(),
  stack = [],
} = {}) {
  return {
    turnNumber,
    activePlayer,
    startingPlayer: 0,
    phase,
    observer: 0,
    players: [human, opponent],
    stack,
  };
}

function event(kind, player, phase, label, eventState) {
  return {
    type: "event",
    state: eventState,
    event: { kind, player, phase, label },
  };
}

function write(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

function fail(line) {
  process.stderr.write(`unexpected journey action: ${line}\n`);
  process.exitCode = 3;
  input.close();
  process.stdin.destroy();
}

const initialState = state();
write({ type: "status", message: "Preparing the battlefield" });
write({
  type: "event",
  state: initialState,
  event: {
    kind: "turn_started",
    player: 0,
    phase: "first_main",
    label: "You started turn 3",
  },
});
write({
  type: "decision",
  state: initialState,
  decision: {
    id: 1,
    kind: "priority",
    phase: "first_main",
    options: [
      { index: 0, label: "Pass priority", kind: "pass" },
      {
        index: 1,
        label: "Play Forest",
        kind: "play_land",
        card: forest,
      },
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
    process.stderr.write("invalid journey action\n");
    process.exitCode = 2;
    input.close();
    return;
  }

  if (step === 0 && action.decisionId === 1 && action.index === 0) {
    step = 7;
    const settlePass = () => {
      write(
        event(
          "priority_action",
          0,
          "first_main",
          "You: Pass priority",
          initialState,
        ),
      );
      write({
        type: "decision",
        state: state({ phase: "declare_attackers" }),
        decision: {
          id: 7,
          kind: "attackers",
          eligible: [102],
        },
      });
    };
    if (fixtureDelayMs === 0) {
      settlePass();
    } else {
      setTimeout(settlePass, fixtureDelayMs);
    }
    return;
  }

  if (step === 0 && action.decisionId === 1 && action.index === 1) {
    step = 1;
    const afterLand = state({
      human: humanPlayer({
        hand: postLandHand,
        lands: [oldForestOne, oldForestTwo, playedForest],
        landPlayedThisTurn: true,
      }),
    });
    write(
      event(
        "priority_action",
        0,
        "first_main",
        "You: Play Forest",
        afterLand,
      ),
    );
    write({
      type: "decision",
      state: afterLand,
      decision: {
        id: 2,
        kind: "priority",
        phase: "first_main",
        options: [
          { index: 0, label: "Pass priority", kind: "pass" },
          {
            index: 1,
            label: "Cast Grizzly Bears",
            kind: "cast_creature",
            card: grizzlyBears,
          },
        ],
      },
    });
    return;
  }

  if (step === 1 && action.decisionId === 2 && action.index === 1) {
    step = 3;
    const spellState = state({
      human: humanPlayer({
        hand: postCastHand,
        lands: [
          { ...oldForestOne, tapped: true },
          { ...oldForestTwo, tapped: true },
          playedForest,
        ],
        landPlayedThisTurn: true,
      }),
      stack: [
        {
          stackId: 1,
          kind: "spell",
          controller: 0,
          card: grizzlyBears,
          xValue: 0,
          label: "Your Grizzly Bears",
        },
      ],
    });
    write(
      event(
        "priority_action",
        0,
        "first_main",
        "You: Cast Grizzly Bears",
        spellState,
      ),
    );
    write(
      event(
        "priority_action",
        0,
        "first_main",
        "You: Pass priority",
        spellState,
      ),
    );
    const resolvedState = state({
      human: humanPlayer({
        hand: postCastHand,
        lands: [
          { ...oldForestOne, tapped: true },
          { ...oldForestTwo, tapped: true },
          playedForest,
        ],
        creatures: [{ ...newBear, summoningSick: true }, oldBear],
        landPlayedThisTurn: true,
      }),
    });
    write(
      event(
        "stack_resolved",
        0,
        "first_main",
        "Resolved your Grizzly Bears",
        resolvedState,
      ),
    );
    const turnStartState = state({
      turnNumber: 4,
      human: humanPlayer({
        hand: postCastHand,
        lands: [oldForestOne, oldForestTwo, playedForest],
        creatures: [newBear, oldBear],
      }),
    });
    write(
      event(
        "turn_started",
        0,
        "first_main",
        "You started turn 4",
        turnStartState,
      ),
    );
    const attackState = state({
      turnNumber: 4,
      phase: "declare_attackers",
      human: humanPlayer({
        hand: postCastHand,
        lands: [oldForestOne, oldForestTwo, playedForest],
        creatures: [newBear, oldBear],
      }),
    });
    write({
      type: "decision",
      state: attackState,
      decision: {
        id: 4,
        kind: "attackers",
        eligible: [101, 102],
      },
    });
    return;
  }

  if (
    step === 3 &&
    action.decisionId === 4 &&
    Array.isArray(action.ids) &&
    JSON.stringify(action.ids) === JSON.stringify([101])
  ) {
    step = 4;
    const declaredState = state({
      turnNumber: 4,
      phase: "declare_attackers",
      human: humanPlayer({
        hand: postCastHand,
        lands: [oldForestOne, oldForestTwo, playedForest],
        creatures: [{ ...newBear, tapped: true }, oldBear],
      }),
    });
    write(
      event(
        "attackers_declared",
        0,
        "declare_attackers",
        "You declared 1 attacker(s)",
        declaredState,
      ),
    );
    const blockedState = state({
      turnNumber: 4,
      phase: "declare_blockers",
      human: humanPlayer({
        hand: postCastHand,
        lands: [oldForestOne, oldForestTwo, playedForest],
        creatures: [{ ...newBear, tapped: true }, oldBear],
      }),
    });
    write(
      event(
        "blockers_declared",
        1,
        "declare_blockers",
        "Opponent declared 2 block(s)",
        blockedState,
      ),
    );
    const damageState = state({
      turnNumber: 4,
      phase: "damage_order",
      human: humanPlayer({
        hand: postCastHand,
        lands: [oldForestOne, oldForestTwo, playedForest],
        creatures: [{ ...newBear, tapped: true }, oldBear],
      }),
    });
    write({
      type: "decision",
      state: damageState,
      decision: {
        id: 5,
        kind: "damage_order",
        attacker: 101,
        blockers: [201, 202],
      },
    });
    return;
  }

  if (
    step === 4 &&
    action.decisionId === 5 &&
    Array.isArray(action.ids) &&
    JSON.stringify(action.ids) === JSON.stringify([202, 201])
  ) {
    step = 5;
    const orderedState = state({
      turnNumber: 4,
      phase: "damage_order",
      human: humanPlayer({
        hand: postCastHand,
        lands: [oldForestOne, oldForestTwo, playedForest],
        creatures: [oldBear],
      }),
      opponent: opponentPlayer({
        creatures: [firstOpponentBear],
      }),
    });
    write(
      event(
        "damage_order",
        0,
        "damage_order",
        "You ordered combat damage",
        orderedState,
      ),
    );
    const resolvedCombatState = state({
      turnNumber: 4,
      phase: "end_combat",
      human: humanPlayer({
        hand: postCastHand,
        lands: [oldForestOne, oldForestTwo, playedForest],
        creatures: [oldBear],
      }),
      opponent: opponentPlayer({
        creatures: [firstOpponentBear],
      }),
    });
    write(
      event(
        "combat_resolved",
        0,
        "end_combat",
        "Combat damage resolved",
        resolvedCombatState,
      ),
    );
    const opponentAttackState = state({
      turnNumber: 5,
      activePlayer: 1,
      phase: "declare_attackers",
      human: humanPlayer({
        hand: postCastHand,
        lands: [oldForestOne, oldForestTwo, playedForest],
        creatures: [oldBear],
      }),
      opponent: opponentPlayer({
        creatures: [{ ...firstOpponentBear, tapped: true }],
      }),
    });
    write(
      event(
        "attackers_declared",
        1,
        "declare_attackers",
        "Opponent declared 1 attacker(s)",
        opponentAttackState,
      ),
    );
    const blockerState = state({
      turnNumber: 5,
      activePlayer: 1,
      phase: "declare_blockers",
      human: humanPlayer({
        hand: postCastHand,
        lands: [oldForestOne, oldForestTwo, playedForest],
        creatures: [oldBear],
      }),
      opponent: opponentPlayer({
        creatures: [{ ...firstOpponentBear, tapped: true }],
      }),
    });
    write({
      type: "decision",
      state: blockerState,
      decision: {
        id: 6,
        kind: "blockers",
        attackers: [201],
        choices: [{ blocker: 102, legalAttackers: [201] }],
      },
    });
    return;
  }

  if (
    step === 5 &&
    action.decisionId === 6 &&
    Array.isArray(action.pairs) &&
    JSON.stringify(action.pairs) === JSON.stringify([[201, 102]])
  ) {
    step = 6;
    const blockedState = state({
      turnNumber: 5,
      activePlayer: 1,
      phase: "declare_blockers",
      human: humanPlayer({
        hand: postCastHand,
        lands: [oldForestOne, oldForestTwo, playedForest],
        creatures: [oldBear],
      }),
      opponent: opponentPlayer({
        librarySize: 0,
        creatures: [{ ...firstOpponentBear, tapped: true }],
      }),
    });
    write(
      event(
        "blockers_declared",
        0,
        "declare_blockers",
        "You declared 1 block(s)",
        blockedState,
      ),
    );
    const endingState = state({
      turnNumber: 5,
      activePlayer: 1,
      phase: "end_combat",
      human: humanPlayer({
        hand: postCastHand,
        lands: [oldForestOne, oldForestTwo, playedForest],
        creatures: [],
      }),
      opponent: opponentPlayer({
        librarySize: 0,
        creatures: [],
      }),
    });
    write(
      event(
        "combat_resolved",
        1,
        "end_combat",
        "Combat damage resolved",
        endingState,
      ),
    );
    write({
      type: "game_over",
      state: endingState,
      result: {
        winner: 0,
        reason: "empty_library",
        turns: 5,
        startingPlayer: 0,
        endingLife: [20, 20],
      },
    });
    input.close();
    return;
  }

  fail(line);
});
