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

async function loadTypesModule() {
  const typesSource = await readFile(
    path.join(WEB_ROOT, "src/types.ts"),
    "utf8",
  );
  const compiled = ts.transpileModule(typesSource, {
    compilerOptions: {
      module: ts.ModuleKind.ESNext,
      target: ts.ScriptTarget.ES2022,
    },
  }).outputText;
  const encoded = Buffer.from(compiled).toString("base64");
  return import(`data:text/javascript;base64,${encoded}`);
}

const DISINTEGRATE = {
  id: "card-8",
  name: "Disintegrate",
  type: "sorcery",
  cost: { generic: 0, red: 1 },
};

function disintegrateOption(index, xValue) {
  // Bridge convention: xValue is omitted when zero.
  const option = {
    index,
    label: `Cast Disintegrate (X=${xValue}) → Opponent`,
    kind: "cast_disintegrate",
    card: DISINTEGRATE,
    target: { player: 1, label: "Opponent" },
  };
  if (xValue !== 0) option.xValue = xValue;
  return option;
}

test("X-spell variants at one target collapse into a stepper group", async () => {
  const { xSpellGroupFromOptions, clampXValue } = await loadTypesModule();

  const options = [0, 1, 2, 3, 4].map((x) => disintegrateOption(x, x));
  const group = xSpellGroupFromOptions(options);
  assert.ok(group, "expected a group for uniform X variants");
  assert.equal(group.min, 0);
  assert.equal(group.max, 4, "max X must reflect payable mana");
  assert.equal(group.targetLabel, "Opponent");
  assert.equal(group.byX.get(3).index, 3);

  // Stepper arithmetic clamps to the legal, payable range.
  assert.equal(clampXValue(group, 9), 4);
  assert.equal(clampXValue(group, -5), 0);
  assert.equal(clampXValue(group, 2.6), 3);

  // Mixed targets must NOT collapse: X grouping is per destination.
  const mixed = [
    disintegrateOption(0, 1),
    {
      ...disintegrateOption(1, 1),
      target: { player: 1, creature: 7, label: "Su-Chi #7" },
    },
  ];
  assert.equal(xSpellGroupFromOptions(mixed), null);

  // Non-X kinds never group even when xValue is present (Chaos Orb and
  // mana taps reuse the field for other meanings).
  const taps = [
    {
      index: 0,
      label: "Tap City of Brass for {R}",
      kind: "tap_land_for_mana",
      card: DISINTEGRATE,
      sourcePermanent: 3,
      xValue: 2,
    },
    {
      index: 1,
      label: "Tap City of Brass for {U}",
      kind: "tap_land_for_mana",
      card: DISINTEGRATE,
      sourcePermanent: 3,
      xValue: 3,
    },
  ];
  assert.equal(xSpellGroupFromOptions(taps), null);
});

test("tutor options sort like a deck list and require chosen cards", async () => {
  const { tutorChoicesFromOptions } = await loadTypesModule();

  const tutor = (index, chosenCard) => ({
    index,
    label: "Cast Demonic Tutor",
    kind: "cast_demonic_tutor",
    card: { id: "card-70", name: "Demonic Tutor", type: "sorcery" },
    chosenCard,
  });
  const options = [
    tutor(0, {
      id: "card-1",
      name: "Swamp",
      type: "land",
      cost: {},
    }),
    tutor(1, {
      id: "card-2",
      name: "Juzam Djinn",
      type: "creature",
      cost: { generic: 2, black: 2 },
    }),
    tutor(2, {
      id: "card-3",
      name: "Hypnotic Specter",
      type: "creature",
      cost: { generic: 1, black: 2 },
    }),
    tutor(3, {
      id: "card-4",
      name: "Lightning Bolt",
      type: "instant",
      cost: { red: 1 },
    }),
  ];
  const sorted = tutorChoicesFromOptions(options);
  assert.ok(sorted, "expected tutor choices to group");
  assert.deepEqual(
    sorted.map((option) => option.chosenCard.name),
    ["Hypnotic Specter", "Juzam Djinn", "Lightning Bolt", "Swamp"],
    "creatures by cost first, then instants, lands last",
  );
  // The original submission indices survive sorting untouched.
  assert.deepEqual(
    sorted.map((option) => option.index).sort(),
    [0, 1, 2, 3],
  );

  // Options without chosenCard (every non-tutor action) never group.
  assert.equal(
    tutorChoicesFromOptions([
      disintegrateOption(0, 1),
      disintegrateOption(1, 2),
    ]),
    null,
  );
});
