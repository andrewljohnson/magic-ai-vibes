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
    formatStackController,
    formatStackEntryLabel,
    formatTargetLabel,
    priorityDestinationKey,
    priorityOptionsForCard,
    priorityOptionsForSourcePermanent,
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
  assert.equal(formatStackController(0, 0), "You");
  assert.equal(formatStackController(1, 0), "Opponent");
  assert.equal(formatStackController(1, 1), "You");
  assert.equal(formatStackController(0, 1), "Opponent");
  assert.equal(formatStackController(undefined, 0), null);
  assert.equal(formatStackController(2, 0), null);
  assert.equal(formatStackController(0, 2), null);
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
        label: "Play Forest",
        kind: "play_land",
        card: { id: "card-1", name: "Forest" },
      },
      {
        index: 6,
        label: "Cast Lightning Bolt → Opponent",
        kind: "cast",
        card: { id: "card-5", name: "Lightning Bolt" },
        target: { player: 1, label: "Opponent" },
      },
      {
        index: 7,
        label: "Cast Counterspell → stack #302",
        kind: "cast",
        card: { id: "card-13", name: "Counterspell" },
        spellTarget: 302,
      },
      {
        index: 8,
        label: "Activate Millstone → Opponent",
        kind: "activate",
        card: { id: "card-20", name: "Millstone" },
        sourcePermanent: 900,
        target: { player: 1, label: "Opponent" },
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
  assert.equal(
    priorityDestinationKey(giantGrowthDecision.options[1]),
    "permanent:110",
  );
  assert.equal(
    priorityDestinationKey(giantGrowthDecision.options[2]),
    "permanent:210",
  );
  assert.equal(priorityDestinationKey(giantGrowthDecision.options[3]), "play");
  assert.equal(
    priorityDestinationKey(giantGrowthDecision.options[4]),
    "player:1",
  );
  assert.equal(
    priorityDestinationKey(giantGrowthDecision.options[5]),
    "stack:302",
  );
  assert.deepEqual(
    priorityOptionsForCard(giantGrowthDecision, "card-20"),
    [],
    "a permanent activation must never masquerade as a hand-card action",
  );
  assert.deepEqual(
    priorityOptionsForSourcePermanent(giantGrowthDecision, 900).map(
      (option) => option.index,
    ),
    [8],
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
    /formatStackController\(\s*entry\.controller,\s*observerSeat,\s*\)/,
  );
  assert.match(app, /observerSeat=\{nearSeat\}/);
  assert.match(app, /aria-label=\{`Controlled by \$\{controller\}`\}/);
  assert.match(app, /\{controller\.toUpperCase\(\)\}/);
  assert.match(app, /const stackId = entry\.stackId \?\? entry\.id/);
  assert.match(app, /key=\{stackId \?\?/);
  assert.match(app, /formatTargetLabel\(option\.target\)/);
  assert.match(app, /option\.spellTarget/);
  assert.match(app, /describeTopOfStack\(state\.stack \?\? \[\]\)/);
  assert.match(app, /"Respond to the stack"/);
  assert.match(app, /className="stack-pass-button"/);
  assert.match(app, /concisePriorityOptionLabel\(option\)/);
  assert.match(app, /target\s*\?\s*`Target → \$\{target\}`/);
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
  assert.match(
    app,
    /priorityStackTargetIds\?\.has\(String\(stackId\)\)/,
  );
  assert.match(
    app,
    /choosePriorityDestination\(`stack:\$\{id\}`\)/,
  );
  assert.match(css, /\.stack-entry\.is-priority-destination\s*\{/);
  assert.match(css, /\.stack-surface-action\s*\{/);
  assert.match(css, /\.stack-controller\s*\{/);
  assert.match(css, /\.stack-controller-you\s*\{/);
  assert.match(css, /\.stack-controller-opponent\s*\{/);
  assert.doesNotMatch(app, /\{entry\.target\}/);
  assert.doesNotMatch(app, /entry\.targets\?\.join/);
});

test("priority origins route to exact Arena surfaces without a legal-action wall", async () => {
  const [app, css] = await Promise.all([
    source("src/App.tsx"),
    source("src/styles.css"),
  ]);
  const dragHandler = app.slice(
    app.indexOf("const startHandCardDrag"),
    app.indexOf("const selectPriorityPermanentOrigin"),
  );
  const routing = app.slice(
    app.indexOf("const playableHandCardIds"),
    app.indexOf("const toggleAttacker"),
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
  assert.match(routing, /priorityOptionsForCard\(priorityDecision, card\.id\)/);
  assert.match(
    routing,
    /priorityOptionsForSourcePermanent\(\s*priorityDecision,\s*permanentId/,
  );
  assert.match(routing, /priorityDestinationKey\(option\)/);
  assert.match(
    routing,
    /new Map<\s*PriorityDestinationKey,\s*PriorityOption\[\]/,
  );
  assert.match(
    routing,
    /priorityDestinationGroups\.get\(destination\) \?\? \[\]/,
  );
  assert.match(
    routing,
    /playOptions\.length === 1[\s\S]+?submitPriorityOption\(playOptions\[0\]\)/,
    "double-click may immediately submit only one exact targetless option",
  );
  assert.match(app, /data-priority-play-target=\{priorityPlayTarget \|\| undefined\}/);
  assert.match(app, /onCardDoubleClick=\{doubleClickHandCard\}/);
  assert.match(app, /choiceTarget=\{priorityDestination\}/);
  assert.match(app, /priorityTarget=\{priorityPlayerTargets\?\.has\(seat\)\}/);
  assert.match(
    app,
    /priorityStackTargetIds\?\.has\(String\(stackId\)\)/,
  );
  assert.match(app, /className="priority-parameter-chooser"/);
  assert.match(app, /index: option\.index/);
  assert.match(
    app,
    /const draggedPriorityOriginRef = useRef<PriorityOriginSelection \| null>/,
  );
  assert.match(
    routing,
    /draggedPriorityOriginRef\.current = origin;[\s\S]+?setDraggedPriorityOrigin\(origin\)/,
    "native drag legality must become synchronous before React renders highlights",
  );
  assert.match(
    app,
    /onDragOver=\{\(event\) => onPriorityDragOver\?\.\("play", event\)\}/,
  );
  assert.match(
    app,
    /onPriorityDragOver\?\.\(`permanent:\$\{id\}`, event\)/,
  );
  assert.match(
    routing,
    /matchingPriorityDropOptions\(origin, destination\)[\s\S]+?event\.preventDefault\(\)/,
  );
  assert.doesNotMatch(
    app,
    /target\.closest\("\\.permanent-zone"\)/,
    "the empty near battlefield and its non-button children must all accept play clicks",
  );
  assert.match(
    app,
    /decision\.kind !== "priority" &&[\s\S]+?className="decision-heading"/,
  );
  assert.match(css, /\.decision-dock\.is-priority\s*\{/);
  assert.match(
    css,
    /\.game-shell\.has-decision\.has-priority-decision\s*\{[^}]*--player-decision-height:\s*78px;/s,
  );
  assert.match(css, /\.player-side\.is-play-destination\s*\{/);
  assert.match(css, /\.card-face\.is-choice-target\s*\{/);
  assert.match(css, /\.surface-action\s*\{/);
  assert.doesNotMatch(app, /function CardInspector/);
  assert.doesNotMatch(app, /className="action-card/);
  assert.doesNotMatch(app, /play-cast-zone/);
  assert.doesNotMatch(app, />SELECTED</);
  assert.doesNotMatch(
    routing,
    /card\.name|card\.type/,
    "Arena routing must use structured engine fields, never card identity heuristics",
  );
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
  assert.deepEqual(
    blockerPairsFromKeys(
      { 31: "21", 32: "21" },
      [
        { blocker: 31, legalAttackers: [21] },
        { blocker: 32, legalAttackers: [21] },
      ],
    ),
    [
      [21, 31],
      [21, 32],
    ],
    "multiple blockers may converge on one attacker without stringifying IDs",
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
