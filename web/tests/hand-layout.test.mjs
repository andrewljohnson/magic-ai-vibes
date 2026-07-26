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

function signedPixels(rule, property) {
  const match = rule.match(new RegExp(`${property}:\\s*(-?\\d+)px`));
  assert.ok(match, `missing signed pixel value for ${property}`);
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
  assert.match(consoleRule, /border-top:\s*0/);
  assert.match(consoleRule, /box-shadow:[\s\S]*inset 0 1px 0 var\(--line-strong\)/);

  const dockRule = cssRule(css, ".decision-dock");
  assert.doesNotMatch(dockRule, /position:\s*fixed/);
  assert.match(dockRule, /position:\s*relative/);
  assert.match(dockRule, /grid-template-rows:\s*minmax\(0,\s*1fr\)/);

  const bodyRule = cssRule(css, ".decision-body");
  assert.match(bodyRule, /min-height:\s*0/);
  assert.match(bodyRule, /height:\s*100%/);
  assert.match(bodyRule, /overflow:\s*auto/);
});

test("the full human hand stays rendered and legal cards select exact destinations", async () => {
  const app = await source("App.tsx");

  assert.match(app, /cards\.map\(\(card, index\)/);
  assert.doesNotMatch(app, /cards\.slice\(0,\s*12\)/);
  assert.match(app, /playableCardIds=\{playableHandCardIds\}/);
  assert.match(app, /selectedCardKey=/);
  assert.match(app, /onCardClick=\{selectHandCard\}/);
  assert.match(app, /onCardDoubleClick=\{doubleClickHandCard\}/);
  assert.match(app, /choosePriorityDestination\(`permanent:\$\{id\}`\)/);
  assert.match(app, /choosePriorityDestination\("play"\)/);
});

test("legal cards glow without PLAY labels and white cards keep dark readable ink", async () => {
  const [app, css] = await Promise.all([
    source("App.tsx"),
    source("styles.css"),
  ]);

  assert.match(
    cssRule(css, ".hand-card-wrap.is-playable .card-face,\n.card-face.is-choice-origin"),
    /box-shadow:[\s\S]*rgba\(241,\s*211,\s*138/,
  );
  assert.doesNotMatch(css, /content:\s*"PLAY(?: HERE)?"/);
  assert.doesNotMatch(css, /content:\s*"DROP TO PLAY"/);
  assert.doesNotMatch(app, />PLAY<\/span>/);

  const white = cssRule(css, ".card-white");
  assert.match(white, /color:\s*#171914/);
  assert.match(white, /border-color:\s*#eee5b8/);
  assert.match(white, /background:/);
  assert.match(
    cssRule(css, ".card-white :is(.card-type, .mana-cost, .combat-stats)"),
    /color:\s*#171914/,
  );
});

test("card accessible names mirror every rendered public state cue", async () => {
  const app = await source("App.tsx");
  const cardFace = app.slice(
    app.indexOf("function CardFace"),
    app.indexOf("function HiddenHand"),
  );
  const description = cardFace.slice(
    cardFace.indexOf("const publicDescription"),
    cardFace.indexOf("const title"),
  );

  assert.match(
    cardFace,
    /const hasStats = hasPublicCombatStats\(card\.type, power, toughness\)/,
  );
  for (const publicCue of [
    /card\.name/,
    /permanent #\$\{instanceId\}/,
    /card\.type/,
    /Cost \$\{cost\}/,
    /card\.flying \? "Flying"/,
    /hasStats && power !== undefined \? `\$\{power\} power`/,
    /hasStats && toughness !== undefined \? `\$\{toughness\} toughness`/,
    /permanent\?\.tapped \? "Tapped"/,
    /permanent\?\.summoningSick \? "Summoning sick"/,
    /\$\{permanent\.damage\} damage marked/,
    /Targeted by an object on the stack/,
    /Legal destination for the selected action/,
    /Legal action origin/,
    /choiceOriginLabel/,
  ]) {
    assert.match(description, publicCue);
  }
  assert.match(cardFace, /const title = publicDescription\.join\(" • "\)/);
  assert.match(
    cardFace,
    /const accessibleLabel = publicDescription\.join\(", "\)/,
  );
  assert.match(cardFace, /aria-label=\{accessibleLabel\}/);
  assert.match(cardFace, /aria-pressed=\{actionable \? selected : undefined\}/);
  assert.match(
    cardFace,
    /className="status-token status-damage">\s*DMG \{permanent\.damage\}/,
  );
  assert.doesNotMatch(cardFace, />−\{permanent\.damage\}</);
  assert.doesNotMatch(
    description,
    /opponent|revealedHand|handSize|players|snapshot/,
  );
});

test("flying uses an explicit public keyword without changing other card glyphs", async () => {
  const [app, css] = await Promise.all([
    source("App.tsx"),
    source("styles.css"),
  ]);
  const cardFace = app.slice(
    app.indexOf("function CardFace"),
    app.indexOf("function HiddenHand"),
  );

  assert.match(
    cardFace,
    /card\.flying \? \(\s*<span className="card-flying-cue" aria-hidden="true">\s*FLYING\s*<\/span>/,
  );
  assert.match(
    cardFace,
    /card\.type\?\.toLowerCase\(\)\.includes\("land"\) \? "◆" : "◇"/,
  );
  assert.doesNotMatch(cardFace, /card\.flying \? "✦"/);

  const cue = cssRule(css, ".card-flying-cue");
  assert.match(cue, /font-size:\s*var\(--card-type-floor\)/);
  assert.match(cue, /font-weight:\s*900/);
  assert.match(cue, /border:/);
  assert.match(cue, /background:/);
});

test("public card-state badges occupy separated vertical lanes", async () => {
  const css = await source("styles.css");
  const token = cssRule(css, ".status-token");
  const tokenHeight = pixels(token, "height");
  assert.equal(tokenHeight, 17);
  assert.match(token, /line-height:\s*11px/);

  const damageBottom = signedPixels(cssRule(css, ".status-damage"), "bottom");
  const sickBottom = signedPixels(cssRule(css, ".status-sick"), "bottom");
  const choiceBottom = signedPixels(cssRule(css, ".status-choice"), "bottom");
  assert.ok(
    damageBottom - sickBottom >= tokenHeight + 2,
    "damage and sickness badges need a visible vertical gap",
  );
  assert.ok(
    sickBottom - choiceBottom >= tokenHeight + 2,
    "sickness and action badges need a visible vertical gap",
  );
});

test("cleanup discard selects exact hand positions, including duplicate copies", async () => {
  const app = await source("App.tsx");
  const hand = app.slice(
    app.indexOf("function FaceUpHand"),
    app.indexOf("function ZoneBadge"),
  );
  const cleanup = app.slice(
    app.indexOf("function CleanupDiscardControls"),
    app.indexOf("function DecisionDock"),
  );

  assert.match(hand, /discardOptionIndices\?\.has\(index\)/);
  assert.match(hand, /selectedDiscardIndices\?\.has\(index\)/);
  assert.match(hand, /onDiscardToggle\(index\)/);
  assert.doesNotMatch(
    hand.slice(hand.indexOf("const discardable")),
    /discardOptionIndices\?\.has\(String\(card\.id\)\)/,
  );
  assert.match(cleanup, /selected\.size === decision\.count/);
  assert.match(cleanup, /indices: \[\.\.\.selected\]\.sort/);
  assert.match(cleanup, /disabled=\{busy \|\| !ready\}/);
  assert.match(app, /className=\{`hand-card-wrap[\s\S]+?is-discard-selected/);
});

test("block declarations live on the battlefield and preserve exact engine IDs", async () => {
  const [app, css] = await Promise.all([
    source("App.tsx"),
    source("styles.css"),
  ]);
  const blockers = app.slice(
    app.indexOf("function BlockersControls"),
    app.indexOf("function DamageOrderControls"),
  );
  const blockRouting = app.slice(
    app.indexOf("const selectBlocker"),
    app.indexOf("const toggleAttacker"),
  );

  assert.match(app, /function CombatLane/);
  assert.match(app, /combatAttackerIds\?\.has\(permanentId\(permanent\)\)/);
  assert.match(app, /className="combat-attacking-label">ATTACKING/);
  assert.match(
    cssRule(css, ".combat-attacker-card .card-face"),
    /transform:\s*rotate\(90deg\)/,
  );
  assert.doesNotMatch(blockers, /<select/);
  assert.match(blockers, /className="block-pair-links"/);
  assert.match(blockers, />\s*No blocks\s*<\/button>/);
  assert.match(blockers, /blockerPairsFromKeys\(assignments, decision\.choices\)/);
  assert.match(
    blockRouting,
    /choice\?\.legalAttackers\.some\([\s\S]+?String\(attacker\) === attackerId/,
  );
  assert.match(
    blockRouting,
    /setBlockAssignments\(\{[\s\S]+?\[blockerId\]: attackerId/,
  );
  assert.match(app, /BLOCKED\{blockerCount > 1 \? ` ×\$\{blockerCount\}` : ""\}/);
});

test("damage ordering links duplicate cards to stable public permanent IDs", async () => {
  const [app, css] = await Promise.all([
    source("App.tsx"),
    source("styles.css"),
  ]);
  const cardFace = app.slice(
    app.indexOf("function CardFace"),
    app.indexOf("function HiddenHand"),
  );
  const damageOrder = app.slice(
    app.indexOf("function DamageOrderControls"),
    app.indexOf("function CleanupDiscardControls"),
  );

  assert.match(cardFace, /const instanceId = permanent \? permanentId\(permanent\) : null/);
  assert.match(cardFace, /className="card-instance-cue">#\{instanceId\}/);
  assert.match(cardFace, /permanent #\$\{instanceId\}/);
  assert.match(damageOrder, /className="damage-order-identity"/);
  assert.match(damageOrder, /#\{String\(id\)\}/);
  assert.match(damageOrder, /\{stats \? ` · \$\{stats\}` : ""\}/);
  assert.match(
    damageOrder,
    /aria-label=\{`Move #\$\{String\(id\)\} earlier in damage order`\}/,
  );
  assert.match(
    damageOrder,
    /aria-label=\{`Move #\$\{String\(id\)\} later in damage order`\}/,
  );
  assert.match(cssRule(css, ".card-instance-cue"), /font-size:\s*var\(--card-type-floor\)/);
  assert.match(cssRule(css, ".damage-order-identity small"), /font-size:\s*var\(--type-floor\)/);
});

test("keyboard activation completes exact card and combat choices without drag", async () => {
  const app = await source("App.tsx");
  const hand = app.slice(
    app.indexOf("function FaceUpHand"),
    app.indexOf("function ZoneBadge"),
  );
  const exactActivation = app.slice(
    app.indexOf("const doubleClickHandCard"),
    app.indexOf("const startHandCardDrag"),
  );

  assert.match(
    hand,
    /selected=\{\s*selectedCardKey === renderKey \|\| discardSelected\s*\}/,
  );
  assert.match(
    hand,
    /selectedCardKey === renderKey[\s\S]+?event\.key !== "Enter" && event\.key !== " "[\s\S]+?event\.preventDefault\(\)[\s\S]+?onCardDoubleClick\(card, renderKey\)/,
  );
  assert.match(
    exactActivation,
    /const playOptions = options\.filter\([\s\S]+?priorityDestinationKey\(option\) === "play"/,
  );
  assert.match(
    exactActivation,
    /playOptions\.length === 1[\s\S]+?submitPriorityOption\(playOptions\[0\]\)/,
  );
  assert.match(
    exactActivation,
    /playOptions\.length > 1[\s\S]+?setPendingPriorityOptions\(playOptions\)/,
  );
  assert.match(app, /aria-pressed=\{actionable \? selected : undefined\}/);
});

test("Bluff mode is explicit and suppresses forced empty-combat advance", async () => {
  const app = await source("App.tsx");
  const autoAdvance = app.slice(
    app.indexOf("const decision = snapshot?.decision;"),
    app.indexOf("useEffect(() => {", app.indexOf("const decision = snapshot?.decision;") + 1),
  );

  assert.match(app, /<strong>Bluff mode<\/strong>/);
  assert.match(app, /checked=\{config\.bluffMode\}/);
  assert.match(
    app,
    /Pause for forced passes and empty attacks to preserve timing/,
  );
  assert.match(autoAdvance, /currentConfig\?\.bluffMode/);
  assert.match(
    app,
    /const hasVisibleDecision =\s*\(Boolean\(currentConfig\?\.bluffMode\) \|\|/,
  );
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
    ".priority-parameter-chooser small",
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

test("subdued interface text clears normal-text contrast on dark panels", async () => {
  const css = await source("styles.css");
  const root = cssRule(css, ":root");
  const colorToken = (name) => {
    const match = root.match(new RegExp(`--${name}:\\s*(#[0-9a-f]{6})`, "i"));
    assert.ok(match, `missing hex color token --${name}`);
    return match[1];
  };
  const luminance = (hex) => {
    const channels = hex
      .slice(1)
      .match(/../g)
      .map((channel) => Number.parseInt(channel, 16) / 255)
      .map((channel) =>
        channel <= 0.04045
          ? channel / 12.92
          : ((channel + 0.055) / 1.055) ** 2.4,
      );
    return (
      0.2126 * channels[0] +
      0.7152 * channels[1] +
      0.0722 * channels[2]
    );
  };
  const contrast = (first, second) => {
    const firstLuminance = luminance(first);
    const secondLuminance = luminance(second);
    return (
      (Math.max(firstLuminance, secondLuminance) + 0.05) /
      (Math.min(firstLuminance, secondLuminance) + 0.05)
    );
  };

  assert.ok(
    contrast(colorToken("faint"), colorToken("panel-2")) >= 4.5,
    "the faint token must clear WCAG AA normal-text contrast on panel-2",
  );
  assert.match(cssRule(css, ".event-index"), /color:\s*var\(--faint\)/);
  assert.match(cssRule(css, ".phase-steps li"), /color:\s*var\(--faint\)/);
  assert.match(
    cssRule(css, ".phase-steps li.current"),
    /color:\s*var\(--gold-bright\)/,
    "raising supporting text must not flatten the active-phase hierarchy",
  );
});

test("hidden setup toggles cannot widen the match drawer", async () => {
  const css = await source("styles.css");
  const hiddenToggle = cssRule(css, ".toggle-field > input");

  assert.match(hiddenToggle, /position:\s*absolute/);
  assert.match(hiddenToggle, /width:\s*1px/);
  assert.match(hiddenToggle, /height:\s*1px/);
  assert.match(hiddenToggle, /margin:\s*0/);
  assert.match(hiddenToggle, /opacity:\s*0/);
  assert.match(hiddenToggle, /pointer-events:\s*none/);
  assert.match(
    cssRule(css, ".toggle-field > input:focus-visible + .toggle-track"),
    /outline:\s*2px solid var\(--gold-bright\)[\s\S]+?outline-offset:\s*3px/,
    "the visually hidden native checkbox still needs a visible keyboard cue",
  );
  assert.match(
    cssRule(css, ".simulation-settings"),
    /grid-template-columns:\s*1\.1fr repeat\(3,\s*1fr\) 1\.2fr/,
    "the proven five-column setup layout should not be reflowed to mask overflow",
  );
});

test("setup content uses the primary-label type floor without reflowing its grid", async () => {
  const css = await source("styles.css");
  const setupCopySelectors = [
    ".setup-drawer header p",
    ".seat-setup label > span,\n.simulation-settings label > span:first-child",
    ".policy-description",
    ".seed-field button",
    ".toggle-field strong",
    ".toggle-field small",
    ".setup-drawer footer p",
  ];

  for (const selector of setupCopySelectors) {
    assert.match(
      cssRule(css, selector),
      /font-size:\s*var\(--type-label\)/,
      `${selector} must use the 12px setup-content floor`,
    );
  }
  assert.match(
    cssRule(css, ".simulation-settings"),
    /grid-template-columns:\s*1\.1fr repeat\(3,\s*1fr\) 1\.2fr/,
  );
  assert.match(cssRule(css, ".toggle-field > input"), /width:\s*1px/);
});

test("reduced motion stops animation without hiding status content", async () => {
  const [app, css] = await Promise.all([
    source("App.tsx"),
    source("styles.css"),
  ]);
  const reducedMotion = css.slice(
    css.indexOf("@media (prefers-reduced-motion: reduce)"),
  );

  assert.match(
    cssRule(css, ".spinner"),
    /animation:\s*spin 0\.8s linear infinite/,
  );
  assert.match(
    cssRule(css, ".live-indicator::before"),
    /animation:\s*pulse 2s ease-in-out infinite/,
  );
  assert.match(
    reducedMotion,
    /\*,\s*\*::before,\s*\*::after\s*\{[\s\S]+?scroll-behavior:\s*auto !important/,
  );
  assert.match(reducedMotion, /animation-duration:\s*0\.01ms !important/);
  assert.match(reducedMotion, /animation-iteration-count:\s*1 !important/);
  assert.match(reducedMotion, /transition-duration:\s*0\.01ms !important/);
  assert.doesNotMatch(
    reducedMotion,
    /display:\s*none|visibility:\s*hidden|opacity:\s*0(?:[;\s}]|$)/,
  );
  assert.match(
    app,
    /className="opponent-thinking-banner"[\s\S]+?Opponent thinking…[\s\S]+?Resolving your action/,
  );
  assert.doesNotMatch(app, /matchMedia\([^)]*prefers-reduced-motion/);
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

test("live header exposes a compact selectable reproduction summary", async () => {
  const [app, css] = await Promise.all([
    source("App.tsx"),
    source("styles.css"),
  ]);
  const reproduction = app.slice(
    app.indexOf("function ReproductionSummary"),
    app.indexOf("function TopBar"),
  );

  for (const field of [
    "config.seed",
    "config.trainGames",
    "config.trainSeed",
    "config.rollouts",
    "config.deepRollouts",
    "config.learnedRollouts",
    "config.bluffMode",
    "config.debugReveal",
  ]) {
    assert.match(reproduction, new RegExp(field.replace(".", "\\.")));
  }
  assert.match(reproduction, /deckLabel\(snapshot, meta, seat\)/);
  assert.match(reproduction, /policyLabel\(snapshot, meta, seat\)/);
  assert.match(reproduction, /formatReproductionSummary\(config,\s*\{/);
  const exactSummary = reproduction.slice(
    reproduction.indexOf("const exactSummary"),
    reproduction.indexOf("return ("),
  );
  assert.match(exactSummary, /snapshot\.state\?\.turnNumber/);
  assert.match(exactSummary, /snapshot\.state\?\.phase/);
  assert.match(
    exactSummary,
    /reproductionPriorityHolder\(\s*snapshot\.decision\?\.kind,\s*Boolean\(snapshot\.state\)/,
  );
  assert.match(exactSummary, /latestPublicEventMessage\(snapshot\.log \?\? \[\]\)/);
  assert.doesNotMatch(
    exactSummary,
    /state\?\.players|\.hand|revealedHand|graveyard|library|stack/,
  );
  assert.match(reproduction, /<textarea[\s\S]+?value=\{exactSummary\}[\s\S]+?readOnly/);
  assert.match(
    reproduction,
    /onFocus=\{\(event\) => event\.currentTarget\.select\(\)\}/,
  );
  assert.match(
    reproduction,
    /Setup \+ public turn context · no hand contents included/,
  );
  assert.match(reproduction, /rows=\{4\}/);
  assert.doesNotMatch(reproduction, /\.hand|revealedHand|graveyard|library|stack/);
  assert.match(
    app,
    /<ReproductionSummary[\s\S]+?config=\{config\}[\s\S]+?snapshot=\{snapshot\}/,
  );

  const panel = cssRule(css, ".repro-panel");
  assert.match(panel, /position:\s*absolute/);
  assert.match(panel, /width:\s*min\(600px,\s*calc\(100vw - 48px\)\)/);
  assert.equal(pixels(cssRule(css, ".top-bar"), "height"), 64);
});

test("debug reveal is persistently warned and new setup fails closed", async () => {
  const [app, css] = await Promise.all([
    source("App.tsx"),
    source("styles.css"),
  ]);
  const initialConfig = app.slice(
    app.indexOf("const initialConfig = useMemo"),
    app.indexOf("const currentConfig"),
  );
  const newMatchConfig = app.slice(
    app.indexOf("const newMatchConfig"),
    app.indexOf("const startGame"),
  );
  const topBar = app.slice(
    app.indexOf("function TopBar"),
  );

  assert.match(initialConfig, /debugReveal:\s*false/);
  assert.doesNotMatch(initialConfig, /readDefault\([^)]*debugReveal/);
  assert.match(
    newMatchConfig,
    /currentConfig[\s\S]+?\.\.\.currentConfig[\s\S]+?debugReveal:\s*false/,
  );
  assert.match(
    app,
    /useEffect\(\(\) => \{\s*if \(open && initialConfig\) \{\s*setConfig\(\{\s*\.\.\.initialConfig,\s*debugReveal: false/,
  );
  assert.match(app, /<SetupDrawer[\s\S]+?initialConfig=\{newMatchConfig\}/);
  assert.match(
    topBar,
    /inGame && config\?\.debugReveal[\s\S]+?className="debug-reveal-warning"[\s\S]+?role="status"[\s\S]+?DEBUG REVEAL ON/,
  );
  assert.doesNotMatch(
    topBar.slice(topBar.indexOf("debug-reveal-warning")),
    /revealedHand|\.hand|graveyard|stack/,
  );
  assert.match(app, /const replay = \(\) => \{[\s\S]+?startGame\(currentConfig\)/);

  const warning = cssRule(css, ".debug-reveal-warning");
  assert.match(warning, /border:\s*1px solid #ff9b8c/);
  assert.match(warning, /background:\s*#8d3028/);
  assert.match(warning, /font-weight:\s*850/);
  assert.match(warning, /white-space:\s*nowrap/);
});

test("combat skips empty attacker decisions without duplicate controls", async () => {
  const app = await source("App.tsx");
  const attackersControls = app.slice(
    app.indexOf("function AttackersControls"),
    app.indexOf("function BlockersControls"),
  );
  const decisionDock = app.slice(
    app.indexOf("function DecisionDock"),
    app.indexOf("function DeckManifest"),
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
    /const emptyAttackDecisionKey =[\s\S]+?attackerDecision\?\.eligible\.length === 0[\s\S]+?const hasVisibleDecision =[\s\S]+?autoAdvanceFailedDecision === emptyAttackDecisionKey\)/,
  );
  assert.match(
    app,
    /\{snapshot\.decision && hasVisibleDecision && \(\s*<DecisionDock/,
  );
  assert.match(
    decisionDock,
    /bluffMode\s*\?\s*"Bluff mode paused before declaring no attackers\."\s*:\s*"Automatic advance was interrupted\. Continue without attackers\."/,
  );
  assert.match(
    app,
    /bluffMode=\{Boolean\(currentConfig\?\.bluffMode\)\}/,
  );
});

test("combat auto-submits an engine-forced empty blocker declaration", async () => {
  const app = await source("App.tsx");
  const blockerAutoAdvanceStart = app.indexOf(
    'decision?.kind !== "blockers"',
  );
  const blockerAutoAdvanceEnd = app.indexOf(
    "useEffect(() => {",
    blockerAutoAdvanceStart,
  );
  const blockerAutoAdvance = app.slice(
    blockerAutoAdvanceStart,
    blockerAutoAdvanceEnd,
  );

  assert.ok(blockerAutoAdvanceStart >= 0, "missing blocker auto-advance effect");
  assert.match(
    app,
    /const autoSubmittedBlockerDecision = useRef<string \| null>\(null\);/,
  );
  assert.match(
    blockerAutoAdvance,
    /decision\.choices\.length !== 0/,
  );
  assert.doesNotMatch(
    blockerAutoAdvance,
    /card|creature|battlefield|currentConfig\?\.bluffMode/,
  );
  assert.match(
    blockerAutoAdvance,
    /if \(autoSubmittedBlockerDecision\.current === decisionKey\) return;[\s\S]+?decisionId: decision\.decisionId, pairs: \[\][\s\S]+?setAutoAdvanceFailedDecision\(decisionKey\)/,
  );
  assert.match(
    app,
    /const emptyBlockDecisionKey =[\s\S]+?blockerDecision\?\.choices\.length === 0[\s\S]+?\(emptyBlockDecisionKey === null \|\|[\s\S]+?autoAdvanceFailedDecision === emptyBlockDecisionKey\)/,
  );
});
