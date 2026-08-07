import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

import { createGameContractHarness } from "../server.mjs";

const TEST_DIRECTORY = path.dirname(fileURLToPath(import.meta.url));
const JOURNEY_BRIDGE = path.join(
  TEST_DIRECTORY,
  "fixtures",
  "journey-bridge.mjs",
);

function makeHarness(t, options = {}) {
  let nextId = 0;
  const harness = createGameContractHarness({
    bridgePath: process.execPath,
    bridgeArgsPrefix: [JOURNEY_BRIDGE, ...(options.bridgeArgs ?? [])],
    initialTimeoutMs: 5_000,
    actionTimeoutMs: options.actionTimeoutMs ?? 5_000,
    idFactory: () => `journey-${++nextId}`,
  });
  t.after(() => harness.shutdown());
  return harness;
}

function assertCard(card) {
  assert.equal(typeof card, "object");
  assert.notEqual(card, null);
  assert.ok(["string", "number"].includes(typeof card.id));
  assert.equal(typeof card.name, "string");
  assert.notEqual(card.name, "");
}

function assertPlayerZones(player) {
  assert.equal(typeof player.life, "number");
  assert.equal(typeof player.librarySize, "number");
  assert.equal(typeof player.handSize, "number");
  for (const zone of [
    "graveyard",
    "exile",
    "lands",
    "creatures",
    "artifacts",
    "enchantments",
  ]) {
    assert.ok(Array.isArray(player[zone]), `${zone} must be an array`);
  }
}

function assertVisibleHumanHand(game, expectedSize) {
  const state = game.snapshot;
  assert.equal(typeof state.turnNumber, "number");
  assert.equal(typeof state.phase, "string");
  assert.ok(Array.isArray(state.stack));
  assert.equal(state.players.length, 2);

  const human = state.players[0];
  const opponent = state.players[1];
  assertPlayerZones(human);
  assertPlayerZones(opponent);
  assert.ok(Array.isArray(human.hand), "the human hand must be public to its UI");
  assert.equal(human.hand.length, expectedSize);
  assert.equal(human.handSize, expectedSize);
  human.hand.forEach(assertCard);

  assert.equal(
    Object.hasOwn(opponent, "hand"),
    false,
    "the normal contract must not expose the opponent hand",
  );
  assert.equal(
    Object.hasOwn(opponent, "revealedHand"),
    false,
    "debug-only opponent cards must stay absent by default",
  );
}

test("journey Pass priority settles and unsupported branches fail promptly", async (t) => {
  const harness = makeHarness(t, { actionTimeoutMs: 500 });
  const { game: opening } = await harness.create({
    players: [
      { deckId: "rg-berserk", policyId: "human" },
      { deckId: "rg-berserk", policyId: "handcrafted" },
    ],
    seed: 42,
    debugReveal: false,
  });

  const { game: passed } = await harness.action(opening.id, {
    decisionId: 1,
    index: 0,
  });
  assert.equal(passed.status, "awaiting_action");
  assert.equal(passed.decision.id, 7);
  assert.equal(passed.decision.kind, "attackers");
  assert.deepEqual(passed.decision.eligible, [102]);
  assert.equal(passed.snapshot.phase, "declare_attackers");
  assert.equal(passed.snapshot.players[0].handSize, 7);
  assert.equal(passed.events.at(-1).kind, "priority_action");
  assert.equal(passed.events.at(-1).actionKind, "pass");
  assert.equal(passed.events.at(-1).message, "You: Pass priority");

  const { game: failed } = await harness.action(passed.id, {
    decisionId: 7,
    ids: [],
  });
  assert.equal(failed.status, "failed");
  assert.equal(failed.decision, null);
  assert.equal(failed.error.code, "bridge_failed");
  assert.match(failed.error.message, /exited unexpectedly \(code 3\)/);
});

