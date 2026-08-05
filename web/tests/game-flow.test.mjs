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

test("policy version dates render deterministically without locale drift", async () => {
  const { formatPolicyVersionDate } =
    await loadTypeScriptModule("src/types.ts");

  assert.equal(formatPolicyVersionDate("2026-07-26"), "Jul 26, 2026");
  assert.equal(formatPolicyVersionDate("2026-07-24"), "Jul 24, 2026");
  assert.equal(formatPolicyVersionDate("rolling"), "rolling");
  assert.equal(formatPolicyVersionDate("2026-13-01"), "2026-13-01");
});

test("evolution result helpers preserve the exact manifest and percent scale", async () => {
  const { evolvedDeckCardCount, formatEvolutionPercent } =
    await loadTypeScriptModule("src/types.ts");

  assert.equal(
    evolvedDeckCardCount([
      { id: 1, name: "Island", count: 15 },
      { id: 2, name: "Flying Men", count: 4 },
      { id: 3, name: "Counterspell", count: 8 },
      { id: 4, name: "Other", count: 13 },
    ]),
    40,
  );
  assert.equal(formatEvolutionPercent(62.456), "62.5%");
  assert.equal(formatEvolutionPercent(Number.NaN), "0.0%");
});

test("reproduction summaries keep setup stable while public context advances", async () => {
  const {
    formatReproductionSummary,
    latestPublicEventMessage,
    reproductionPriorityHolder,
  } = await loadTypeScriptModule("src/types.ts");

  const config = {
    players: [
      { deckId: "rg-berserk", policyId: "human" },
      { deckId: "atog", policyId: "spz" },
    ],
    seed: "18446744073709551615",
    rollouts: 2,
    deepRollouts: 8,
    bluffMode: true,
    debugReveal: false,
  };
  const first = formatReproductionSummary(config, {
    turnNumber: 1,
    phase: "first_main",
    priorityHolder: "You",
    latestEvent: "You started turn 1",
    model: {
      family: "self-play-zero",
      generation: 0,
      searchWorlds: 3,
      horizonTurns: 1,
      source: "frozen-artifact",
      fingerprint:
        "0000000000000000000000000000000000000000000000000000000000000000",
    },
    opponentHand: ["Counterspell"],
  });
  const advanced = formatReproductionSummary(config, {
    turnNumber: 3,
    phase: "declare_attackers",
    priorityHolder: "None",
    latestEvent: "You declared 2 attacker(s)",
    model: {
      family: "self-play-zero",
      generation: 0,
      searchWorlds: 3,
      horizonTurns: 1,
      source: "frozen-artifact",
      fingerprint:
        "0000000000000000000000000000000000000000000000000000000000000000",
    },
    opponentHand: ["Lightning Bolt"],
  });

  assert.equal(
    first,
    'you=rg-berserk/human | opponent=atog/spz | game-seed=18446744073709551615 | rollouts=2 | deep-rollouts=8 | model=self-play-zero/C0 | model-fingerprint=0000000000000000000000000000000000000000000000000000000000000000 | model-search=K3/H1 | bluff=on | reveal=off | turn=1 | phase=first_main | priority-holder=You | latest-event="You started turn 1"',
  );
  assert.equal(
    advanced,
    'you=rg-berserk/human | opponent=atog/spz | game-seed=18446744073709551615 | rollouts=2 | deep-rollouts=8 | model=self-play-zero/C0 | model-fingerprint=0000000000000000000000000000000000000000000000000000000000000000 | model-search=K3/H1 | bluff=on | reveal=off | turn=3 | phase=declare_attackers | priority-holder=None | latest-event="You declared 2 attacker(s)"',
  );
  assert.equal(
    first.slice(0, first.indexOf(" | turn=")),
    advanced.slice(0, advanced.indexOf(" | turn=")),
  );
  assert.doesNotMatch(`${first}\n${advanced}`, /Counterspell|Lightning Bolt/);
  assert.equal(reproductionPriorityHolder("priority", true), "You");
  for (const kind of [
    "attackers",
    "blockers",
    "damage_order",
    "cleanup_discard",
  ]) {
    assert.equal(reproductionPriorityHolder(kind, true), "None");
  }
  assert.equal(reproductionPriorityHolder(undefined, false), "Unknown");
  assert.match(
    formatReproductionSummary(config, {
      priorityHolder: "Unknown",
    }),
    / \| turn=unknown \| phase=unknown \| priority-holder=Unknown \| latest-event=none$/,
  );

  assert.equal(
    latestPublicEventMessage([
      {
        message: "Opponent discarded 2 cards",
        cards: [{ name: "Ancestral Recall" }],
      },
    ]),
    "Opponent discarded 2 cards",
  );
  assert.equal(
    latestPublicEventMessage([
      { kind: "private_payload_only", cards: [{ name: "Ancestral Recall" }] },
    ]),
    undefined,
  );
});

