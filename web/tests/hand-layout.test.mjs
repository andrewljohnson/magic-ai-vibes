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

  assert.match(
    attackersControls,
    /if \(decision\.eligible\.length === 0\) return null;/,
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
    /if \(autoSubmittedAttackerDecision\.current === decisionKey\) return;[\s\S]+?act\(\{ decisionId: decision\.decisionId, ids: \[\] \}\);/,
  );
  assert.match(
    app,
    /const hasVisibleDecision = !\([\s\S]+?attackerDecision\.eligible\.length === 0[\s\S]+?\);/,
  );
  assert.match(
    app,
    /\{snapshot\.decision && hasVisibleDecision && \(\s*<DecisionDock/,
  );
});
