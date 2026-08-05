import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

import { createGameContractHarness } from "../server.mjs";

const TEST_DIRECTORY = path.dirname(fileURLToPath(import.meta.url));
const WEB_DIRECTORY = path.dirname(TEST_DIRECTORY);
const INTERACTION_BRIDGE = path.join(
  TEST_DIRECTORY,
  "fixtures",
  "interaction-bridge.mjs",
);

function makeHarness(t) {
  const harness = createGameContractHarness({
    bridgePath: process.execPath,
    bridgeArgsPrefix: [INTERACTION_BRIDGE],
    initialTimeoutMs: 5_000,
    actionTimeoutMs: 5_000,
    idFactory: () => "interaction-fixture",
  });
  t.after(() => harness.shutdown());
  return harness;
}

function makeForcedEmptyBlockersHarness(t) {
  const harness = createGameContractHarness({
    bridgePath: process.execPath,
    bridgeArgsPrefix: [INTERACTION_BRIDGE, "--forced-empty-blockers"],
    initialTimeoutMs: 5_000,
    actionTimeoutMs: 5_000,
    idFactory: () => "forced-empty-blockers-fixture",
  });
  t.after(() => harness.shutdown());
  return harness;
}

test("interaction fixture exposes one exact forced empty blocker declaration", async (t) => {
  const harness = makeForcedEmptyBlockersHarness(t);
  const { game: initial } = await harness.create({
    players: [
      { deckId: "white-weenie", policyId: "human" },
      { deckId: "br-midrange", policyId: "handcrafted" },
    ],
    seed: 42,
    debugReveal: false,
    bluffMode: true,
  });
  t.after(() => {
    try {
      harness.delete(initial.id);
    } catch {
      // A failed assertion may observe an already-stopped child.
    }
  });

  assert.equal(initial.decision.kind, "blockers");
  assert.equal(initial.decision.id, "forced-empty-blockers:opaque/17");
  assert.deepEqual(initial.decision.attackers, [210]);
  assert.deepEqual(initial.decision.choices, []);
  assert.deepEqual(initial.snapshot.players[0].creatures, []);

  const { game: progressed } = await harness.action(initial.id, {
    decisionId: initial.decision.id,
    pairs: [],
  });
  assert.equal(progressed.events.at(-1).kind, "blockers_declared");
  assert.deepEqual(progressed.events.at(-1).pairs, []);
  assert.equal(progressed.decision.kind, "priority");
  assert.equal(progressed.decision.id, "forced-empty-blockers:next-priority");
});

test("interaction fixture advances duplicate cleanup into numeric board blocks", async (t) => {
  const harness = makeHarness(t);
  const { game: initial } = await harness.create({
    players: [
      { deckId: "white-weenie", policyId: "human" },
      { deckId: "br-midrange", policyId: "handcrafted" },
    ],
    seed: 42,
    debugReveal: false,
    bluffMode: false,
  });
  t.after(() => {
    try {
      harness.delete(initial.id);
    } catch {
      // A failed assertion may observe an already-stopped child.
    }
  });

  assert.equal(initial.decision.kind, "cleanup_discard");
  assert.equal(initial.decision.count, 2);
  assert.equal(initial.snapshot.players[0].hand.length, 9);
  assert.deepEqual(
    initial.decision.options.slice(0, 2).map(({ index, card }) => [
      index,
      card.id,
      card.name,
    ]),
    [
      [0, "card-21", "Plains"],
      [1, "card-21", "Plains"],
    ],
    "duplicate definitions remain distinct by exact hand position",
  );

  const { game: blocking } = await harness.action(initial.id, {
    decisionId: "interaction-cleanup-1",
    indices: [0, 1],
  });
  assert.equal(blocking.events.at(-1).kind, "cards_discarded");
  assert.deepEqual(
    blocking.events.at(-1).cards.map(({ name }) => name),
    ["Plains", "Plains"],
  );
  assert.equal(blocking.decision.kind, "blockers");
  assert.deepEqual(blocking.decision.attackers, [210, 211]);
  assert.deepEqual(blocking.decision.choices, [
    { blocker: 110, legalAttackers: [210] },
    { blocker: 111, legalAttackers: [210, 211] },
  ]);

  await assert.rejects(
    harness.action(initial.id, {
      decisionId: 7002,
      pairs: [[211, 110]],
    }),
    /not legal/,
  );

  const { game: finished } = await harness.action(initial.id, {
    decisionId: 7002,
    pairs: [
      [210, 110],
      [210, 111],
    ],
  });
  assert.equal(finished.status, "finished");
  assert.deepEqual(finished.events.at(-1).pairs, [
    [210, 110],
    [210, 111],
  ]);
});

test("interaction browser fixture has one stable make command", async () => {
  const [packageJson, makefile] = await Promise.all([
    readFile(path.join(WEB_DIRECTORY, "package.json"), "utf8").then(JSON.parse),
    readFile(path.join(WEB_DIRECTORY, "..", "Makefile"), "utf8"),
  ]);

  assert.equal(
    packageJson.scripts["fixture:interaction"],
    "node fixture-server.mjs interaction",
  );
  assert.match(
    makefile,
    /web-interaction:[^\n]*\n\tnpm --prefix web run build\n\tnpm --prefix web run fixture:interaction/,
  );
});
