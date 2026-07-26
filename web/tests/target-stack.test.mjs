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
  const { formatTargetLabel } = await loadTargetFormatter();

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
});

test("stack and priority rendering consume bridge-shaped targets and IDs", async () => {
  const app = await source("src/App.tsx");

  assert.match(app, /formatTargetLabel\(entry\.target\)/);
  assert.match(app, /\.map\(formatTargetLabel\)/);
  assert.match(app, /entry\.spellTarget/);
  assert.match(
    app,
    /key=\{entry\.stackId \?\? entry\.id \?\?/,
  );
  assert.match(app, /formatTargetLabel\(option\.target\)/);
  assert.match(app, /option\.spellTarget/);
  assert.doesNotMatch(app, /\{entry\.target\}/);
  assert.doesNotMatch(app, /entry\.targets\?\.join/);
});
