import assert from "node:assert/strict";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

import { createGameContractHarness } from "../server.mjs";

const WEB_ROOT = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "..",
);
const REAL_BRIDGE = path.resolve(
  WEB_ROOT,
  "../build/old-school-web-bridge",
);

test("the production session manager drives the real engine bridge without HTTP", async (t) => {
  let nextId = 0;
  const harness = createGameContractHarness({
    bridgePath: REAL_BRIDGE,
    initialTimeoutMs: 10_000,
    actionTimeoutMs: 10_000,
    idFactory: () => `real-bridge-${++nextId}`,
  });
  t.after(() => harness.shutdown());

  const requestedConfig = {
    players: [
      { deckId: "ru-aggro", policyId: "human" },
      { deckId: "red", policyId: "random" },
    ],
    seed: 42,
    debugReveal: false,
    bluffMode: false,
    rollouts: 1,
    deepRollouts: 1,
  };

  const { game: created } = await harness.create(requestedConfig);
  try {
    assert.equal(created.status, "awaiting_action");
    assert.deepEqual(created.config, {
      ...requestedConfig,
      seed: "42",
    });
    assert.equal(created.snapshot.turnNumber, 1);
    assert.equal(created.snapshot.phase, "first_main");
    assert.equal(created.snapshot.players[0].hand.length, 7);
    assert.equal(created.snapshot.players[0].handSize, 7);
    assert.equal(
      Object.hasOwn(created.snapshot.players[1], "hand"),
      false,
    );
    assert.equal(created.events.at(-1).kind, "turn_started");
    assert.equal(typeof created.decision.id, "number");
    assert.equal(created.decision.kind, "priority");

    const land = created.decision.options.find(
      ({ kind }) => kind === "play_land",
    );
    assert.ok(land, "seeded opening hand should expose a legal land play");
    assert.equal(typeof land.card?.name, "string");

    const { game: advanced } = await harness.action(created.id, {
      decisionId: created.decision.id,
      index: land.index,
    });
    assert.equal(advanced.id, created.id);
    assert.equal(advanced.status, "awaiting_action");
    assert.equal(advanced.snapshot.players[0].hand.length, 6);
    assert.equal(advanced.snapshot.players[0].lands.length, 1);
    assert.equal(advanced.events.at(-1).kind, "priority_action");
    assert.match(advanced.events.at(-1).message, /Play/);
    assert.notEqual(advanced.decision.id, created.decision.id);
  } finally {
    harness.delete(created.id);
  }
});
