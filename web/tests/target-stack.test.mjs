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
    describeTopOfStack,
    formatStackEntryLabel,
    formatTargetLabel,
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