test("delayed journey mode has a real bounded in-flight interval", async (t) => {
  const delayMs = 250;
  const harness = makeHarness(t, {
    actionTimeoutMs: 2_000,
    bridgeArgs: ["--fixture-delay-ms", String(delayMs)],
  });
  const { game: opening } = await harness.create({
    players: [
      { deckId: "rg-berserk", policyId: "human" },
      { deckId: "rg-berserk", policyId: "handcrafted" },
    ],
    seed: 42,
    debugReveal: false,
  });

  const startedAt = performance.now();
  const { game: passed } = await harness.action(opening.id, {
    decisionId: 1,
    index: 0,
  });
  const elapsedMs = performance.now() - startedAt;

  assert.ok(
    elapsedMs >= delayMs - 25,
    `delayed fixture settled too early after ${elapsedMs.toFixed(1)} ms`,
  );
  assert.equal(passed.status, "awaiting_action");
  assert.equal(passed.snapshot.phase, "declare_attackers");
  assert.equal(passed.decision.kind, "attackers");
  assert.deepEqual(passed.decision.eligible, [102]);
});

async function runFullJourney(harness, config) {
  let { game } = await harness.create(config);
  assert.equal(game.status, "awaiting_action");
  assertVisibleHumanHand(game, 7);
  assert.equal(game.decision.kind, "priority");
  assert.equal(game.decision.id, 1);
  assert.ok(game.decision.options.some(({ kind }) => kind === "play_land"));
  assert.deepEqual(game.config, {
    players: config.players,
    seed: "42",
    debugReveal: false,
    bluffMode: false,
    rollouts: 2,
    deepRollouts: 8,
  });
  assert.equal(game.events.at(-1).kind, "turn_started");

  ({ game } = await harness.action(game.id, {
    decisionId: 1,
    index: 1,
  }));
  assert.equal(game.decision.id, 2);
  assert.equal(game.snapshot.players[0].lands.length, 3);
  assertVisibleHumanHand(game, 6);
  assert.equal(game.events.at(-1).kind, "priority_action");
  assert.match(game.events.at(-1).message, /Play Forest/);

  ({ game } = await harness.action(game.id, {
    decisionId: 2,
    index: 1,
  }));
  assert.equal(game.decision.id, 4);
  assert.equal(game.decision.kind, "attackers");
  assert.equal(game.snapshot.stack.length, 0);
  assert.equal(game.snapshot.players[0].creatures.length, 2);
  assert.deepEqual(game.decision.eligible, [101, 102]);
  assertVisibleHumanHand(game, 5);
  assert.ok(
    game.events.some(
      ({ kind, message }) =>
        kind === "priority_action" && /Cast Grizzly Bears/.test(message),
    ),
  );
  assert.ok(
    game.events.some(
      ({ kind, message }) =>
        kind === "priority_action" && /Pass priority/.test(message),
    ),
    "the forced pass remains observable as an event without becoming a decision",
  );
  assert.ok(game.events.some(({ kind }) => kind === "stack_resolved"));
  assert.equal(game.events.at(-1).kind, "turn_started");

  ({ game } = await harness.action(game.id, {
    decisionId: 4,
    ids: [101],
  }));
  assert.equal(game.decision.kind, "damage_order");
  assert.equal(game.decision.attacker, 101);
  assert.deepEqual(game.decision.blockers, [201, 202]);
  assert.deepEqual(
    game.snapshot.players[1].creatures.map(({ permanentId }) => permanentId),
    [201, 202],
    "every damage-order ID must map to a visible public permanent",
  );
  assert.equal(game.events.at(-2).kind, "attackers_declared");
  assert.equal(game.events.at(-1).kind, "blockers_declared");

  ({ game } = await harness.action(game.id, {
    decisionId: 5,
    ids: [202, 201],
  }));
  assert.equal(game.decision.kind, "blockers");
  assert.deepEqual(game.decision.attackers, [201]);
  assert.deepEqual(game.decision.choices, [
    { blocker: 102, legalAttackers: [201] },
  ]);
  assert.deepEqual(
    game.snapshot.players[1].creatures.map(({ permanentId }) => permanentId),
    [201],
    "damage removes the first ordered blocker and keeps the other one",
  );
  assert.ok(game.events.some(({ kind }) => kind === "damage_order"));
  assert.ok(game.events.some(({ kind }) => kind === "combat_resolved"));

  ({ game } = await harness.action(game.id, {
    decisionId: 6,
    pairs: [[201, 102]],
  }));
  assert.equal(game.status, "finished");
  assert.equal(game.decision, null);
  assert.deepEqual(game.snapshot.players[0].creatures, []);
  assert.deepEqual(game.snapshot.players[1].creatures, []);
  assert.equal(game.events.at(-2).kind, "blockers_declared");
  assert.equal(game.events.at(-1).kind, "combat_resolved");
  assert.deepEqual(game.result, {
    winner: 0,
    reason: "empty_library",
    turns: 5,
    startingPlayer: 0,
    endingLife: [20, 20],
  });
  return game;
}