test("bug-report copy ignores a deferred response after its snapshot is invalidated", async () => {
  const { performBugReportCopy } = await loadTypeScriptModule(
    "src/bug-report-copy.ts",
  );
  let resolveOldReport;
  const oldReport = new Promise((resolve) => {
    resolveOldReport = resolve;
  });
  let generation = 1;
  const oldController = new AbortController();
  const copied = [];
  const states = [];
  const staleCopy = performBugReportCopy({
    gameId: "old-session",
    generation,
    signal: oldController.signal,
    currentGeneration: () => generation,
    fetchReport: () => oldReport,
    copyText: async (text) => {
      copied.push(text);
    },
    updateState: (state) => {
      states.push(state);
    },
  });

  generation += 1;
  oldController.abort();
  resolveOldReport({ match: { id: "old-session" } });

  assert.equal(await staleCopy, "stale");
  assert.deepEqual(copied, []);
  assert.deepEqual(states, []);

  const currentController = new AbortController();
  const currentCopy = await performBugReportCopy({
    gameId: "current-session",
    generation,
    signal: currentController.signal,
    currentGeneration: () => generation,
    fetchReport: async (id) => ({ match: { id } }),
    copyText: async (text) => {
      copied.push(text);
    },
    updateState: (state) => {
      states.push(state);
    },
  });

  assert.equal(currentCopy, "copied");
  assert.deepEqual(states, ["copied"]);
  assert.equal(copied.length, 1);
  assert.match(copied[0], /"id": "current-session"/);
  assert.doesNotMatch(copied[0], /old-session/);
});

test("clipboard copy falls back after API rejection and always removes its field", async () => {
  const { copyTextToClipboard } = await loadTypeScriptModule(
    "src/bug-report-copy.ts",
  );
  const makeField = () => ({
    value: "",
    style: { position: "", opacity: "" },
    attributes: new Map(),
    selected: false,
    removed: false,
    setAttribute(name, value) {
      this.attributes.set(name, value);
    },
    select() {
      this.selected = true;
    },
    remove() {
      this.removed = true;
    },
  });
  const fallbackField = makeField();
  let appendedField = null;
  await copyTextToClipboard("fallback text", {
    writeText: async () => {
      throw new Error("permission denied");
    },
    createTextArea: () => fallbackField,
    appendTextArea: (field) => {
      appendedField = field;
    },
    execCopy: () => true,
  });

  assert.equal(appendedField, fallbackField);
  assert.equal(fallbackField.value, "fallback text");
  assert.equal(fallbackField.selected, true);
  assert.equal(fallbackField.removed, true);

  const failedField = makeField();
  await assert.rejects(
    copyTextToClipboard("uncopyable text", {
      createTextArea: () => failedField,
      appendTextArea: () => {},
      execCopy: () => false,
    }),
    /Clipboard copy is unavailable/,
  );
  assert.equal(failedField.removed, true);
});

test("event prose cannot fall back to serializing incidental payloads", async () => {
  const { formatPublicLogEntry, latestPublicEventMessage } =
    await loadTypeScriptModule("src/types.ts");
  const hiddenShaped = {
    kind: "private_payload_only",
    turn: 4,
    player: 1,
    cards: [{ name: "Ancestral Recall" }],
    hand: ["Counterspell"],
    graveyard: [{ name: "Lightning Bolt" }],
  };

  assert.deepEqual(formatPublicLogEntry("You started turn 1"), {
    message: "You started turn 1",
  });
  assert.deepEqual(
    formatPublicLogEntry({
      message: "You cast Grizzly Bears",
      text: "ignored lower-priority text",
      label: "ignored lower-priority label",
      turn: 3,
      player: 0,
      kind: "priority_action",
      actionKind: "cast_creature",
    }),
    {
      message: "You cast Grizzly Bears",
      turn: 3,
      player: 0,
      kind: "priority_action",
      actionKind: "cast_creature",
    },
  );
  assert.deepEqual(formatPublicLogEntry(hiddenShaped), {
    message: "Game event",
    turn: 4,
    player: 1,
    kind: "private_payload_only",
  });
  assert.doesNotMatch(
    JSON.stringify(formatPublicLogEntry(hiddenShaped)),
    /Ancestral Recall|Counterspell|Lightning Bolt|cards|hand|graveyard/,
  );
  assert.equal(latestPublicEventMessage([hiddenShaped]), undefined);

  const app = await source("src/App.tsx");
  const matchLog = app.slice(
    app.indexOf("function MatchLog"),
    app.indexOf("function StackRail"),
  );
  assert.match(
    matchLog,
    /chronicleEntries\(entries, showPriorityPasses\)/,
  );
  assert.doesNotMatch(matchLog, /JSON\.stringify|Object\.(?:entries|values)/);
});

