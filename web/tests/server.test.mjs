import assert from "node:assert/strict";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { Readable, Writable } from "node:stream";
import { fileURLToPath } from "node:url";
import test from "node:test";

import { createServer } from "../server.mjs";

const TEST_DIRECTORY = path.dirname(fileURLToPath(import.meta.url));
const FAKE_BRIDGE = path.join(
  TEST_DIRECTORY,
  "fixtures",
  "fake-bridge.mjs",
);
const FAKE_CLEANUP_BRIDGE = path.join(
  TEST_DIRECTORY,
  "fixtures",
  "fake-cleanup-bridge.mjs",
);

function dispatch(server, pathname, init = {}) {
  const request = Readable.from(
    init.body === undefined ? [] : [Buffer.from(String(init.body))],
  );
  request.method = init.method ?? "GET";
  request.url = pathname;
  request.headers = init.headers ?? {};

  const chunks = [];
  const responseHeaders = new Map();
  const response = new Writable({
    write(chunk, _encoding, callback) {
      chunks.push(Buffer.from(chunk));
      callback();
    },
  });
  response.statusCode = 200;
  response.writeHead = (status, headers = {}) => {
    response.statusCode = status;
    for (const [name, value] of Object.entries(headers)) {
      responseHeaders.set(name.toLowerCase(), String(value));
    }
    return response;
  };

  return new Promise((resolve, reject) => {
    response.once("error", reject);
    response.once("finish", () => {
      const bytes = Buffer.concat(chunks);
      resolve({
        status: response.statusCode,
        headers: {
          get(name) {
            return responseHeaders.get(name.toLowerCase()) ?? null;
          },
        },
        async text() {
          return bytes.toString("utf8");
        },
        async json() {
          return JSON.parse(bytes.toString("utf8"));
        },
      });
    });
    server.emit("request", request, response);
  });
}

async function startTestServer(t, bridgePath = FAKE_BRIDGE) {
  const distDirectory = await mkdtemp(
    path.join(tmpdir(), "old-school-web-test-"),
  );
  await writeFile(
    path.join(distDirectory, "index.html"),
    "<!doctype html><title>Old School Arena</title><main>Arena</main>",
  );
  await writeFile(
    path.join(distDirectory, "arena.js"),
    "globalThis.arena = true;",
  );

  const server = createServer({
    bridgePath: process.execPath,
    bridgeArgsPrefix: [bridgePath],
    distDirectory,
    initialTimeoutMs: 5_000,
    actionTimeoutMs: 5_000,
    idFactory: () => "test-game",
  });
  t.after(async () => {
    server.gameManager.shutdown();
    await rm(distDirectory, { recursive: true, force: true });
  });
  return {
    request: (pathname, init) => dispatch(server, pathname, init),
    server,
  };
}

async function json(response) {
  const body = await response.json();
  return { response, body };
}

