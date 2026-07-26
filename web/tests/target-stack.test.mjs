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

async function loadTargetFormatter() {
  const typesSource = await source("src/types.ts");
  const compiled = ts.transpileModule(typesSource, {
    compilerOptions: {
      module: ts.ModuleKind.ESNext,
      target: ts.ScriptTarget.ES2022,
    },
  }).outputText;
  const encoded = Buffer.from(compiled).toString("base64");
  return import(`data:text/javascript;base64,${encoded}`);
}

test("production structured targets have stable readable labels", async () => {
  const {
    concisePriorityOptionLabel,
    describeTopOfStack,
    formatStackEntryLabel,
    formatTargetLabel,
    priorityOptionsForCard,
    stackPermanentTargetIds,
  } = await loadTargetFormatter();

  // Fixed reproduction: game seed 42, RU Aggro (human) into Blue. The
  // production bridge orders the bottom of the stack first and the next
  // object to resolve last.
  const targetedStack = [
    {
      stackId: 41,
      kind: "spell",
      card: { id: "card-5", name: "Lightning Bolt" },
      xValue: 0,
      target: { player: 1, creature: 73, label: "Air Elemental #73" },
    },
    {
      stackId: 42,
      kind: "spell",
      card: { id: "card-3", name: "Giant Growth" },
      xValue: 0,
      target: { player: 0, creature: 18, label: "Grizzly Bears #18" },
    },
    {
      stackId: 43,
      kind: "spell",
      card: { id: "card-6", name: "Disintegrate" },
      xValue: 4,
      target: { player: 1, label: "Opponent" },
    },
    {
      stackId: 44,
      kind: "spell",
      card: { id: "card-19", name: "Braingeyser" },
      label: "Braingeyser → You",
      xValue: 3,
      target: { player: 0, label: "You" },
    },
  ];
  assert.deepEqual(
    targetedStack.map(({ card }) => card.name),
    ["Lightning Bolt", "Giant Growth", "Disintegrate", "Braingeyser"],
  );
  assert.deepEqual(
    targetedStack.map(({ target }) => formatTargetLabel(target)),
    ["Air Elemental #73", "Grizzly Bears #18", "Opponent", "You"],
  );

  assert.equal(
    formatTargetLabel({ player: 0, creature: 18 }),
    "Creature #18",
  );
  assert.equal(formatTargetLabel({ player: 0 }), "You");
  assert.equal(formatTargetLabel({ player: 1 }), "Opponent");
  assert.equal(formatTargetLabel("Opponent"), "Opponent");
  assert.equal(formatTargetLabel({ unexpected: true }), "Unknown target");
  assert.equal(formatTargetLabel("   "), null);
  assert.equal(formatTargetLabel(null), null);
  assert.equal(
    concisePriorityOptionLabel({
      index: 1,
      label: "Cast Giant Growth → Grizzly Bears #110",
      kind: "cast_giant_growth",
      target: { player: 0, creature: 110, label: "Grizzly Bears #110" },
    }),
    "Cast Giant Growth",
  );
  assert.equal(
    concisePriorityOptionLabel({
      index: 2,
      label: "Cast Counterspell → stack #302",
      kind: "cast_counterspell",
      spellTarget: 302,
    }),
    "Cast Counterspell",
  );
  assert.equal(
    concisePriorityOptionLabel({
      index: 0,
      label: "Pass priority",
      kind: "pass",
    }),
    "Pass priority",
  );

  assert.deepEqual(describeTopOfStack(targetedStack), {
    label: "Braingeyser",
    targets: ["You"],
    summary: "Braingeyser targeting You is next to resolve.",
  });
  assert.deepEqual(stackPermanentTargetIds(targetedStack), ["73", "18"]);
  assert.equal(
    formatStackEntryLabel({
      kind: "activated_ability",
      card: { id: "card-20", name: "Millstone" },
      label: "Millstone → Opponent",
    }),
    "Millstone ability",
  );

  const giantGrowthDecision = {
    kind: "priority",
    decisionId: "target-stack-priority-1",
    options: [
      { index: 0, label: "Pass priority", kind: "pass" },
      {
        index: 3,
        label: "Cast Giant Growth → Grizzly Bears #110",
        kind: "cast",
        card: { id: "card-3", name: "Giant Growth" },
        target: { player: 0, creature: 110, label: "Grizzly Bears #110" },
      },
      {
        index: 4,
        label: "Cast Giant Growth → Ironclaw Orcs #210",
        kind: "cast",
        card: { id: "card-3", name: "Giant Growth" },
        target: { player: 1, creature: 210, label: "Ironclaw Orcs #210" },
      },
      {
        index: 5,
        label: "Cast Lightning Bolt → Grizzly Bears #110",
        kind: "cast",
        card: { id: "card-5", name: "Lightning Bolt" },
      },
    ],
  };
  assert.deepEqual(
    priorityOptionsForCard(giantGrowthDecision, "card-3").map(
      (option) => option.index,
    ),
    [3, 4],
    "one dragged card definition must expose every exact target-specific option",
  );
  assert.deepEqual(priorityOptionsForCard(undefined, "card-3"), []);
});