test("Chronicle filters structured passes and groups the visible events by turn", async () => {
  const { chronicleEntries } =
    await loadTypeScriptModule("src/types.ts");
  const entries = [
    {
      message: "You started turn 3",
      turn: 3,
      kind: "turn_started",
    },
    {
      message: "Opaque pass prose",
      turn: 3,
      kind: "priority_action",
      actionKind: "pass",
    },
    {
      message: "You: Pass priority",
      turn: 3,
      kind: "priority_action",
      actionKind: "cast_creature",
    },
    {
      message: "Another opaque pass",
      turn: 4,
      kind: "priority_action",
      actionKind: "pass",
    },
    {
      message: "Resolved Grizzly Bears",
      turn: 4,
      kind: "stack_resolved",
    },
    {
      message: "Non-priority event with pass metadata",
      turn: 4,
      kind: "stack_resolved",
      actionKind: "pass",
    },
    {
      message: "Third opaque pass",
      turn: 5,
      kind: "priority_action",
      actionKind: "pass",
    },
    {
      message: "Opponent declared an attacker",
      turn: 5,
      kind: "attackers_declared",
    },
  ];

  const filtered = chronicleEntries(entries, false);
  assert.deepEqual(
    filtered.map(({ entry }) => entry.message),
    [
      "You started turn 3",
      "You: Pass priority",
      "Resolved Grizzly Bears",
      "Non-priority event with pass metadata",
      "Opponent declared an attacker",
    ],
  );
  assert.deepEqual(
    filtered.map(({ sourceIndex }) => sourceIndex),
    [0, 2, 4, 5, 7],
  );
  assert.deepEqual(
    filtered.map(({ startsTurn }) => startsTurn),
    [true, false, true, false, true],
  );

  const unfiltered = chronicleEntries(entries, true);
  assert.equal(unfiltered.length, entries.length);
  assert.deepEqual(
    unfiltered.map(({ startsTurn }) => startsTurn),
    [true, false, false, true, false, false, true, false],
  );
});

