import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

import { createGameContractHarness } from "../server.mjs";

const TEST_DIRECTORY = path.dirname(fileURLToPath(import.meta.url));
const WEB_DIRECTORY = path.dirname(TEST_DIRECTORY);
const TARGET_STACK_BRIDGE = path.join(
  TEST_DIRECTORY,
  "fixtures",
  "target-stack-bridge.mjs",
);

function makeHarness(t) {
  let nextId = 0;
  const harness = createGameContractHarness({
    bridgePath: process.execPath,
    bridgeArgsPrefix: [TARGET_STACK_BRIDGE],
    initialTimeoutMs: 5_000,
    actionTimeoutMs: 5_000,
    idFactory: () => `target-stack-${++nextId}`,
  });
  t.after(() => harness.shutdown());
  return harness;
}

function permanentIds(player) {
  return new Set(
    [
      ...player.lands,
      ...player.creatures,
      ...player.artifacts,
      ...player.enchantments,
    ].map(({ permanentId }) => String(permanentId)),
  );
}

test("targeted-stack fixture exposes both public targets and a legal response", async (t) => {
  const harness = makeHarness(t);
  const { game: initial } = await harness.create({
    players: [
      { deckId: "green", policyId: "human" },
      { deckId: "red", policyId: "learned-value" },
    ],
    seed: 42,
    trainGames: 800,
    trainSeed: 424242,
    debugReveal: false,
  });
  t.after(() => {
    try {
      harness.delete(initial.id);
    } catch {
      // A failed assertion may observe a child that has already stopped.
    }
  });

  assert.equal(initial.status, "awaiting_action");
  assert.equal(initial.config.seed, "42");
  assert.equal(initial.snapshot.turnNumber, 4);
  assert.equal(initial.snapshot.phase, "first_main");
  assert.equal(initial.decision.id, "target-stack-priority-1");
  assert.equal(initial.decision.kind, "priority");

  const [human, opponent] = initial.snapshot.players;
  assert.equal(human.hand.length, 7);
  assert.equal(human.handSize, 7);
  assert.equal(opponent.handSize, 5);
  assert.equal(Object.hasOwn(opponent, "hand"), false);
  assert.equal(Object.hasOwn(opponent, "revealedHand"), false);

  const [bottom, top] = initial.snapshot.stack;
  assert.equal(bottom.card.name, "Giant Growth");
  assert.deepEqual(bottom.target, {
    player: 1,
    creature: 210,
    label: "Ironclaw Orcs #210",
  });
  assert.ok(permanentIds(opponent).has(String(bottom.target.creature)));
  assert.equal(top.card.name, "Lightning Bolt");
  assert.deepEqual(top.target, {
    player: 0,
    creature: 110,
    label: "Grizzly Bears #110",
  });
  assert.ok(permanentIds(human).has(String(top.target.creature)));

  assert.deepEqual(
    initial.decision.options.map(({ index, kind }) => ({ index, kind })),
    [
      { index: 0, kind: "pass" },
      { index: 1, kind: "cast_giant_growth" },
    ],
  );
  assert.deepEqual(initial.decision.options[1].target, top.target);

  const { game: responded } = await harness.action(initial.id, {
    decisionId: initial.decision.id,
    index: 1,
  });
  assert.equal(responded.status, "awaiting_action");
  assert.equal(responded.decision.id, "target-stack-priority-2");
  assert.equal(responded.snapshot.stack.length, 3);
  assert.equal(responded.snapshot.stack.at(-1).card.name, "Giant Growth");
  assert.deepEqual(responded.snapshot.stack.at(-1).target, top.target);
  assert.equal(responded.snapshot.players[0].hand.length, 6);
  assert.equal(responded.snapshot.players[0].lands[1].tapped, true);
  assert.equal(responded.events.at(-1).kind, "priority_action");
  assert.match(responded.events.at(-1).message, /Cast Giant Growth/);

  const { game: resolved } = await harness.action(initial.id, {
    decisionId: responded.decision.id,
    index: 0,
  });
  assert.equal(resolved.snapshot.stack.length, 2);
  assert.equal(resolved.decision.id, "target-stack-priority-3");
  assert.equal(resolved.events.at(-1).kind, "stack_resolved");
});

test("targeted-stack browser fixture has one stable make command", async () => {
  const [packageJson, makefile] = await Promise.all([
    readFile(path.join(WEB_DIRECTORY, "package.json"), "utf8").then(JSON.parse),
    readFile(path.join(WEB_DIRECTORY, "..", "Makefile"), "utf8"),
  ]);

  assert.equal(
    packageJson.scripts["fixture:target-stack"],
    "node fixture-server.mjs target-stack",
  );
  assert.match(
    makefile,
    /web-target-stack:[^\n]*\n\tnpm --prefix web run build\n\tnpm --prefix web run fixture:target-stack/,
  );
});
