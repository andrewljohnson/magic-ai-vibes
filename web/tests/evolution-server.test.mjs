import assert from "node:assert/strict";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { Readable, Writable } from "node:stream";
import { fileURLToPath } from "node:url";
import test from "node:test";

import { createServer } from "../server.mjs";

const TEST_DIRECTORY = path.dirname(fileURLToPath(import.meta.url));
const FAKE_EVOLUTION_BRIDGE = path.join(
  TEST_DIRECTORY,
  "fixtures",
  "fake-evolution-bridge.mjs",
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
        async json() {
          return JSON.parse(bytes.toString("utf8"));
        },
      });
    });
    server.emit("request", request, response);
  });
}

async function startTestServer(t, options = {}) {
  const distDirectory = await mkdtemp(
    path.join(tmpdir(), "old-school-evolution-test-"),
  );
  await writeFile(
    path.join(distDirectory, "index.html"),
    "<!doctype html><title>Evolution test</title>",
  );
  let gameId = 0;
  let evolutionId = 0;
  let deckId = 0;
  const server = createServer({
    bridgePath: process.execPath,
    bridgeArgsPrefix: [
      FAKE_EVOLUTION_BRIDGE,
      ...(options.bridgeArgsPrefix ?? []),
    ],
    distDirectory,
    initialTimeoutMs: 5_000,
    actionTimeoutMs: 5_000,
    evolutionTimeoutMs: options.evolutionTimeoutMs ?? 5_000,
    evolutionTerminationGraceMs:
      options.evolutionTerminationGraceMs ?? 250,
    evolutionMaxOutputBytes:
      options.evolutionMaxOutputBytes ?? 128 * 1024,
    idFactory: () => `game-${++gameId}`,
    evolutionIdFactory: () => `evolution-${++evolutionId}`,
    deckIdFactory: () => `deck-${++deckId}`,
  });
  t.after(async () => {
    server.gameManager.shutdown();
    server.evolutionManager.shutdown();
    await rm(distDirectory, { recursive: true, force: true });
  });
  return {
    request: (pathname, init) => dispatch(server, pathname, init),
    server,
  };
}

async function json(response) {
  return { response, body: await response.json() };
}

