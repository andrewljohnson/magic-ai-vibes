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
const FAKE_BUG_REPORT_BRIDGE = path.join(
  TEST_DIRECTORY,
  "fixtures",
  "fake-bug-report-bridge.mjs",
);
const FAKE_BUG_REPORT_TIMEOUT_BRIDGE = path.join(
  TEST_DIRECTORY,
  "fixtures",
  "fake-bug-report-timeout-bridge.mjs",
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

async function startTestServer(
  t,
  bridgePath = FAKE_BRIDGE,
  options = {},
) {
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
    initialTimeoutMs: options.initialTimeoutMs ?? 5_000,
    actionTimeoutMs: options.actionTimeoutMs ?? 5_000,
    idFactory: options.idFactory ?? (() => "test-game"),
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

test("serves the arena and publishes six-deck game metadata", async (t) => {
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
    ["rg-berserk", "atog", "br-midrange", "robots", "white-weenie", "uwr"],
  );
  assert.match(
    body.decks.find(({ id }) => id === "uwr").deckList,
    /Savannah Lions/,
  );
  assert.deepEqual(
    body.policies.map(({ id }) => id),
    ["random", "monte-carlo", "deep-monte-carlo", "handcrafted", "spz"],
  );
  const spz = body.policies.find(({ id }) => id === "spz");
  assert.match(spz?.description ?? "", /[Ss]ix-deck/);
  assert.equal(body.defaults.bluffMode, false);
  assert.deepEqual(body.decisionKinds, [
    "priority",
    "attackers",
    "blockers",
    "damage_order",
    "cleanup_discard",
    "sylvan_return",
    "mulligan",
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

test("bug reports preserve public decisions and successful actions without hidden cards", async (t) => {
  const { request } = await startTestServer(t, FAKE_BUG_REPORT_BRIDGE);
  const created = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        players: [
          { deckId: "atog", policyId: "human" },
          { deckId: "atog", policyId: "spz" },
        ],
        seed: 42,
        debugReveal: true,
        bluffMode: false,
        rollouts: 2,
        deepRollouts: 8,
      }),
    }),
  );
  assert.equal(created.response.status, 201);

  const initial = await json(
    await request("/api/games/test-game/bug-report"),
  );
  assert.equal(initial.response.status, 200);
  assert.equal(initial.body.report.schema, "old-school-arena-bug-report");
  assert.equal(initial.body.report.version, 1);
  assert.deepEqual(initial.body.report.successfulHumanActions, []);
  assert.equal(initial.body.report.currentDecision.kind, "priority");
  assert.deepEqual(
    initial.body.report.currentDecision.options.map(({ index, kind }) => [
      index,
      kind,
    ]),
    [
      [0, "pass"],
      [1, "cast_counterspell"],
    ],
  );

  const rejected = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ decisionId: "stale", index: 1 }),
    }),
  );
  assert.equal(rejected.response.status, 409);
  assert.deepEqual(
    (await json(await request("/api/games/test-game/bug-report"))).body.report
      .successfulHumanActions,
    [],
  );

  const illegalOption = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "report-priority-1",
        index: 99,
      }),
    }),
  );
  assert.equal(illegalOption.response.status, 422);
  assert.equal(illegalOption.body.error.code, "illegal_action");
  assert.deepEqual(
    (await json(await request("/api/games/test-game/bug-report"))).body.report
      .successfulHumanActions,
    [],
  );

  const firstAction = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "report-priority-1",
        index: 1,
      }),
    }),
  );
  assert.equal(firstAction.response.status, 200);
  assert.equal(firstAction.body.game.decision.kind, "attackers");
  assert.deepEqual(
    (await json(await request("/api/games/test-game/bug-report"))).body.report
      .currentDecision,
    {
      decisionId: 2,
      kind: "attackers",
      eligible: [110],
    },
  );

  const secondAction = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ decisionId: 2, ids: [110] }),
    }),
  );
  assert.equal(secondAction.response.status, 200);
  assert.equal(secondAction.body.game.decision.kind, "blockers");

  const exported = await json(
    await request("/api/games/test-game/bug-report"),
  );
  assert.equal(exported.response.status, 200);
  const report = exported.body.report;
  assert.deepEqual(report.match.config.players, [
    { deckId: "atog", policyId: "human" },
    { deckId: "atog", policyId: "spz" },
  ]);
  assert.deepEqual(report.match.model, {
    family: "self-play-zero",
    generation: 0,
    searchWorlds: 4,
    horizonTurns: 1,
    source: "frozen-artifact",
    fingerprint:
      "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f",
  });
  assert.deepEqual(report.successfulHumanActions, [
    {
      sequence: 1,
      turnNumber: 13,
      phase: "first_main",
      decision: {
        decisionId: "report-priority-1",
        kind: "priority",
      },
      submission: {
        decisionId: "report-priority-1",
        index: 1,
      },
    },
    {
      sequence: 2,
      turnNumber: 13,
      phase: "declare_attackers",
      decision: { decisionId: 2, kind: "attackers" },
      submission: { decisionId: 2, ids: [110] },
    },
  ]);
  assert.deepEqual(
    {
      turnNumber: report.publicState.turnNumber,
      phase: report.publicState.phase,
      activePlayer: report.publicState.activePlayer,
      priority: report.publicState.priority,
    },
    {
      turnNumber: 13,
      phase: "declare_blockers",
      activePlayer: 0,
      priority: { holder: "none", seat: null },
    },
  );
  assert.deepEqual(
    report.publicState.players.map(
      ({
        seat,
        life,
        handCount,
        libraryCount,
        graveyardCount,
        exileCount,
      }) => ({
        seat,
        life,
        handCount,
        libraryCount,
        graveyardCount,
        exileCount,
      }),
    ),
    [
      {
        seat: 0,
        life: 17,
        handCount: 2,
        libraryCount: 24,
        graveyardCount: 1,
        exileCount: 1,
      },
      {
        seat: 1,
        life: 14,
        handCount: 6,
        libraryCount: 23,
        graveyardCount: 1,
        exileCount: 1,
      },
    ],
  );
  assert.equal(
    report.publicState.players[1].battlefield[1].card.name,
    "Flying Men",
  );
  assert.deepEqual(
    report.publicState.stack.map(
      ({ stackId, controller, card, spellTarget }) => ({
        stackId,
        controller,
        card: card.name,
        ...(spellTarget === undefined ? {} : { spellTarget }),
      }),
    ),
    [
      {
        stackId: 301,
        controller: 0,
        card: "Flying Men",
      },
      {
        stackId: 302,
        controller: 1,
        card: "Counterspell",
        spellTarget: 301,
      },
    ],
  );
  assert.deepEqual(report.currentDecision, {
    decisionId: "report-blockers-3",
    kind: "blockers",
    attackers: [110],
    choices: [{ blocker: 210, legalAttackers: [110] }],
  });
  assert.deepEqual(
    report.publicChronicle.map(({ sequence, message, kind }) => ({
      sequence,
      message,
      kind,
    })),
    [
      {
        sequence: 1,
        message: "Opponent: Cast Counterspell → Flying Men",
        kind: "priority_action",
      },
      {
        sequence: 2,
        message: "You: Cast Counterspell → stack #302",
        kind: "priority_action",
      },
      {
        sequence: 3,
        message: "You attacked with Flying Men #110",
        kind: "attackers_declared",
      },
    ],
  );

  const serialized = JSON.stringify(report);
  for (const secret of [
    "OPPONENT_HAND_SECRET",
    "OPPONENT_LIBRARY_SECRET",
    "HUMAN_GRAVEYARD_SECRET",
    "OPPONENT_GRAVEYARD_SECRET",
    "HUMAN_EXILE_SECRET",
    "OPPONENT_EXILE_SECRET",
    "INCIDENTAL_EVENT_SECRET",
    "DECISION_SECRET",
    "DECISION_SECRET_AFTER_ACTION",
    "RAW_STATE_SECRET",
    "STACK_SECRET",
    "BRIDGE_STDERR_SECRET",
  ]) {
    assert.doesNotMatch(serialized, new RegExp(secret));
  }
  assert.doesNotMatch(serialized, /"hand":|"revealedHand":|"library":/);

  const blocks = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "report-blockers-3",
        pairs: [[110, 210]],
      }),
    }),
  );
  assert.equal(blocks.response.status, 200);
  assert.deepEqual(
    (await json(await request("/api/games/test-game/bug-report"))).body.report
      .currentDecision,
    {
      decisionId: "report-damage-order-4",
      kind: "damage_order",
      attacker: 110,
      blockers: [210],
    },
  );

  const damageOrder = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "report-damage-order-4",
        ids: [210],
      }),
    }),
  );
  assert.equal(damageOrder.response.status, 200);
  assert.deepEqual(
    (await json(await request("/api/games/test-game/bug-report"))).body.report
      .currentDecision,
    {
      decisionId: "report-cleanup-5",
      kind: "cleanup_discard",
      count: 1,
      options: [
        {
          index: 0,
          card: {
            id: "card-counterspell",
            name: "Counterspell",
            type: "instant",
            costLabel: "UU",
          },
        },
        {
          index: 1,
          card: {
            id: "card-flying-men",
            name: "Flying Men",
            type: "creature",
            costLabel: "U",
            power: 1,
            toughness: 1,
            flying: true,
          },
        },
      ],
    },
  );

  const cleanup = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "report-cleanup-5",
        indices: [1],
      }),
    }),
  );
  assert.equal(cleanup.response.status, 200);
  assert.equal(cleanup.body.game.status, "finished");

  const finalReport = (
    await json(await request("/api/games/test-game/bug-report"))
  ).body.report;
  assert.deepEqual(
    finalReport.successfulHumanActions.slice(2).map(
      ({ decision, submission }) => ({ decision, submission }),
    ),
    [
      {
        decision: {
          decisionId: "report-blockers-3",
          kind: "blockers",
        },
        submission: {
          decisionId: "report-blockers-3",
          pairs: [[110, 210]],
        },
      },
      {
        decision: {
          decisionId: "report-damage-order-4",
          kind: "damage_order",
        },
        submission: {
          decisionId: "report-damage-order-4",
          ids: [210],
        },
      },
      {
        decision: {
          decisionId: "report-cleanup-5",
          kind: "cleanup_discard",
        },
        submission: {
          decisionId: "report-cleanup-5",
          indices: [1],
        },
      },
    ],
  );

  const wrongMethod = await request(
    "/api/games/test-game/bug-report",
    { method: "POST", body: "{}" },
  );
  assert.equal(wrongMethod.status, 405);
  assert.equal(wrongMethod.headers.get("allow"), "GET");
});

