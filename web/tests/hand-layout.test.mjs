import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const WEB_ROOT = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "..",
);

async function source(name) {
  return readFile(path.join(WEB_ROOT, "src", name), "utf8");
}

function cssRule(css, selector) {
  const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const match = css.match(new RegExp(`${escaped}\\s*\\{([^}]+)\\}`, "s"));
  assert.ok(match, `missing CSS rule for ${selector}`);
  return match[1];
}

function pixels(rule, property) {
  const match = rule.match(new RegExp(`${property}:\\s*(\\d+)px`));
  assert.ok(match, `missing pixel value for ${property}`);
  return Number(match[1]);
}

function hoverTransform(rule) {
  const match = rule.match(
    /transform:\s*translateY\(-(\d+)px\)\s*scale\(([\d.]+)\)/,
  );
  assert.ok(match, "hand hover must use a bounded lift and scale affordance");
  return { lift: Number(match[1]), scale: Number(match[2]) };
}

test("the player console reserves separate visible regions for hand and actions", async () => {
  const [app, css] = await Promise.all([
    source("App.tsx"),
    source("styles.css"),
  ]);

  assert.match(app, /className=\{`player-console/);
  assert.match(
    app,
    /className=\{`player-console[\s\S]+?<FaceUpHand[\s\S]+?<DecisionDock/,
  );

  const consoleRule = cssRule(css, ".player-console");
  assert.match(consoleRule, /display:\s*grid/);
  assert.match(consoleRule, /grid-template-rows:/);
  assert.match(consoleRule, /height:\s*var\(--player-console-height\)/);

  const dockRule = cssRule(css, ".decision-dock");
  assert.doesNotMatch(dockRule, /position:\s*fixed/);
  assert.match(dockRule, /position:\s*relative/);
  assert.match(dockRule, /grid-template-rows:\s*minmax\(0,\s*1fr\)/);

  const bodyRule = cssRule(css, ".decision-body");
  assert.match(bodyRule, /min-height:\s*0/);
  assert.match(bodyRule, /height:\s*100%/);
  assert.match(bodyRule, /overflow:\s*auto/);
});

test("the full human hand stays rendered and legal cards focus their action", async () => {
  const app = await source("App.tsx");

  assert.match(app, /cards\.map\(\(card, index\)/);
  assert.doesNotMatch(app, /cards\.slice\(0,\s*12\)/);
  assert.match(app, /playableCardIds=\{playableHandCardIds\}/);
  assert.match(app, /priorityOptionElementId\(decision\.decisionId/);
  assert.match(app, /element\?\.focus\(\{ preventScroll: true \}\)/);
});

test("hand hover keeps card headings inside 1440x900 and 1280x720 layouts", async () => {
  const css = await source("styles.css");
  const shellRule = cssRule(css, ".game-shell");
  const wrapRule = cssRule(css, ".hand-card-wrap");
  const hoverRule = cssRule(css, ".hand-card-wrap:hover");
  const actionableHoverRule = cssRule(
    css,
    ".hand-card-wrap .card-face.is-actionable:hover",
  );
  const shortViewport = css.match(
    /@media \(max-height:\s*780px\)\s*\{([\s\S]+?)\n\}/,
  );

  assert.ok(shortViewport, "missing compact-height layout");
  const compactShellRule = cssRule(shortViewport[1], ".game-shell");
  const compactWrapRule = cssRule(shortViewport[1], ".hand-card-wrap");
  const { lift, scale } = hoverTransform(hoverRule);
  assert.match(actionableHoverRule, /transform:\s*none/);

  const layouts = [
    {
      viewport: "1440x900",
      handHeight: pixels(shellRule, "--player-hand-height"),
      wrapHeight: pixels(wrapRule, "height"),
    },
    {
      viewport: "1280x720",
      handHeight: pixels(compactShellRule, "--player-hand-height"),
      wrapHeight: pixels(compactWrapRule, "height"),
    },
  ];

  for (const { viewport, handHeight, wrapHeight } of layouts) {
    const visibleHeadroom = handHeight - 1 - wrapHeight * scale - lift;
    assert.ok(
      visibleHeadroom >= 0,
      `${viewport} clips the hovered card heading by ${-visibleHeadroom}px`,
    );
  }
});

test("match typography keeps a readable floor without enlarging fixed regions", async () => {
  const css = await source("styles.css");
  const rootRule = cssRule(css, ":root");
  assert.match(rootRule, /--type-floor:\s*11px/);
  assert.match(rootRule, /--type-label:\s*12px/);
  assert.match(rootRule, /--card-type-floor:\s*11px/);
  assert.match(rootRule, /--card-name-size:\s*12px/);

  const uiFloorSelectors = [
    ".event-index",
    ".event-turn",
    ".priority-marker span",
    ".zone-badge",
    ".row-label",
    ".stack-entry > span:not(.stack-order)",
    ".stack-choice-context > small",
    ".action-copy span",
  ];
  for (const selector of uiFloorSelectors) {
    assert.match(
      cssRule(css, selector),
      /font-size:\s*var\(--type-floor\)/,
      `${selector} must use the 11px interface floor`,
    );
  }

  for (const selector of [
    ".turn-marker span",
    ".phase-steps li",
    ".hand-label",
    ".decision-heading p",
    ".deck-manifest li",
  ]) {
    assert.match(
      cssRule(css, selector),
      /font-size:\s*var\(--type-label\)/,
      `${selector} must use a 12px primary-label size`,
    );
  }

  const cardFloorSelectors = [
    ".mana-cost",
    ".card-type",
    ".combat-stats",
    ".status-token",
  ];
  for (const selector of cardFloorSelectors) {
    assert.match(
      cssRule(css, selector),
      /font-size:\s*var\(--card-type-floor\)/,
      `${selector} must use the 11px embedded-card floor`,
    );
  }
  assert.match(
    cssRule(css, ".card-name"),
    /font-size:\s*var\(--card-name-size\)/,
  );
  assert.match(cssRule(css, ".hand-card-wrap .card-name"), /font-size:\s*13px/);

  const undersizedLiterals = [...css.matchAll(/font-size:\s*(\d+)px/g)]
    .map(([, value]) => Number(value))
    .filter((value) => value > 0 && value < 11);
  assert.deepEqual(
    undersizedLiterals,
    [],
    "content text must not bypass the 11px absolute floor",
  );
  assert.equal(css.match(/font-size:\s*0\s*;/g)?.length, 1);
  assert.match(
    css,
    /Intentional exception: the adjacent 17px icon replaces this text label\.\s*\*\/\s*font-size:\s*0/,
  );
  assert.doesNotMatch(
    css,
    /scale\(0\./,
    "responsive transforms must not shrink readable type below its declared size",
  );

  const shellRule = cssRule(css, ".game-shell");
  const compactViewport = css.match(
    /@media \(max-height:\s*780px\)\s*\{([\s\S]+?)\n\}/,
  );
  assert.ok(compactViewport, "missing 1280x720 compact-height layout");
  const compactShellRule = cssRule(compactViewport[1], ".game-shell");
  assert.equal(pixels(shellRule, "--player-hand-height"), 142);
  assert.equal(pixels(compactShellRule, "--player-hand-height"), 112);
  assert.ok(
    pixels(compactShellRule, "--player-decision-height") >= 150,
    "the 1280x720 decision row must fit the 12px heading copy with margin",
  );
});

test("landing metadata includes and advertises every bot policy", async () => {
  const app = await source("App.tsx");

  for (const policy of [
    "Random",
    "Monte Carlo",
    "Deep Monte Carlo",
    "HandcodedPolicy",
    "Learned Value",
    "Learned Actor",
  ]) {
    assert.match(app, new RegExp(policy));
  }
  assert.match(app, /id:\s*"learned-actor"/);
  assert.match(app, /policyCount=\{meta\?\.policies\.length \|\| FALLBACK_POLICIES\.length\}/);
  assert.match(app, /<strong>\{policyCount\}<\/strong> bot policies/);
});

test("combat skips empty attacker decisions without duplicate controls", async () => {
  const app = await source("App.tsx");
  const attackersControls = app.slice(
    app.indexOf("function AttackersControls"),
    app.indexOf("function BlockersControls"),
  );
  const emptyAttackers = attackersControls.slice(
    attackersControls.indexOf("if (decision.eligible.length === 0)"),
    attackersControls.indexOf("const toggle"),
  );

  assert.match(
    emptyAttackers,
    /Continue — no attackers/,
  );
  assert.equal(
    emptyAttackers.match(/<button/g)?.length,
    1,
    "a failed automatic advance must reveal exactly one recovery action",
  );
  assert.match(attackersControls, />\s*No attacks\s*<\/button>/);
  assert.match(attackersControls, /disabled=\{busy \|\| selected\.size === 0\}/);
  assert.match(
    attackersControls,
    /selected\.size === 0\s*\?\s*"Select attackers"\s*:\s*`Attack with \$\{selected\.size\}`/,
  );
  assert.doesNotMatch(attackersControls, /Attack with \{selected\.size \|\| "none"\}/);

  assert.match(
    app,
    /const autoSubmittedAttackerDecision = useRef<string \| null>\(null\);/,
  );
  assert.match(
    app,
    /decision\?\.kind !== "attackers"[\s\S]+?decision\.eligible\.length !== 0/,
  );
  assert.match(
    app,
    /if \(autoSubmittedAttackerDecision\.current === decisionKey\) return;[\s\S]+?act\([\s\S]+?decisionId: decision\.decisionId, ids: \[\][\s\S]+?setAutoAdvanceFailedDecision\(decisionKey\)/,
  );
  assert.match(
    app,
    /const emptyAttackDecisionKey =[\s\S]+?attackerDecision\?\.eligible\.length === 0[\s\S]+?const hasVisibleDecision =[\s\S]+?autoAdvanceFailedDecision === emptyAttackDecisionKey;/,
  );
  assert.match(
    app,
    /\{snapshot\.decision && hasVisibleDecision && \(\s*<DecisionDock/,
  );
});