test("serves the arena and publishes five-deck game metadata", async (t) => {
  const { request } = await startTestServer(t);

  const page = await request("/");
  assert.equal(page.status, 200);
  assert.match(await page.text(), /Old School Arena/);

  const asset = await request("/arena.js");
  assert.equal(asset.status, 200);
  assert.match(asset.headers.get("content-type"), /javascript/);

  const { response, body } = await json(
    await request("/api/meta"),
  );
  assert.equal(response.status, 200);
  assert.deepEqual(
    body.decks.map(({ id }) => id),
    ["green", "red", "blue", "white", "ru-aggro"],
  );
  assert.match(
    body.decks.find(({ id }) => id === "blue").deckList,
    /Force Spike/,
  );
  const c16 = body.policies.find(({ id }) => id === "learned-value-c16");
  const foresight = body.policies.find(
    ({ id }) => id === "learned-value-c16-actor-local-search",
  );
  const combinedSearch = body.policies.find(
    ({ id }) => id === "learned-value-c16-combined-search",
  );
  const bilinear = body.policies.find(
    ({ id }) => id === "learned-value-c16-bilinear-aq19",
  );
  const bestResponse = body.policies.find(
    ({ id }) => id === "learned-value-c16-adversarial-blocks",
  );
  const stackDiscipline = body.policies.find(
    ({ id }) => id === "learned-value-c16-stack-discipline",
  );
  const g0 = body.policies.find(({ id }) => id === "learned-value-g0");
  const actor = body.policies.find(({ id }) => id === "learned-actor");
  assert.deepEqual(
    {
      versionDate: c16?.versionDate,
      versionDateLabel: c16?.versionDateLabel,
      lifecycle: c16?.lifecycle,
    },
    {
      versionDate: "2026-07-26",
      versionDateLabel: "Artifact frozen",
      lifecycle: "Research control · not promoted over Handcoded Policy",
    },
  );
  assert.match(c16?.description ?? "", /16 bootstrapped self-play generations/);
  assert.deepEqual(
    {
      name: foresight?.name,
      versionDate: foresight?.versionDate,
      versionDateLabel: foresight?.versionDateLabel,
      lifecycle: foresight?.lifecycle,
    },
    {
      name: "Learned C16 · Foresight Search (AQ4)",
      versionDate: "2026-07-28",
      versionDateLabel: "Manual pilot introduced",
      lifecycle: "Manual diagnostic · not promoted",
    },
  );
  assert.match(foresight?.description ?? "", /outer K8\/H8/);
  assert.match(foresight?.description ?? "", /actor-local inner K2\/H4/);
  assert.match(foresight?.description ?? "", /Priority decisions only/);
  assert.match(foresight?.description ?? "", /attack and block selection still use C16/);
  assert.deepEqual(
    {
      name: combinedSearch?.name,
      versionDate: combinedSearch?.versionDate,
      versionDateLabel: combinedSearch?.versionDateLabel,
      lifecycle: combinedSearch?.lifecycle,
    },
    {
      name: "Learned C16 · Combined Search (AQ15)",
      versionDate: "2026-07-29",
      versionDateLabel: "Manual pilot introduced",
      lifecycle: "Manual diagnostic · unscreened · not promoted",
    },
  );
  assert.match(combinedSearch?.description ?? "", /outer K8\/H8/);
  assert.match(combinedSearch?.description ?? "", /actor-local inner K2\/H4/);
  assert.match(combinedSearch?.description ?? "", /real attack sets/);
  assert.match(combinedSearch?.description ?? "", /defender-best-response minimum/);
  assert.match(combinedSearch?.description ?? "", /Simulated continuations retain canonical C16 combat/);
  assert.deepEqual(
    {
      name: bilinear?.name,
      versionDate: bilinear?.versionDate,
      versionDateLabel: bilinear?.versionDateLabel,
      lifecycle: bilinear?.lifecycle,
    },
    {
      name: "Learned C16 · Bilinear AQ19",
      versionDate: "2026-07-29",
      versionDateLabel: "Manual pilot introduced",
      lifecycle: "Manual pilot · 31–29 selector · not promoted",
    },
  );
  assert.match(bilinear?.description ?? "", /Rank-2 card-agnostic state×action residual/);
  assert.match(bilinear?.description ?? "", /deep actor-local labels/);
  assert.match(bilinear?.description ?? "", /exact C16 K8\/H4 base/);
  assert.match(bilinear?.description ?? "", /Offline all-five-deck gates passed/);
  assert.match(bilinear?.description ?? "", /small selector only licenses manual testing/);
  assert.deepEqual(
    {
      name: bestResponse?.name,
      versionDate: bestResponse?.versionDate,
      versionDateLabel: bestResponse?.versionDateLabel,
      lifecycle: bestResponse?.lifecycle,
    },
    {
      name: "Learned C16 · Best-Response Attacks",
      versionDate: "2026-07-28",
      versionDateLabel: "Fast screen run",
      lifecycle:
        "Exploratory challenger · 127–113 fast screen · awaiting human play-test · not promoted",
    },
  );
  assert.match(bestResponse?.description ?? "", /Exact frozen C16 critic/);
  assert.match(bestResponse?.description ?? "", /K8\/H4/);
  assert.match(bestResponse?.description ?? "", /defender-best-response minimum/);
  assert.deepEqual(
    {
      name: stackDiscipline?.name,
      versionDate: stackDiscipline?.versionDate,
      versionDateLabel: stackDiscipline?.versionDateLabel,
      lifecycle: stackDiscipline?.lifecycle,
    },
    {
      name: "Learned C16 · Stack Discipline",
      versionDate: "2026-07-28",
      versionDateLabel: "Fast screen run",
      lifecycle:
        "Behavior diagnostic · 30–30 fast screen · performance gate not passed · awaiting human play-test · not promoted",
    },
  );
  assert.match(stackDiscipline?.description ?? "", /rules-only marginal-effect filter/);
  assert.match(stackDiscipline?.description ?? "", /same public outcome/);
  assert.match(stackDiscipline?.description ?? "", /strictly fewer resources/);
  assert.doesNotMatch(stackDiscipline?.description ?? "", /never double-counter/i);
  for (const policy of [g0, actor]) {
    assert.equal(policy?.versionDate, "2026-07-24");
    assert.equal(policy?.versionDateLabel, "Recipe introduced");
    assert.match(policy?.lifecycle ?? "", /trained per match/);
  }
  assert.match(g0?.description ?? "", /random play plus two fitted self-play/);
  assert.match(actor?.description ?? "", /priority, attacks, blocks/);
  assert.equal(body.defaults.bluffMode, false);
  assert.equal(body.defaults.learnedGenerations, 16);
  assert.equal(body.defaults.learnedRollouts, 8);
  assert.deepEqual(body.decisionKinds, [
    "priority",
    "attackers",
    "blockers",
    "damage_order",
    "cleanup_discard",
  ]);
});

