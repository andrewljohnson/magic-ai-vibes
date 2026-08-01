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
    // The opening decision is the keep-or-mulligan choice on the
    // freshly dealt seven cards.
    assert.equal(created.snapshot.players[0].hand.length, 7);
    assert.equal(created.snapshot.players[0].handSize, 7);
    assert.equal(
      Object.hasOwn(created.snapshot.players[1], "hand"),
      false,
    );
    assert.equal(typeof created.decision.id, "number");
    assert.equal(created.decision.kind, "mulligan");
    assert.equal(created.decision.handSize, 7);
    assert.deepEqual(
      created.decision.options.map(({ index }) => index),
      [0, 1],
    );

    const { game: kept } = await harness.action(created.id, {
      decisionId: created.decision.id,
      index: 0,
    });
    assert.equal(kept.status, "awaiting_action");
    assert.equal(kept.snapshot.turnNumber, 1);
    assert.equal(kept.snapshot.phase, "first_main");
    assert.equal(kept.snapshot.players[0].hand.length, 7);
    assert.ok(
      kept.events.some((event) => event.kind === "turn_started"),
    );
    assert.equal(kept.decision.kind, "priority");

    const land = kept.decision.options.find(
      ({ kind }) => kind === "play_land",
    );
    assert.ok(land, "seeded opening hand should expose a legal land play");
    assert.equal(typeof land.card?.name, "string");

    const { game: advanced } = await harness.action(created.id, {
      decisionId: kept.decision.id,
      index: land.index,
    });
    assert.equal(advanced.id, created.id);
    assert.equal(advanced.status, "awaiting_action");
    assert.equal(advanced.snapshot.players[0].hand.length, 6);
    assert.equal(advanced.snapshot.players[0].lands.length, 1);
    // The bridge may auto-advance trivial follow-up decisions, so the
    // land play need not be the final event - it must simply be present.
    assert.ok(
      advanced.events.some(
        (event) =>
          event.kind === "priority_action" && /Play/.test(event.message),
      ),
    );
    assert.notEqual(advanced.decision.id, kept.decision.id);
  } finally {
    harness.delete(created.id);
  }
});