test("stack, priority, and battlefield rendering share bridge-shaped targets", async () => {
  const [app, css] = await Promise.all([
    source("src/App.tsx"),
    source("src/styles.css"),
  ]);

  assert.match(app, /formatStackTargets\(entry\)/);
  assert.match(
    app,
    /key=\{entry\.stackId \?\? entry\.id \?\?/,
  );
  assert.match(app, /formatTargetLabel\(option\.target\)/);
  assert.match(app, /option\.spellTarget/);
  assert.match(app, /describeTopOfStack\(state\.stack \?\? \[\]\)/);
  assert.match(app, /"Respond to the stack"/);
  assert.match(
    app,
    /continue toward resolving \$\{stackInteraction\.label\}/,
  );
  assert.match(app, /concisePriorityOptionLabel\(option\)/);
  assert.match(app, /target\s*\?\s*`Target → \$\{target\}`/);
  assert.match(css, /\.action-card\.no-card\s*\{/);
  assert.match(app, /stackPermanentTargetIds\(stack\)/);
  assert.equal(
    app.match(/targetedPermanentIds=\{targetedPermanentIds\}/g)?.length,
    2,
    "both battlefields must receive the public stack target set",
  );
  assert.match(app, /targeted \? "is-targeted" : ""/);
  assert.match(app, />TARGET<\/span>/);
  assert.match(css, /\.card-face\.is-targeted\s*\{[^}]*outline:/s);
  assert.match(css, /\.status-targeted\s*\{/);
  assert.match(css, /\.stack-choice-context\s*\{/);
  assert.doesNotMatch(app, /\{entry\.target\}/);
  assert.doesNotMatch(app, /entry\.targets\?\.join/);
});

test("playable hand cards inspect safely and drag onto exact engine options", async () => {
  const [app, css] = await Promise.all([
    source("src/App.tsx"),
    source("src/styles.css"),
  ]);
  const inspector = app.slice(
    app.indexOf("function CardInspector"),
    app.indexOf("function PriorityControls"),
  );
  const priorityControls = app.slice(
    app.indexOf("function PriorityControls"),
    app.indexOf("function AttackersControls"),
  );
  const dragHandler = app.slice(
    app.indexOf("const startHandCardDrag"),
    app.indexOf("const inspectedCardOptions"),
  );

  assert.match(app, /draggable=\{playable && Boolean\(onCardDragStart\)\}/);
  assert.match(
    dragHandler,
    /priorityOptionsForCard\(priorityDecision, card\.id\)\.length === 0/,
  );
  assert.doesNotMatch(
    dragHandler,
    /card\.name/,
    "drag legality must come from engine options, not card-name knowledge",
  );

  assert.match(inspector, /role="dialog"/);
  assert.match(inspector, /aria-modal="true"/);
  assert.match(inspector, /Inspecting never plays a card/);
  assert.match(inspector, /onFocusOption\(option\)/);
  assert.doesNotMatch(
    inspector,
    /onSubmit/,
    "inspection links focus actions and never submit them",
  );
  assert.match(app, /event\.key === "Escape" && inspectedHandCard/);
  const inspectorShortcutGuard = app.indexOf("!inspectedHandCard");
  const numericShortcut = app.indexOf('/^[1-9]$/.test', inspectorShortcutGuard);
  assert.ok(
    inspectorShortcutGuard >= 0 && numericShortcut > inspectorShortcutGuard,
    "the open inspector must suppress numeric action shortcuts",
  );

  assert.match(
    priorityControls,
    /String\(option\.card\.id\) === draggedCardId/,
  );
  assert.match(
    priorityControls,
    /onDrop=[\s\S]+?index: option\.index/,
    "dropping must submit the exact engine option, including its target",
  );
  assert.match(priorityControls, />DROP TO PLAY<\/span>/);
  assert.match(css, /\.action-card\.is-drop-target\s*\{/);
  assert.match(css, /\.drop-cue\s*\{/);
  assert.match(css, /\.card-inspect-face \.card-face\s*\{[^}]*width:\s*220px;[^}]*height:\s*306px;/s);
});

test("browser selections preserve opaque numeric IDs in every combat request", async () => {
  const { blockerPairsFromKeys, restoreOpaqueIds } =
    await loadTargetFormatter();
  const app = await source("src/App.tsx");

  assert.deepEqual(restoreOpaqueIds(new Set(["101"]), [101, 102]), [101]);
  assert.deepEqual(restoreOpaqueIds(new Set(["bear-a"]), ["bear-a"]), [
    "bear-a",
  ]);
  assert.deepEqual(
    blockerPairsFromKeys(
      { 102: "202" },
      [{ blocker: 102, legalAttackers: [202] }],
    ),
    [[202, 102]],
  );
  assert.deepEqual(
    blockerPairsFromKeys(
      { "bear-a": "orc-b" },
      [{ blocker: "bear-a", legalAttackers: ["orc-b"] }],
    ),
    [["orc-b", "bear-a"]],
  );

  assert.match(
    app,
    /ids: restoreOpaqueIds\(selected, decision\.eligible\)/,
  );
  assert.match(
    app,
    /blockerPairsFromKeys\(assignments, decision\.choices\)/,
  );
  assert.match(app, /\? \[\.\.\.decision\.blockers\]\s*: \[\]/);
  assert.match(app, /decisionId: decision\.decisionId, ids: order/);
  assert.doesNotMatch(app, /decision\.blockers\.map\(\(id\) => String\(id\)\)/);
});