test("cleanup discard validates exact duplicate-card hand positions", async (t) => {
  const { request } = await startTestServer(t, FAKE_CLEANUP_BRIDGE);
  const created = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ seed: 42 }),
    }),
  );
  assert.equal(created.response.status, 201);
  assert.equal(created.body.game.decision.kind, "cleanup_discard");
  assert.equal(created.body.game.decision.count, 2);
  assert.deepEqual(
    created.body.game.decision.options.map(({ index, card }) => [
      index,
      card.name,
    ]),
    [
      [0, "Forest"],
      [1, "Forest"],
      [2, "Mountain"],
    ],
  );

  for (const action of [
    { decisionId: "stale", indices: [0, 1], status: 409, code: "stale_decision" },
    { decisionId: "cleanup-1", indices: [0], status: 422, code: "illegal_action" },
    { decisionId: "cleanup-1", indices: [0, 0], status: 422, code: "illegal_action" },
    { decisionId: "cleanup-1", indices: [0, 9], status: 422, code: "illegal_action" },
    { decisionId: "cleanup-1", indices: [0, 1.5], status: 422, code: "illegal_action" },
  ]) {
    const rejected = await json(
      await request("/api/games/test-game/actions", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify(action),
      }),
    );
    assert.equal(rejected.response.status, action.status);
    assert.equal(rejected.body.error.code, action.code);
  }

  const accepted = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "cleanup-1",
        kind: "cleanup_discard",
        indices: [0, 1],
      }),
    }),
  );
  assert.equal(accepted.response.status, 200);
  assert.equal(accepted.body.game.status, "finished");
  assert.equal(accepted.body.game.events.at(-1).kind, "cards_discarded");
  assert.deepEqual(
    accepted.body.game.events.at(-1).cards.map(({ name }) => name),
    ["Forest", "Forest"],
  );
});

test("creates a session and maps canonical config to bridge flags", async (t) => {
  const { request } = await startTestServer(t);
  const { response, body } = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        players: [
          { deckId: "blue", policyId: "human" },
          { deckId: "white", policyId: "learned-value-g0" },
        ],
        seed: "18446744073709551615",
        trainGames: 321,
        trainSeed: 424242,
        debugReveal: true,
        bluffMode: true,
        rollouts: 3,
        deepRollouts: 9,
        learnedRollouts: 5,
        learnedGenerations: 0,
      }),
    }),
  );

  assert.equal(response.status, 201);
  assert.equal(body.game.id, "test-game");
  assert.equal(body.game.status, "awaiting_action");
  assert.equal(body.game.decision.id, "priority-1");
  assert.equal(body.game.decision.kind, "priority");
  assert.deepEqual(body.game.snapshot.received, {
    humanDeck: "blue",
    opponentDeck: "white",
    opponentPolicy: "learned-value-g0",
    seed: "18446744073709551615",
    trainGames: "321",
    trainSeed: "424242",
    rollouts: "3",
    deepRollouts: "9",
    learnedRollouts: "5",
    learnedGenerations: "0",
    debugReveal: true,
    bluffMode: true,
  });
  assert.deepEqual(body.game.snapshot.players[1], {
    handSize: 7,
    life: 20,
  });
  assert.deepEqual(body.game.model, {
    family: "learned-value",
    generation: 0,
    searchWorlds: 5,
    horizonTurns: 4,
    source: "trained-for-match",
    fingerprint:
      "0000000000000000000000000000000000000000000000000000000000000000",
  });

  const fetched = await json(
    await request("/api/games/test-game"),
  );
  assert.equal(fetched.response.status, 200);
  assert.equal(fetched.body.game.decision.id, "priority-1");
});

