import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import ts from "typescript";
import { fileURLToPath } from "node:url";

const WEB_ROOT = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "..",
);

async function source(relativePath) {
  return readFile(path.join(WEB_ROOT, relativePath), "utf8");
}

async function loadTypeScriptModule(relativePath) {
  const moduleSource = await source(relativePath);
  const compiled = ts.transpileModule(moduleSource, {
    compilerOptions: {
      module: ts.ModuleKind.ESNext,
      target: ts.ScriptTarget.ES2022,
    },
  }).outputText;
  return import(`data:text/javascript;base64,${Buffer.from(compiled).toString("base64")}`);
}

test("game results use player-facing titles and readable reasons", async () => {
  const { formatGameResultReason, formatGameResultTitle } =
    await loadTypeScriptModule("src/types.ts");

  assert.equal(formatGameResultTitle("finished", 0, 0), "You won");
  assert.equal(formatGameResultTitle("finished", 1, 0), "Opponent won");
  assert.equal(formatGameResultTitle("finished", null, 0), "The match is a draw");
  assert.equal(formatGameResultTitle("error", 0, 0), "Match interrupted");

  assert.equal(
    formatGameResultReason("life_total"),
    "The losing player’s life total reached zero.",
  );
  assert.equal(
    formatGameResultReason("life"),
    "The losing player’s life total reached zero.",
  );
  assert.equal(
    formatGameResultReason("empty_library"),
    "The losing player tried to draw from an empty library.",
  );
  assert.equal(
    formatGameResultReason("turn_limit"),
    "The match reached the turn limit.",
  );
  assert.equal(
    formatGameResultReason("custom_bridge_reason"),
    "Match ended: Custom bridge reason.",
  );
  assert.equal(formatGameResultReason(null), null);
});

test("API errors retain status, code, details, and readable fallback text", async () => {
  const { ApiRequestError, apiRequestErrorFromResponse } =
    await loadTypeScriptModule("src/errors.ts");

  const stale = apiRequestErrorFromResponse(409, {
    error: {
      code: "stale_decision",
      message: "That choice belongs to an old decision",
      details: { expectedDecisionId: 7 },
    },
  });
  assert.ok(stale instanceof ApiRequestError);
  assert.equal(stale.status, 409);
  assert.equal(stale.code, "stale_decision");
  assert.equal(stale.message, "That choice belongs to an old decision");
  assert.deepEqual(stale.details, { expectedDecisionId: 7 });

  const plain = apiRequestErrorFromResponse(503, "offline");
  assert.equal(plain.status, 503);
  assert.equal(plain.code, "http_error");
  assert.equal(plain.message, "Request failed (503)");
});

test("create and action paths are synchronously single-flight and stale-safe", async () => {
  const app = await source("src/App.tsx");
  const startGame = app.slice(
    app.indexOf("const startGame = useCallback"),
    app.indexOf("useEffect(() => {", app.indexOf("const startGame = useCallback")),
  );
  const act = app.slice(
    app.indexOf("const act = useCallback"),
    app.indexOf("useEffect(() => {", app.indexOf("const act = useCallback")),
  );
  const gameOver = app.slice(
    app.indexOf("function GameOver"),
    app.indexOf("export default function App"),
  );

  assert.ok(
    startGame.indexOf("if (creatingRef.current) return") <
      startGame.indexOf("createGame(config)"),
  );
  assert.ok(
    startGame.indexOf("creatingRef.current = true") <
      startGame.indexOf("createGame(config)"),
  );
  assert.match(
    startGame,
    /\.finally\(\(\) => \{[\s\S]*creatingRef\.current = false;[\s\S]*setCreating\(false\)/,
  );

  assert.ok(
    act.indexOf("if (!snapshot || actingRef.current) return") <
      act.indexOf("submitAction(gameId, action)"),
  );
  assert.ok(
    act.indexOf("actingRef.current = true") <
      act.indexOf("submitAction(gameId, action)"),
  );
  assert.match(
    act,
    /"stale_decision", "not_awaiting_action", "game_over"/,
  );
  assert.match(act, /setSnapshot\(await fetchGame\(gameId\)\)/);
  assert.match(
    act,
    /\.finally\(\(\) => \{[\s\S]*actingRef\.current = false;[\s\S]*setActing\(false\)/,
  );

  assert.match(gameOver, /formatGameResultTitle\(/);
  assert.match(gameOver, /formatGameResultReason\(/);
  assert.doesNotMatch(gameOver, /Victory at the near seat/);
  assert.doesNotMatch(gameOver, /Victory across the table/);
  assert.equal(gameOver.match(/disabled=\{busy\}/g)?.length, 2);
  assert.match(gameOver, /Preparing rematch…/);
  assert.match(app, /<GameOver[\s\S]+?busy=\{creating\}/);
});