test("bug reports omit validated actions whose bridge advance fails", async (t) => {
  const { request } = await startTestServer(t, FAKE_BUG_REPORT_BRIDGE);
  const created = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        players: [
          { deckId: "atog", policyId: "human" },
          { deckId: "atog", policyId: "spz" },
        ],
        seed: 42,
        debugReveal: false,
        bluffMode: false,
        rollouts: 2,
        deepRollouts: 8,
      }),
    }),
  );
  assert.equal(created.response.status, 201);

  const failedAdvance = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "report-priority-1",
        index: 0,
      }),
    }),
  );
  assert.equal(failedAdvance.response.status, 200);
  assert.equal(failedAdvance.body.game.status, "failed");

  const exported = await json(
    await request("/api/games/test-game/bug-report"),
  );
  assert.equal(exported.response.status, 200);
  assert.equal(exported.body.report.match.status, "failed");
  assert.deepEqual(exported.body.report.successfulHumanActions, []);
  assert.doesNotMatch(
    JSON.stringify(exported.body.report),
    /BRIDGE_STDERR_SECRET/,
  );
});

test("bug reports omit validated actions that receive no bridge response", async (t) => {
  const { request } = await startTestServer(
    t,
    FAKE_BUG_REPORT_TIMEOUT_BRIDGE,
    { actionTimeoutMs: 25 },
  );
  const created = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ seed: 42 }),
    }),
  );
  assert.equal(created.response.status, 201);
  assert.equal(created.body.game.decision.id, "timeout-priority-1");

  const timedOut = await json(
    await request("/api/games/test-game/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "timeout-priority-1",
        index: 0,
      }),
    }),
  );
  assert.equal(timedOut.response.status, 504);
  assert.equal(timedOut.body.error.code, "bridge_timeout");

  const exported = await json(
    await request("/api/games/test-game/bug-report"),
  );
  assert.equal(exported.response.status, 200);
  assert.equal(exported.body.report.match.status, "failed");
  assert.deepEqual(exported.body.report.successfulHumanActions, []);
});