test("C16 is explicit, canonical, and fingerprinted in match metadata", async (t) => {
  const { request } = await startTestServer(t);
  const { response, body } = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        players: [
          { deckId: "blue", policyId: "human" },
          { deckId: "blue", policyId: "learned-value-c16" },
        ],
        seed: 42,
        trainGames: 800,
        trainSeed: 424242,
        learnedRollouts: 8,
        learnedGenerations: 16,
      }),
    }),
  );

  assert.equal(response.status, 201);
  assert.equal(body.game.config.players[1].policyId, "learned-value-c16");
  assert.equal(body.game.snapshot.received.opponentPolicy, "learned-value-c16");
  assert.equal(body.game.snapshot.received.learnedGenerations, "16");
  assert.deepEqual(body.game.model, {
    family: "learned-value",
    generation: 16,
    searchWorlds: 8,
    horizonTurns: 4,
    source: "frozen-artifact",
    fingerprint:
      "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f",
  });
});

test("best-response attacks reuse exact frozen C16 identity", async (t) => {
  const { request } = await startTestServer(t);
  const policyId = "learned-value-c16-adversarial-blocks";
  const { response, body } = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        players: [
          { deckId: "ru-aggro", policyId: "human" },
          { deckId: "ru-aggro", policyId },
        ],
        seed: 42,
        trainGames: 800,
        trainSeed: 424242,
        learnedRollouts: 8,
        learnedGenerations: 16,
      }),
    }),
  );

  assert.equal(response.status, 201);
  assert.equal(body.game.config.players[1].policyId, policyId);
  assert.equal(body.game.snapshot.received.opponentPolicy, policyId);
  assert.equal(body.game.snapshot.received.trainGames, "800");
  assert.equal(body.game.snapshot.received.trainSeed, "424242");
  assert.equal(body.game.snapshot.received.learnedGenerations, "16");
  assert.deepEqual(body.game.model, {
    family: "learned-value",
    generation: 16,
    searchWorlds: 8,
    horizonTurns: 4,
    source: "frozen-artifact",
    fingerprint:
      "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f",
  });
});

test("stack discipline reuses exact frozen C16 identity", async (t) => {
  const { request } = await startTestServer(t);
  const policyId = "learned-value-c16-stack-discipline";
  const { response, body } = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        players: [
          { deckId: "blue", policyId: "human" },
          { deckId: "blue", policyId },
        ],
        seed: 42,
        trainGames: 800,
        trainSeed: 424242,
        learnedRollouts: 8,
        learnedGenerations: 16,
      }),
    }),
  );

  assert.equal(response.status, 201);
  assert.equal(body.game.config.players[1].policyId, policyId);
  assert.equal(body.game.snapshot.received.opponentPolicy, policyId);
  assert.equal(body.game.snapshot.received.trainGames, "800");
  assert.equal(body.game.snapshot.received.trainSeed, "424242");
  assert.equal(body.game.snapshot.received.learnedGenerations, "16");
  assert.deepEqual(body.game.model, {
    family: "learned-value",
    generation: 16,
    searchWorlds: 8,
    horizonTurns: 4,
    source: "frozen-artifact",
    fingerprint:
      "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f",
  });
});

test("AQ4 foresight search reuses exact frozen C16 identity", async (t) => {
  const { request } = await startTestServer(t);
  const policyId = "learned-value-c16-actor-local-search";
  const { response, body } = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        players: [
          { deckId: "blue", policyId: "human" },
          { deckId: "blue", policyId },
        ],
        seed: 42,
        trainGames: 800,
        trainSeed: 424242,
        learnedRollouts: 8,
        learnedGenerations: 16,
      }),
    }),
  );

  assert.equal(response.status, 201);
  assert.equal(body.game.config.players[1].policyId, policyId);
  assert.equal(body.game.snapshot.received.opponentPolicy, policyId);
  assert.equal(body.game.snapshot.received.trainGames, "800");
  assert.equal(body.game.snapshot.received.trainSeed, "424242");
  assert.equal(body.game.snapshot.received.learnedGenerations, "16");
  assert.deepEqual(body.game.model, {
    family: "learned-value",
    generation: 16,
    searchWorlds: 8,
    horizonTurns: 8,
    source: "frozen-artifact",
    fingerprint:
      "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f",
  });
});

