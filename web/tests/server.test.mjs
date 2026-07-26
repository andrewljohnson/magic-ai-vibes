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

async function startTestServer(t) {
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
    bridgeArgsPrefix: [FAKE_BRIDGE],
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
  assert.ok(body.policies.some(({ id }) => id === "learned-value"));
  assert.ok(body.policies.some(({ id }) => id === "learned-actor"));
  assert.deepEqual(body.decisionKinds, [
    "priority",
    "attackers",
    "blockers",
    "damage_order",
  ]);
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
          { deckId: "white", policyId: "learned-value" },
        ],
        seed: "18446744073709551615",
        trainGames: 321,
        trainSeed: 424242,
        debugReveal: true,
        rollouts: 3,
        deepRollouts: 9,
        learnedRollouts: 5,
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
    opponentPolicy: "learned-value",
    seed: "18446744073709551615",
    trainGames: "321",
    trainSeed: "424242",
    rollouts: "3",
    deepRollouts: "9",
    learnedRollouts: "5",
    debugReveal: true,
  });
  assert.deepEqual(body.game.snapshot.players[1], {
    handSize: 7,
    life: 20,
  });

  const fetched = await json(
    await request("/api/games/test-game"),
  );
  assert.equal(fetched.response.status, 200);
  assert.equal(fetched.body.game.decision.id, "priority-1");
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
    { debugReveal: "yes" },
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