test("bug-report action transcripts stay isolated across live sessions", async (t) => {
  let nextId = 0;
  const { request } = await startTestServer(
    t,
    FAKE_BUG_REPORT_BRIDGE,
    { idFactory: () => `report-game-${++nextId}` },
  );
  const gameConfig = {
    players: [
      { deckId: "atog", policyId: "human" },
      { deckId: "atog", policyId: "spz" },
    ],
    seed: 42,
  };
  const first = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(gameConfig),
    }),
  );
  const second = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ ...gameConfig, seed: 43 }),
    }),
  );
  assert.equal(first.response.status, 201);
  assert.equal(second.response.status, 201);
  assert.equal(first.body.game.id, "report-game-1");
  assert.equal(second.body.game.id, "report-game-2");

  const firstAction = await json(
    await request("/api/games/report-game-1/actions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        decisionId: "report-priority-1",
        index: 1,
      }),
    }),
  );
  assert.equal(firstAction.response.status, 200);

  const firstReport = (
    await json(await request("/api/games/report-game-1/bug-report"))
  ).body.report;
  const secondReport = (
    await json(await request("/api/games/report-game-2/bug-report"))
  ).body.report;
  assert.equal(firstReport.match.id, "report-game-1");
  assert.equal(secondReport.match.id, "report-game-2");
  assert.deepEqual(
    firstReport.successfulHumanActions.map(({ sequence, submission }) => ({
      sequence,
      submission,
    })),
    [
      {
        sequence: 1,
        submission: {
          decisionId: "report-priority-1",
          index: 1,
        },
      },
    ],
  );
  assert.deepEqual(secondReport.successfulHumanActions, []);
  assert.equal(secondReport.currentDecision.decisionId, "report-priority-1");
});