test("AQ15 combined search reuses exact frozen C16 identity", async (t) => {
  const { request } = await startTestServer(t);
  const policyId = "learned-value-c16-combined-search";
  const { response, body } = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        players: [
          { deckId: "blue", policyId: "human" },
          { deckId: "blue", policyId },
        ],
        seed: 42,
        trainGames: 800,
        trainSeed: 424242,
        learnedRollouts: 8,
        learnedGenerations: 16,
      }),
    }),
  );

  assert.equal(response.status, 201);
  assert.equal(body.game.config.players[1].policyId, policyId);
  assert.equal(body.game.snapshot.received.opponentPolicy, policyId);
  assert.equal(body.game.snapshot.received.trainGames, "800");
  assert.equal(body.game.snapshot.received.trainSeed, "424242");
  assert.equal(body.game.snapshot.received.learnedGenerations, "16");
  assert.deepEqual(body.game.model, {
    family: "learned-value",
    generation: 16,
    searchWorlds: 8,
    horizonTurns: 8,
    source: "frozen-artifact",
    fingerprint:
      "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f",
  });
});

test("AQ19 bilinear reuses exact frozen C16 identity", async (t) => {
  const { request } = await startTestServer(t);
  const policyId = "learned-value-c16-bilinear-aq19";
  const { response, body } = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        players: [
          { deckId: "blue", policyId: "human" },
          { deckId: "blue", policyId },
        ],
        seed: 42,
        trainGames: 800,
        trainSeed: 424242,
        learnedRollouts: 8,
        learnedGenerations: 16,
      }),
    }),
  );

  assert.equal(response.status, 201);
  assert.equal(body.game.config.players[1].policyId, policyId);
  assert.equal(body.game.snapshot.received.opponentPolicy, policyId);
  assert.equal(body.game.snapshot.received.trainGames, "800");
  assert.equal(body.game.snapshot.received.trainSeed, "424242");
  assert.equal(body.game.snapshot.received.learnedGenerations, "16");
  assert.deepEqual(body.game.model, {
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
  });
});

test("rejects stale and illegal choices before progressing a valid game", async (t) => {
  const { request } = await startTestServer(t);
  await request("/api/games", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      players: [
        { deckId: "ru-aggro", policyId: "human" },
        { deckId: "red", policyId: "handcrafted" },
      ],
      seed: 42,
    }),
  });

  const stale = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ decisionId: "old", index: 1 }),
    }),
  );
  assert.equal(stale.response.status, 409);
  assert.equal(stale.body.error.code, "stale_decision");

  const illegalPriority = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ decisionId: "priority-1", index: 999 }),
    }),
  );
  assert.equal(illegalPriority.response.status, 422);
  assert.equal(illegalPriority.body.error.code, "illegal_action");

  const progressed = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "priority-1",
        kind: "priority",
        index: 1,
      }),
    }),
  );
  assert.equal(progressed.response.status, 200);
  assert.equal(progressed.body.game.status, "awaiting_action");
  assert.equal(progressed.body.game.decision.kind, "attackers");
  assert.equal(progressed.body.game.decision.id, 2);
  assert.equal(progressed.body.game.events.at(-1).kind, "spell_resolved");
  assert.equal(
    progressed.body.game.events.at(-1).message,
    "Flying Man resolved",
  );

  const replayed = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ decisionId: "priority-1", index: 1 }),
    }),
  );
  assert.equal(replayed.response.status, 409);
  assert.equal(replayed.body.error.code, "stale_decision");

  const illegalAttacker = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ decisionId: "2", ids: [999] }),
    }),
  );
  assert.equal(illegalAttacker.response.status, 422);
  assert.equal(illegalAttacker.body.error.code, "illegal_action");

  const finished = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "2",
        kind: "attackers",
        ids: [11],
      }),
    }),
  );
  assert.equal(finished.response.status, 200);
  assert.equal(finished.body.game.status, "awaiting_action");
  assert.equal(finished.body.game.decision.kind, "blockers");

  const illegalBlock = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "blockers-3",
        pairs: [[999, 31]],
      }),
    }),
  );
  assert.equal(illegalBlock.response.status, 422);
  assert.equal(illegalBlock.body.error.code, "illegal_action");

  const ordered = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "blockers-3",
        kind: "blockers",
        pairs: [[21, 31]],
      }),
    }),
  );
  assert.equal(ordered.response.status, 200);
  assert.equal(ordered.body.game.decision.kind, "damage_order");

  const incompleteOrder = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ decisionId: 4, ids: [31] }),
    }),
  );
  assert.equal(incompleteOrder.response.status, 422);
  assert.equal(incompleteOrder.body.error.code, "illegal_action");

  const gameOver = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: 4,
        kind: "damage_order",
        ids: [32, 31],
      }),
    }),
  );
  assert.equal(gameOver.response.status, 200);
  assert.equal(gameOver.body.game.status, "finished");
  assert.deepEqual(gameOver.body.game.result, {
    winner: 0,
    reason: "life",
    turns: 3,
  });
  assert.equal(gameOver.body.game.decision, null);
});