test("deterministic UI contract covers every deck and opponent policy", async (t) => {
  const harness = makeHarness(t);
  const metadata = harness.metadata();
  assert.deepEqual(
    metadata.decks.map(({ id }) => id),
    [
      "rg-berserk",
      "atog",
      "br-midrange",
      "robots",
      "white-weenie",
      "uwr",
      "blue-skies",
      "the-deck",
    ],
  );
  assert.deepEqual(
    metadata.policies.map(({ id }) => id),
    ["random", "monte-carlo", "deep-monte-carlo", "handcrafted", "spz"],
  );

  for (const deck of metadata.decks) {
    for (const policy of metadata.policies) {
      await t.test(`${deck.id} vs ${policy.id}`, async () => {
        const liveIds = new Set();
        const config = {
          players: [
            { deckId: deck.id, policyId: "human" },
            { deckId: deck.id, policyId: policy.id },
          ],
          seed: 42,
          debugReveal: false,
        };

        try {
          const completed = await runFullJourney(harness, config);
          liveIds.add(completed.id);
          harness.delete(completed.id);
          liveIds.delete(completed.id);

          const { game: rematch } = await harness.create(config);
          liveIds.add(rematch.id);
          assert.notEqual(rematch.id, completed.id);
          assert.deepEqual(rematch.config, completed.config);
          assert.equal(rematch.config.seed, "42");
          assert.equal(rematch.status, "awaiting_action");
          assert.equal(rematch.decision.id, 1);
          assertVisibleHumanHand(rematch, 7);
        } finally {
          for (const id of liveIds) {
            try {
              harness.delete(id);
            } catch {
              // A prior assertion may have observed a session already closing.
            }
          }
        }
      });
    }
  }
});

test("debug reveal is explicit and leaves the human hand visible", async (t) => {
  const harness = makeHarness(t);
  const { game } = await harness.create({
    players: [
      { deckId: "atog", policyId: "human" },
      { deckId: "white-weenie", policyId: "handcrafted" },
    ],
    seed: 707,
    debugReveal: true,
  });

  try {
    const human = game.snapshot.players[0];
    const opponent = game.snapshot.players[1];
    assert.ok(Array.isArray(human.hand));
    assert.ok(Array.isArray(opponent.revealedHand));
    assert.equal(opponent.revealedHand.length, 2);
    opponent.revealedHand.forEach(assertCard);
  } finally {
    harness.delete(game.id);
  }
});

test("journey fixtures have stable make commands", async () => {
  const webDirectory = path.dirname(TEST_DIRECTORY);
  const [packageJson, fixtureServer, makefile] = await Promise.all([
    readFile(path.join(webDirectory, "package.json"), "utf8").then(JSON.parse),
    readFile(path.join(webDirectory, "fixture-server.mjs"), "utf8"),
    readFile(path.join(webDirectory, "..", "Makefile"), "utf8"),
  ]);

  assert.equal(
    packageJson.scripts["fixture:journey"],
    "node fixture-server.mjs journey",
  );
  assert.equal(
    packageJson.scripts["fixture:delayed-journey"],
    "node fixture-server.mjs delayed-journey",
  );
  assert.match(
    fixtureServer,
    /"journey",\s*path\.join\(directory, "tests", "fixtures", "journey-bridge\.mjs"\)/,
  );
  assert.match(
    fixtureServer,
    /fixtureName === "delayed-journey"[\s\S]+?"--fixture-delay-ms", "650"/,
  );
  assert.match(
    makefile,
    /web-journey:[^\n]*\n\tnpm --prefix web run build\n\tnpm --prefix web run fixture:journey/,
  );
  assert.match(
    makefile,
    /web-delayed-journey:[^\n]*\n\tnpm --prefix web run build\n\tnpm --prefix web run fixture:delayed-journey/,
  );
});