test("creates a session and maps canonical config to bridge flags", async (t) => {
  const { request } = await startTestServer(t);
  const { response, body } = await json(
    await request("/api/games", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        players: [
          { deckId: "atog", policyId: "human" },
          { deckId: "white-weenie", policyId: "monte-carlo" },
        ],
        seed: "18446744073709551615",
        debugReveal: true,
        bluffMode: true,
        rollouts: 3,
        deepRollouts: 9,
      }),
    }),
  );

  assert.equal(response.status, 201);
  assert.equal(body.game.id, "test-game");
  assert.equal(body.game.status, "awaiting_action");
  assert.equal(body.game.decision.id, "priority-1");
  assert.equal(body.game.decision.kind, "priority");
  assert.deepEqual(body.game.snapshot.received, {
    humanDeck: "atog",
    opponentDeck: "white-weenie",
    opponentPolicy: "monte-carlo",
    seed: "18446744073709551615",
    rollouts: "3",
    deepRollouts: "9",
    debugReveal: true,
    bluffMode: true,
  });
  assert.deepEqual(body.game.snapshot.players[1], {
    handSize: 7,
    life: 20,
  });
  assert.equal(body.game.model, null);

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
        { deckId: "rg-berserk", policyId: "human" },
        { deckId: "br-midrange", policyId: "handcrafted" },
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
        { deckId: "br-midrange", policyId: "random" },
      ],
    },
    {
      players: [
        { deckId: "rg-berserk", policyId: "human" },
        { deckId: "br-midrange", policyId: "oracle" },
      ],
    },
    { seed: "18446744073709551616" },
    { rollouts: 0 },
    { deepRollouts: 4_097 },
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