test("serializes simultaneous clicks and forwards only the first", async (t) => {
  const { request } = await startTestServer(t);
  await request("/api/games", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ seed: 7 }),
  });

  const click = () =>
    request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ decisionId: "priority-1", index: 1 }),
    });
  const responses = await Promise.all([click(), click()]);
  assert.deepEqual(
    responses.map(({ status }) => status).sort(),
    [200, 409],
  );

  const current = await json(
    await request("/api/games/test-game"),
  );
  assert.equal(current.body.game.status, "awaiting_action");
  assert.equal(current.body.game.decision.kind, "attackers");
});

test("rejects malformed config without spawning a game", async (t) => {
  const { request, server } = await startTestServer(t);
  const cases = [
    {
      players: [
        { deckId: "alpha", policyId: "human" },
        { deckId: "red", policyId: "random" },
      ],
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        { deckId: "red", policyId: "oracle" },
      ],
    },
    { seed: "18446744073709551616" },
    { trainGames: 0 },
    { learnedRollouts: 4_097 },
    {
      players: [
        { deckId: "green", policyId: "human" },
        { deckId: "blue", policyId: "learned-value-c16" },
      ],
      trainGames: 799,
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        { deckId: "blue", policyId: "learned-value-c16" },
      ],
      learnedGenerations: 0,
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        {
          deckId: "blue",
          policyId: "learned-value-c16-stack-discipline",
        },
      ],
      learnedRollouts: 7,
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        {
          deckId: "blue",
          policyId: "learned-value-c16-combined-search",
        },
      ],
      learnedRollouts: 7,
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        {
          deckId: "blue",
          policyId: "learned-value-c16-actor-local-search",
        },
      ],
      learnedRollouts: 7,
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        {
          deckId: "blue",
          policyId: "learned-value-c16-bilinear-aq19",
        },
      ],
      learnedRollouts: 7,
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        {
          deckId: "blue",
          policyId: "learned-value-c16-stack-discipline",
        },
      ],
      trainGames: 799,
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        {
          deckId: "blue",
          policyId: "learned-value-c16-stack-discipline",
        },
      ],
      trainSeed: 424243,
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        {
          deckId: "blue",
          policyId: "learned-value-c16-stack-discipline",
        },
      ],
      learnedGenerations: 0,
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        {
          deckId: "blue",
          policyId: "learned-value-c16-adversarial-blocks",
        },
      ],
      trainGames: 799,
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        {
          deckId: "blue",
          policyId: "learned-value-c16-adversarial-blocks",
        },
      ],
      trainSeed: 424243,
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        {
          deckId: "blue",
          policyId: "learned-value-c16-adversarial-blocks",
        },
      ],
      learnedGenerations: 0,
    },
    {
      players: [
        { deckId: "green", policyId: "human" },
        { deckId: "blue", policyId: "learned-value-g0" },
      ],
      learnedGenerations: 16,
    },
    { debugReveal: "yes" },
    { bluffMode: "yes" },
  ];

  for (const candidate of cases) {
    const { response, body } = await json(
      await request("/api/games", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify(candidate),
      }),
    );
    assert.equal(response.status, 400);
    assert.equal(body.error.code, "invalid_config");
  }
  assert.equal(server.gameManager.sessions.size, 0);
});