function post(request, pathname, body) {
  return request(pathname, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
}

test("publishes bounded evolution metadata and rejects invalid jobs", async (t) => {
  const { request, server } = await startTestServer(t);
  const meta = await json(await request("/api/meta"));
  assert.equal(meta.response.status, 200);
  assert.deepEqual(
    meta.body.evolution.pilots.map(({ id }) => id),
    ["handcrafted", "spz", "random", "monte-carlo", "deep-monte-carlo"],
  );
  assert.deepEqual(meta.body.evolution.defaults, {
    generations: 3,
    population: 8,
    games: 1,
    pilot: "handcrafted",
    seed: "424242",
  });
  assert.deepEqual(meta.body.evolution.limits.generations, {
    min: 1,
    max: 200,
  });
  assert.deepEqual(meta.body.evolution.limits.population, {
    min: 7,
    max: 32,
  });
  assert.deepEqual(meta.body.evolution.limits.games, {
    min: 1,
    max: 16,
  });
  assert.deepEqual(meta.body.evolution.limits.seed, {
    min: 0,
    max: 4_294_967_295,
  });
  assert.equal(meta.body.evolution.active, false);
  assert.equal(meta.body.evolution.storage, "server-memory");
  assert.match(meta.body.evolution.notice, /server restarts/);

  const invalidBodies = [
    { generations: 0 },
    { generations: 201 },
    { population: 6 },
    { population: 33 },
    { games: 0 },
    { games: 17 },
    { seed: "4294967296" },
    { pilot: "learned-value-c16" },
  ];
  for (const candidate of invalidBodies) {
    const rejected = await json(
      await post(request, "/api/evolutions", candidate),
    );
    assert.equal(rejected.response.status, 400);
    assert.equal(
      rejected.body.error.code,
      "invalid_evolution_config",
    );
  }
  assert.equal(server.evolutionManager.isActive(), false);

  const wrongMethod = await json(await request("/api/evolutions"));
  assert.equal(wrongMethod.response.status, 405);
  assert.equal(wrongMethod.body.error.code, "method_not_allowed");
});

test("saves only an engine result and plays its exact card vector", async (t) => {
  const { request, server } = await startTestServer(t);
  const created = await json(
    await post(request, "/api/evolutions", {
      generations: 2,
      population: 7,
      games: 1,
      pilot: "handcrafted",
      seed: "4294967295",
    }),
  );
  assert.equal(created.response.status, 201);
  assert.deepEqual(created.body.evolution, {
    id: "evolution-1",
    seed: "4294967295",
    pilot: "handcrafted",
    generations: [50, 51],
    population: 7,
    games: 1,
    best: {
      cards: [
        { id: 0, name: "Forest", count: 20 },
        { id: 2, name: "Grizzly Bears", count: 20 },
      ],
      stats: {
        games: 28,
        wins: 14,
        losses: 14,
        draws: 0,
        winRate: 50,
      },
      byOpponent: [
        ["green", "Green"],
        ["red", "Red"],
        ["blue", "Blue"],
        ["white", "White"],
        ["ru-aggro", "RU Aggro"],
        ["lotus-combo", "Lotus Combo"],
        ["burn", "Burn"],
      ].map(([deckId, name]) => ({
        deckId,
        name,
        games: 4,
        wins: 2,
        losses: 2,
        draws: 0,
        winRate: 50,
      })),
    },
    top: [
      {
        cards: [
          { id: 0, name: "Forest", count: 20 },
          { id: 2, name: "Grizzly Bears", count: 20 },
        ],
        stats: {
          games: 28,
          wins: 14,
          losses: 14,
          draws: 0,
          winRate: 50,
        },
      },
      {
        cards: [
          { id: 0, name: "Forest", count: 19 },
          { id: 2, name: "Grizzly Bears", count: 21 },
        ],
        stats: {
          games: 28,
          wins: 13,
          losses: 15,
          draws: 0,
          winRate: (100 * 13) / 28,
        },
      },
    ],
  });

  const clientAuthoredCards = await json(
    await post(
      request,
      "/api/evolutions/evolution-1/save",
      {
        name: "Injected",
        cards: [{ id: 1, name: "Mountain", count: 40 }],
      },
    ),
  );
  assert.equal(clientAuthoredCards.response.status, 400);
  assert.equal(
    clientAuthoredCards.body.error.code,
    "invalid_deck_name",
  );

  const saved = await json(
    await post(
      request,
      "/api/evolutions/evolution-1/save",
      { name: "  Bear alternating  " },
    ),
  );
  assert.equal(saved.response.status, 201);
  assert.equal(saved.body.deck.id, "deck-1");
  assert.equal(saved.body.deck.name, "Bear alternating");
  assert.equal(saved.body.deck.ephemeral, true);
  assert.equal(saved.body.deck.evolution.id, "evolution-1");
  assert.deepEqual(saved.body.deck.cards, created.body.evolution.best.cards);

  const meta = await json(await request("/api/meta"));
  const savedMeta = meta.body.decks.find(({ id }) => id === "deck-1");
  assert.deepEqual(savedMeta, saved.body.deck);
  assert.equal(meta.body.decks.length, 8);

  const game = await json(
    await post(request, "/api/games", {
      players: [
        { deckId: "deck-1", policyId: "human" },
        { deckId: "deck-1", policyId: "handcrafted" },
      ],
      seed: 42,
    }),
  );
  assert.equal(game.response.status, 201);
  assert.equal(game.body.game.config.players[0].deckId, "deck-1");
  assert.equal(game.body.game.config.players[1].deckId, "deck-1");
  assert.equal(game.body.game.snapshot.received.humanDeck, "ru-aggro");
  assert.equal(
    game.body.game.snapshot.received.opponentDeck,
    "ru-aggro",
  );
  const exactVector = Array.from(
    { length: 40 },
    (_, index) => (index % 2 === 0 ? 0 : 2),
  ).join(",");
  assert.equal(
    game.body.game.snapshot.received.humanDeckCards,
    exactVector,
  );
  assert.equal(
    game.body.game.snapshot.received.opponentDeckCards,
    exactVector,
  );
  server.gameManager.delete(game.body.game.id);

  const duplicateSave = await json(
    await post(
      request,
      "/api/evolutions/evolution-1/save",
      { name: "Again" },
    ),
  );
  assert.equal(duplicateSave.response.status, 409);
  assert.equal(
    duplicateSave.body.error.code,
    "evolution_already_saved",
  );
});

test("saved decks disappear with their Node server process", async (t) => {
  const first = await startTestServer(t);
  const evolved = await json(
    await post(first.request, "/api/evolutions", {}),
  );
  await post(
    first.request,
    `/api/evolutions/${evolved.body.evolution.id}/save`,
    { name: "Temporary" },
  );
  const firstMeta = await json(await first.request("/api/meta"));
  assert.equal(firstMeta.body.decks.some(({ ephemeral }) => ephemeral), true);

  const restarted = await startTestServer(t);
  const restartedMeta = await json(await restarted.request("/api/meta"));
  assert.equal(
    restartedMeta.body.decks.some(({ ephemeral }) => ephemeral),
    false,
  );
  const rejected = await json(
    await post(restarted.request, "/api/games", {
      players: [
        { deckId: "deck-1", policyId: "human" },
        { deckId: "red", policyId: "random" },
      ],
    }),
  );
  assert.equal(rejected.response.status, 400);
  assert.equal(rejected.body.error.code, "invalid_config");
});

test("permits only one active evolution", async (t) => {
  const { request } = await startTestServer(t, {
    bridgeArgsPrefix: ["--evolution-delay-ms", "100"],
  });
  const first = post(request, "/api/evolutions", { seed: 101 });
  await new Promise((resolve) => setTimeout(resolve, 20));

  const activeMeta = await json(await request("/api/meta"));
  assert.equal(activeMeta.body.evolution.active, true);
  const busy = await json(
    await post(request, "/api/evolutions", { seed: 202 }),
  );
  assert.equal(busy.response.status, 409);
  assert.equal(busy.body.error.code, "evolution_busy");

  const completed = await json(await first);
  assert.equal(completed.response.status, 201);
  const idleMeta = await json(await request("/api/meta"));
  assert.equal(idleMeta.body.evolution.active, false);
});

test("keeps the concurrency gate closed until timed-out child exit", async (t) => {
  const { request } = await startTestServer(t, {
    bridgeArgsPrefix: [
      "--evolution-delay-ms",
      "1000",
      "--evolution-ignore-sigterm",
    ],
    evolutionTimeoutMs: 80,
    evolutionTerminationGraceMs: 150,
  });
  const startedAt = Date.now();
  const first = post(request, "/api/evolutions", { seed: 303 });
  await new Promise((resolve) => setTimeout(resolve, 115));

  const stillActive = await json(await request("/api/meta"));
  assert.equal(stillActive.body.evolution.active, true);
  const overlapping = await json(
    await post(request, "/api/evolutions", { seed: 404 }),
  );
  assert.equal(overlapping.response.status, 409);
  assert.equal(overlapping.body.error.code, "evolution_busy");

  const timedOut = await json(await first);
  assert.equal(timedOut.response.status, 504);
  assert.equal(timedOut.body.error.code, "evolution_timeout");
  assert.ok(
    Date.now() - startedAt >= 200,
    "timeout response should wait for bounded kill escalation and child close",
  );
  const idle = await json(await request("/api/meta"));
  assert.equal(idle.body.evolution.active, false);
});

test("bounds evolution failures, protocol, runtime, and output", async (t) => {
  const cases = [
    {
      prefix: ["--evolution-fail"],
      status: 502,
      code: "evolution_failed",
    },
    {
      prefix: ["--evolution-invalid"],
      status: 502,
      code: "evolution_protocol_error",
    },
    {
      prefix: ["--evolution-inconsistent-total"],
      status: 502,
      code: "evolution_protocol_error",
    },
    {
      prefix: ["--evolution-delay-ms", "100"],
      timeout: 15,
      status: 504,
      code: "evolution_timeout",
    },
    {
      prefix: ["--evolution-oversize"],
      output: 1_024,
      status: 502,
      code: "evolution_output_too_large",
    },
  ];

  for (const candidate of cases) {
    await t.test(candidate.code, async (subtest) => {
      const { request, server } = await startTestServer(subtest, {
        bridgeArgsPrefix: candidate.prefix,
        evolutionTimeoutMs: candidate.timeout,
        evolutionMaxOutputBytes: candidate.output,
      });
      const failed = await json(
        await post(request, "/api/evolutions", {}),
      );
      assert.equal(failed.response.status, candidate.status);
      assert.equal(failed.body.error.code, candidate.code);
      assert.equal(server.evolutionManager.isActive(), false);
    });
  }
});
