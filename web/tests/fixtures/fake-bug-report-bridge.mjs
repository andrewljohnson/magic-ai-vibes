#!/usr/bin/env node

import readline from "node:readline";

const publicIsland = {
  id: "card-island",
  name: "Island",
  type: "land",
};
const publicFlyingMen = {
  id: "card-flying-men",
  name: "Flying Men",
  type: "creature",
  costLabel: "U",
  power: 1,
  toughness: 1,
  flying: true,
};
const publicCounterspell = {
  id: "card-counterspell",
  name: "Counterspell",
  type: "instant",
  costLabel: "UU",
};

const hiddenHandCard = {
  id: "hidden-hand",
  name: "OPPONENT_HAND_SECRET",
};
const hiddenLibraryCard = {
  id: "hidden-library",
  name: "OPPONENT_LIBRARY_SECRET",
};

function player({
  human,
  life,
  handSize,
  librarySize,
  lands,
  creatures,
}) {
  return {
    life,
    handSize,
    librarySize,
    graveyard: [{ name: `${human ? "HUMAN" : "OPPONENT"}_GRAVEYARD_SECRET` }],
    exile: [{ name: `${human ? "HUMAN" : "OPPONENT"}_EXILE_SECRET` }],
    lands,
    creatures,
    artifacts: [],
    enchantments: [],
    manaPool: { blue: human ? 3 : 2, colorless: 0 },
    landPlayedThisTurn: true,
    extraTurns: 0,
    ...(human
      ? {
          hand: [publicCounterspell, publicFlyingMen],
        }
      : {
          hand: [hiddenHandCard],
          revealedHand: [hiddenHandCard],
          library: [hiddenLibraryCard],
        }),
  };
}

function permanent(permanentId, card, tapped = false) {
  return {
    permanentId,
    card,
    tapped,
    summoningSick: false,
    damage: 0,
    ...(card.type === "creature"
      ? { power: card.power, toughness: card.toughness }
      : {}),
  };
}

function state(turnNumber, phase) {
  return {
    turnNumber,
    activePlayer: 0,
    startingPlayer: 1,
    phase,
    observer: 0,
    rawStateSecret: "RAW_STATE_SECRET",
    opponentHiddenHand: [hiddenHandCard],
    players: [
      player({
        human: true,
        life: 17,
        handSize: 2,
        librarySize: 24,
        lands: [
          permanent(101, publicIsland, true),
          permanent(102, publicIsland),
        ],
        creatures: [permanent(110, publicFlyingMen)],
      }),
      player({
        human: false,
        life: 14,
        handSize: 6,
        librarySize: 23,
        lands: [permanent(201, publicIsland, true)],
        creatures: [permanent(210, publicFlyingMen)],
      }),
    ],
    stack: [
      {
        stackId: 301,
        kind: "spell",
        controller: 0,
        card: publicFlyingMen,
        xValue: 0,
        label: "Your Flying Men",
      },
      {
        stackId: 302,
        kind: "spell",
        controller: 1,
        card: publicCounterspell,
        xValue: 0,
        label: "Opponent's Counterspell → Flying Men",
        spellTarget: 301,
        secret: "STACK_SECRET",
      },
    ],
  };
}

function write(value) {
  process.stdout.write(`${JSON.stringify(value)}\n`);
}

write({
  type: "status",
  message: "Loaded report fixture",
  model: {
    family: "learned-value",
    generation: 16,
    searchWorlds: 8,
    horizonTurns: 4,
    source: "frozen-artifact+aq19-bilinear",
    fingerprint:
      "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f",
    treatment: {
      id: "aq19-bilinear",
      parameterSha256:
        "3114c898085375b7c39a8d8a7add5b0ab87dc70916d676deccd28d45e0942194",
      artifactFileSha256:
        "445f93435aebafbafc16cda4d1faa9e4d56dc12a25196f79c1334fcc84d22c1a",
      artifactBytes: 14_502,
    },
  },
});
write({
  type: "event",
  state: state(13, "first_main"),
  event: {
    kind: "priority_action",
    actionKind: "cast_counterspell",
    player: 1,
    phase: "first_main",
    label: "Opponent: Cast Counterspell → Flying Men",
    incidentalCards: [{ name: "INCIDENTAL_EVENT_SECRET" }],
  },
});
write({
  type: "decision",
  state: state(13, "first_main"),
  decision: {
    id: "report-priority-1",
    kind: "priority",
    phase: "first_main",
    secret: "DECISION_SECRET",
    options: [
      { index: 0, label: "Pass priority", kind: "pass" },
      {
        index: 1,
        label: "Cast Counterspell → stack #302",
        kind: "cast_counterspell",
        card: publicCounterspell,
        spellTarget: 302,
        privateCard: hiddenHandCard,
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
  const action = JSON.parse(line);
  if (
    step === 0 &&
    action.decisionId === "report-priority-1" &&
    action.index === 1
  ) {
    step = 1;
    write({
      type: "event",
      state: state(13, "declare_attackers"),
      event: {
        kind: "priority_action",
        actionKind: "cast_counterspell",
        player: 0,
        phase: "first_main",
        label: "You: Cast Counterspell → stack #302",
      },
    });
    write({
      type: "decision",
      state: state(13, "declare_attackers"),
      decision: {
        id: 2,
        kind: "attackers",
        eligible: [110],
        secret: "DECISION_SECRET_AFTER_ACTION",
      },
    });
    return;
  }
  if (
    step === 1 &&
    String(action.decisionId) === "2" &&
    JSON.stringify(action.ids) === JSON.stringify([110])
  ) {
    step = 2;
    write({
      type: "event",
      state: state(13, "declare_blockers"),
      event: {
        kind: "attackers_declared",
        player: 0,
        label: "You attacked with Flying Men #110",
      },
    });
    write({
      type: "decision",
      state: state(13, "declare_blockers"),
      decision: {
        id: "report-blockers-3",
        kind: "blockers",
        attackers: [110],
        choices: [{ blocker: 210, legalAttackers: [110] }],
        hiddenHand: [hiddenHandCard],
      },
    });
    return;
  }
  if (
    step === 2 &&
    action.decisionId === "report-blockers-3" &&
    JSON.stringify(action.pairs) === JSON.stringify([[110, 210]])
  ) {
    step = 3;
    write({
      type: "decision",
      state: state(13, "combat_damage"),
      decision: {
        id: "report-damage-order-4",
        kind: "damage_order",
        attacker: 110,
        blockers: [210],
        hiddenHand: [hiddenHandCard],
      },
    });
    return;
  }
  if (
    step === 3 &&
    action.decisionId === "report-damage-order-4" &&
    JSON.stringify(action.ids) === JSON.stringify([210])
  ) {
    step = 4;
    write({
      type: "decision",
      state: state(13, "cleanup"),
      decision: {
        id: "report-cleanup-5",
        kind: "cleanup_discard",
        count: 1,
        options: [
          { index: 0, card: publicCounterspell },
          { index: 1, card: publicFlyingMen },
        ],
        hiddenHand: [hiddenHandCard],
      },
    });
    return;
  }
  if (
    step === 4 &&
    action.decisionId === "report-cleanup-5" &&
    JSON.stringify(action.indices) === JSON.stringify([1])
  ) {
    step = 5;
    write({
      type: "game_over",
      state: state(13, "cleanup"),
      result: {
        winner: 0,
        reason: "fixture_complete",
        turns: 13,
      },
    });
    input.close();
    return;
  }
  process.stderr.write("BRIDGE_STDERR_SECRET\n");
  process.exitCode = 3;
  input.close();
  process.stdin.destroy();
});