test("Chronicle exposes an unchecked accessible pass toggle and concise turn markers", async () => {
  const app = await source("src/App.tsx");
  const matchLog = app.slice(
    app.indexOf("function MatchLog"),
    app.indexOf("function StackRail"),
  );

  assert.match(
    matchLog,
    /const \[showPriorityPasses, setShowPriorityPasses\] = useState\(false\)/,
  );
  assert.match(
    matchLog,
    /chronicleEntries\(entries, showPriorityPasses\)/,
  );
  assert.match(matchLog, /type="checkbox"/);
  assert.match(matchLog, /<span>Show priority passes<\/span>/);
  assert.match(matchLog, /\{startsTurn && \(/);
  assert.match(
    matchLog,
    /String\(index \+ 1\)\.padStart\(2, "0"\)/,
  );
  assert.doesNotMatch(
    matchLog,
    /includes\([^)]*Pass priority|match\([^)]*Pass priority/,
  );
});

test("combat stats follow public card type instead of bridge zero defaults", async () => {
  const { hasPublicCombatStats } =
    await loadTypeScriptModule("src/types.ts");

  assert.equal(hasPublicCombatStats("creature", 0, 0), true);
  assert.equal(hasPublicCombatStats("Artifact Creature", 2, 2), true);
  assert.equal(hasPublicCombatStats("land", 0, 0), false);
  assert.equal(hasPublicCombatStats("instant", 0, 0), false);
  assert.equal(hasPublicCombatStats(undefined, 2, 1), true);
  assert.equal(hasPublicCombatStats(undefined, undefined, undefined), false);
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

test("in-flight actions show recoverable non-blocking opponent status", async () => {
  const [app, css] = await Promise.all([
    source("src/App.tsx"),
    source("src/styles.css"),
  ]);
  const act = app.slice(
    app.indexOf("const act = useCallback"),
    app.indexOf("useEffect(() => {", app.indexOf("const act = useCallback")),
  );
  const decisionDock = app.slice(
    app.indexOf("function DecisionDock"),
    app.indexOf("function DeckManifest"),
  );
  const thinking = app.slice(
    app.indexOf('{acting && snapshot.status === "playing"'),
    app.indexOf("</>", app.indexOf('{acting && snapshot.status === "playing"')),
  );

  assert.match(
    thinking,
    /className="opponent-thinking-banner"[\s\S]+?role="status"[\s\S]+?aria-live="polite"[\s\S]+?Opponent thinking…[\s\S]+?Resolving your action/,
  );
  assert.doesNotMatch(thinking, /phase|priority|decision\.kind/);
  assert.match(decisionDock, /aria-busy=\{busy\}/);
  assert.doesNotMatch(decisionDock, /decision-busy|Playing out the response/);

  const catchStart = act.indexOf(".catch");
  const finallyStart = act.indexOf(".finally");
  assert.ok(catchStart >= 0 && finallyStart > catchStart);
  assert.match(
    act.slice(finallyStart),
    /actingRef\.current = false;[\s\S]+?setActing\(false\)/,
  );
  assert.doesNotMatch(
    act.slice(catchStart, finallyStart),
    /setSnapshot\(null\)/,
    "ordinary request errors must retain the last legal controls",
  );
  assert.match(act, /setGameError\(error instanceof Error \? error\.message : String\(error\)\)/);
  assert.match(app, /className="toast toast-error"[\s\S]+?>\s*Dismiss\s*</);

  const bannerRule = css.match(
    /\.opponent-thinking-banner\s*\{([^}]+)\}/s,
  )?.[1];
  assert.ok(bannerRule, "missing opponent thinking banner CSS");
  assert.match(bannerRule, /position:\s*fixed/);
  assert.match(bannerRule, /pointer-events:\s*none/);
});

test("request failures are atomic dismissible alerts without replacing game state", async () => {
  const app = await source("src/App.tsx");
  const noSnapshot = app.slice(
    app.indexOf("if (!snapshot)"),
    app.indexOf("const state = snapshot.state"),
  );
  const activeToast = app.slice(
    app.lastIndexOf("{gameError && ("),
    app.indexOf("function ReproductionSummary"),
  );
  const act = app.slice(
    app.indexOf("const act = useCallback"),
    app.indexOf("useEffect(() => {", app.indexOf("const act = useCallback")),
  );

  assert.match(noSnapshot, /role=\{gameError \? "alert" : "status"\}/);
  assert.match(
    noSnapshot,
    /aria-live=\{gameError \? "assertive" : "polite"\}/,
  );
  assert.match(noSnapshot, /aria-atomic="true"/);
  assert.match(
    noSnapshot,
    /gameError \? \([\s\S]+?<span>\{gameError\}<\/span>[\s\S]+?>\s*Dismiss\s*</,
  );
  assert.match(
    noSnapshot,
    /creating && <span className="spinner" aria-hidden="true" \/>/,
  );
  assert.match(noSnapshot, /<span>Shuffling and training the selected pilot…<\/span>/);

  assert.match(activeToast, /className="toast toast-error"[\s\S]+?role="alert"/);
  assert.match(activeToast, /aria-atomic="true"/);
  assert.match(activeToast, /<span>\{gameError\}<\/span>[\s\S]+?>\s*Dismiss\s*</);
  assert.doesNotMatch(activeToast, /\.focus\(/);

  assert.doesNotMatch(
    act.slice(act.indexOf(".catch"), act.indexOf(".finally")),
    /setSnapshot\(null\)/,
  );
  assert.match(
    act.slice(act.indexOf(".finally")),
    /actingRef\.current = false;[\s\S]+?setActing\(false\)/,
  );
});

test("decision prompts and match results have narrow announcement semantics", async () => {
  const app = await source("src/App.tsx");
  const decisionDock = app.slice(
    app.indexOf("function DecisionDock"),
    app.indexOf("function DeckManifest"),
  );
  const dockOpeningTag = decisionDock.slice(
    decisionDock.indexOf("<section"),
    decisionDock.indexOf(">", decisionDock.indexOf("<section")) + 1,
  );
  const announcement = decisionDock.slice(
    decisionDock.indexOf('className="sr-only"'),
    decisionDock.indexOf("{decision.kind !=="),
  );
  const gameOver = app.slice(
    app.indexOf("function GameOver"),
    app.indexOf("type PriorityOriginSelection"),
  );

  assert.doesNotMatch(dockOpeningTag, /aria-live/);
  assert.match(announcement, /role="status"/);
  assert.match(announcement, /aria-live="polite"/);
  assert.match(announcement, /aria-atomic="true"/);
  assert.match(announcement, /key=\{String\(decision\.decisionId\)\}/);
  assert.match(announcement, /state\.turnNumber/);
  assert.match(announcement, /formatPhase\(state\.phase\)/);
  assert.match(announcement, /\{heading\}/);
  assert.match(announcement, /\{helper\}/);
  assert.doesNotMatch(
    announcement,
    /\.players|\.hand|revealedHand|graveyard|library/,
  );

  assert.match(
    gameOver,
    /role="dialog"[\s\S]+?aria-modal="true"[\s\S]+?aria-labelledby="game-result-title"[\s\S]+?aria-describedby="game-result-reason"/,
  );
  assert.match(gameOver, /<h1 id="game-result-title">\{title\}<\/h1>/);
  assert.match(gameOver, /<p id="game-result-reason">/);
});

test("result dialog receives focus without trapping its explicit actions", async () => {
  const app = await source("src/App.tsx");
  const gameOver = app.slice(
    app.indexOf("function GameOver"),
    app.indexOf("type PriorityOriginSelection"),
  );
  const visibilityCheck = gameOver.indexOf("if (!visible) return null");

  assert.ok(gameOver.indexOf("const dialogRef = useRef") < visibilityCheck);
  assert.ok(gameOver.indexOf("useEffect(() =>") < visibilityCheck);
  assert.match(
    gameOver,
    /if \(visible\) dialogRef\.current\?\.focus\(\{ preventScroll: true \}\)/,
  );
  assert.match(gameOver, /\[snapshot\.id, visible\]/);
  assert.match(gameOver, /ref=\{dialogRef\}/);
  assert.match(gameOver, /tabIndex=\{-1\}/);
  assert.match(gameOver, /onKeyDown=\{keepResultOpen\}/);
  assert.match(
    gameOver,
    /event\.key !== "Escape"[\s\S]+?event\.preventDefault\(\)[\s\S]+?event\.stopPropagation\(\)/,
  );
  assert.equal(gameOver.match(/<button/g)?.length, 2);
  assert.match(gameOver, />\s*Change matchup\s*<\/button>/);
  assert.match(gameOver, /"Replay seed"/);
  assert.doesNotMatch(gameOver, /event\.key === "Tab"|querySelector.*button|autoFocus/);
});

test("new-match dialog owns focus and restores its exact opener", async () => {
  const app = await source("src/App.tsx");
  const setupDrawer = app.slice(
    app.indexOf("function SetupDrawer"),
    app.indexOf("function WelcomeTable"),
  );
  const visibilityCheck = setupDrawer.indexOf("if (!open) return null");

  assert.ok(setupDrawer.indexOf("const drawerRef = useRef") < visibilityCheck);
  assert.ok(
    setupDrawer.indexOf("const closeButtonRef = useRef") < visibilityCheck,
  );
  assert.ok(
    setupDrawer.indexOf("const restoreFocusRef = useRef") < visibilityCheck,
  );
  assert.match(
    setupDrawer,
    /restoreFocusRef\.current =[\s\S]+?document\.activeElement instanceof HTMLElement/,
  );
  assert.match(
    setupDrawer,
    /closeButtonRef\.current\?\.focus\(\{ preventScroll: true \}\)/,
  );
  assert.match(
    setupDrawer,
    /const restoreTarget = restoreFocusRef\.current[\s\S]+?restoreTarget\?\.isConnected[\s\S]+?restoreTarget\.focus\(\{ preventScroll: true \}\)/,
  );
  assert.match(setupDrawer, /\}, \[open\]\)/);

  assert.match(
    setupDrawer,
    /role="dialog"[\s\S]+?aria-modal="true"[\s\S]+?aria-labelledby="setup-dialog-title"[\s\S]+?onKeyDown=\{handleDialogKeyDown\}/,
  );
  assert.match(
    setupDrawer,
    /className="drawer-scrim"[\s\S]+?tabIndex=\{-1\}/,
  );
  assert.match(
    setupDrawer,
    /<h1 id="setup-dialog-title">Set the table<\/h1>/,
  );
  assert.match(
    setupDrawer,
    /className="icon-button"[\s\S]+?onClick=\{onClose\}[\s\S]+?ref=\{closeButtonRef\}/,
  );
  assert.doesNotMatch(setupDrawer, /aria-label="New match"/);

  assert.match(
    setupDrawer,
    /event\.key === "Escape"[\s\S]+?event\.preventDefault\(\)[\s\S]+?event\.stopPropagation\(\)[\s\S]+?onClose\(\)/,
  );
  assert.match(setupDrawer, /event\.key !== "Tab"/);
  assert.match(
    setupDrawer,
    /drawer\.querySelectorAll<HTMLElement>[\s\S]+?button:not\(\[disabled\]\):not\(\[tabindex="-1"\]\)[\s\S]+?select:not\(\[disabled\]\)/,
  );
  assert.match(
    setupDrawer,
    /event\.shiftKey && \(active === first \|\| !drawer\.contains\(active\)\)[\s\S]+?last\.focus\(\)/,
  );
  assert.match(
    setupDrawer,
    /!event\.shiftKey && active === last[\s\S]+?first\.focus\(\)/,
  );
});

test("deck evolution is a separate bounded session-only workflow", async () => {
  const [app, api, styles] = await Promise.all([
    source("src/App.tsx"),
    source("src/api.ts"),
    source("src/styles.css"),
  ]);
  const evolutionDialog = app.slice(
    app.indexOf("function EvolutionDialog"),
    app.indexOf("function WelcomeTable"),
  );
  const topBar = app.slice(app.indexOf("function TopBar"));

  assert.match(
    topBar,
    /className="evolve-deck-button"[\s\S]+?Evolve deck[\s\S]+?className="new-game-button"[\s\S]+?New match/,
  );
  assert.match(
    evolutionDialog,
    /role="dialog"[\s\S]+?aria-modal="true"[\s\S]+?aria-labelledby="evolution-dialog-title"[\s\S]+?aria-describedby="evolution-dialog-description"/,
  );
  assert.match(
    evolutionDialog,
    /<h1 id="evolution-dialog-title">Evolve a deck<\/h1>/,
  );
  assert.match(evolutionDialog, /event\.key === "Escape"/);
  assert.match(evolutionDialog, /event\.key !== "Tab"/);
  assert.match(evolutionDialog, /restoreFocusRef/);

  for (const field of [
    "seed",
    "generations",
    "population",
    "games",
  ]) {
    assert.match(
      evolutionDialog,
      new RegExp(`evolution\\.limits\\.${field}\\.min`),
    );
    assert.match(
      evolutionDialog,
      new RegExp(`evolution\\.limits\\.${field}\\.max`),
    );
  }
  assert.match(evolutionDialog, /createEvolution\(submitted\)/);
  assert.match(evolutionDialog, /result\.generations\.map/);
  assert.match(evolutionDialog, /result\.best\.cards\.map/);
  assert.match(evolutionDialog, /result\.best\.byOpponent\.map/);
  assert.match(evolutionDialog, /Aggregate fitness/);
  assert.match(evolutionDialog, /Save for this session/);
  assert.match(evolutionDialog, /maxLength=\{48\}/);
  assert.match(
    evolutionDialog,
    /className="button-primary evolution-generate"[\s\S]+?disabled=\{generating\}/,
  );
  assert.doesNotMatch(
    evolutionDialog,
    /disabled=\{[^}]*evolution\.active/,
  );
  assert.match(
    evolutionDialog,
    /disappears when the Node server restarts/,
  );
  assert.match(evolutionDialog, /meta\?\.decks\.filter\(\(deck\) => deck\.ephemeral\)/);
  assert.doesNotMatch(evolutionDialog, /localStorage|sessionStorage|indexedDB/);

  assert.match(api, /request<\{ evolution: EvolutionResult \}>\("\/api\/evolutions"/);
  assert.match(
    api,
    /`\/api\/evolutions\/\$\{encodeURIComponent\(id\)\}\/save`/,
  );
  assert.match(api, /body: JSON\.stringify\(\{ name \}\)/);
  assert.match(app, /const addSavedDeck = useCallback/);
  assert.match(
    app,
    /current\.decks\.filter\(\(candidate\) => candidate\.id !== deck\.id\)/,
  );

  assert.match(styles, /\.evolution-layer\s*\{[\s\S]+?z-index:\s*55/);
  assert.match(
    styles,
    /\.evolution-workspace\s*\{[\s\S]+?grid-template-columns:\s*300px minmax\(0,\s*1fr\)/,
  );
  assert.match(styles, /\.evolution-table-scroll\s*\{[\s\S]+?overflow-x:\s*auto/);
});
