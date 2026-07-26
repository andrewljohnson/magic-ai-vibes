import assert from "node:assert/strict";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

import { chromium } from "playwright";

import { startServer } from "../../server.mjs";

const TEST_DIRECTORY = path.dirname(fileURLToPath(import.meta.url));
const TARGET_STACK_BRIDGE = path.join(
  TEST_DIRECTORY,
  "..",
  "fixtures",
  "target-stack-bridge.mjs",
);
const INTERACTION_BRIDGE = path.join(
  TEST_DIRECTORY,
  "..",
  "fixtures",
  "interaction-bridge.mjs",
);
const REAL_ENGINE_BRIDGE = path.resolve(
  TEST_DIRECTORY,
  "..",
  "..",
  "..",
  "build",
  "old-school-web-bridge",
);
const VIEWPORTS = [
  { width: 1280, height: 720 },
  { width: 1440, height: 900 },
];
const REAL_ENGINE_VIEWPORT = { width: 1280, height: 720 };
const ACTION_PATH = /^\/api\/games\/[^/]+\/actions$/;

async function launchBrowser() {
  const executablePath =
    process.env.OLD_SCHOOL_WEB_CHROME_EXECUTABLE?.trim() || undefined;
  const baseOptions = {
    headless: process.env.OLD_SCHOOL_WEB_HEADED !== "1",
    ...(executablePath ? { executablePath } : {}),
  };
  if (executablePath) return chromium.launch(baseOptions);

  try {
    return await chromium.launch(baseOptions);
  } catch (bundledError) {
    try {
      return await chromium.launch({ ...baseOptions, channel: "chrome" });
    } catch (chromeError) {
      throw new AggregateError(
        [bundledError, chromeError],
        "Playwright could not launch its Chromium build or the Chrome channel. " +
          "Run `npx playwright install chromium`, or set " +
          "OLD_SCHOOL_WEB_CHROME_EXECUTABLE.",
      );
    }
  }
}

async function startFixture() {
  const server = await startServer({
    port: 0,
    host: "127.0.0.1",
    bridgePath: process.execPath,
    bridgeArgsPrefix: [TARGET_STACK_BRIDGE],
  });
  const address = server.address();
  assert.ok(
    typeof address === "object" && address !== null,
    "fixture server must expose a TCP address",
  );
  return {
    server,
    url: `http://127.0.0.1:${address.port}`,
  };
}

async function startForcedEmptyBlockersFixture() {
  const server = await startServer({
    port: 0,
    host: "127.0.0.1",
    bridgePath: process.execPath,
    bridgeArgsPrefix: [INTERACTION_BRIDGE, "--forced-empty-blockers"],
  });
  const address = server.address();
  assert.ok(
    typeof address === "object" && address !== null,
    "empty-blockers fixture server must expose a TCP address",
  );
  return {
    server,
    url: `http://127.0.0.1:${address.port}`,
  };
}

async function startRealEngine() {
  const server = await startServer({
    port: 0,
    host: "127.0.0.1",
    bridgePath: REAL_ENGINE_BRIDGE,
  });
  const address = server.address();
  assert.ok(
    typeof address === "object" && address !== null,
    "real-engine server must expose a TCP address",
  );
  return {
    server,
    url: `http://127.0.0.1:${address.port}`,
  };
}

async function closeServer(server) {
  if (!server.listening) return;
  await new Promise((resolve, reject) => {
    server.close((error) => (error ? reject(error) : resolve()));
  });
}

async function configureFixedMatch(page, { bluffMode = false } = {}) {
  const setup = page.getByRole("dialog", { name: "Set the table" });
  await setup.waitFor();
  const nearSeat = setup.locator(".seat-0");
  const farSeat = setup.locator(".seat-1");
  await nearSeat.locator("select").nth(0).selectOption("green");
  await farSeat.locator("select").nth(0).selectOption("red");
  await farSeat.locator("select").nth(1).selectOption("learned-value-g0");

  const numberInputs = setup.locator('input[type="number"]');
  await numberInputs.nth(0).fill("42");
  await numberInputs.nth(1).fill("800");
  await numberInputs.nth(2).fill("424242");
  const toggles = setup.locator('input[type="checkbox"]');
  assert.equal(
    await toggles.nth(0).isChecked(),
    false,
    "debug reveal must be off",
  );
  assert.equal(await toggles.nth(1).isChecked(), false, "Bluff mode must be off");
  if (bluffMode) {
    await setup.getByText("Bluff mode", { exact: true }).click();
    assert.equal(
      await toggles.nth(1).isChecked(),
      true,
      "visible Bluff mode label must enable the checkbox",
    );
  }

  await setup.getByRole("button", { name: "Start match" }).click();
  await setup.waitFor({ state: "hidden" });
}

async function configureRealEngineMatch(page, { bluffMode }) {
  const setup = page.getByRole("dialog", { name: "Set the table" });
  await setup.waitFor();
  const nearSeat = setup.locator(".seat-0");
  const farSeat = setup.locator(".seat-1");
  await nearSeat.locator("select").nth(0).selectOption("green");
  await farSeat.locator("select").nth(0).selectOption("red");
  await farSeat.locator("select").nth(1).selectOption("random");

  const numberInputs = setup.locator('input[type="number"]');
  await numberInputs.nth(0).fill("42");
  await numberInputs.nth(1).fill("1");
  await numberInputs.nth(2).fill("424242");
  const toggles = setup.locator('input[type="checkbox"]');
  assert.equal(
    await toggles.nth(0).isChecked(),
    false,
    "debug reveal must be off",
  );
  assert.equal(
    await toggles.nth(1).isChecked(),
    false,
    "Bluff mode must begin off",
  );
  if (bluffMode) {
    await setup.getByText("Bluff mode", { exact: true }).click();
    assert.equal(
      await toggles.nth(1).isChecked(),
      true,
      "visible Bluff mode label must enable the checkbox",
    );
  }

  await setup.getByRole("button", { name: "Start match" }).click();
  await setup.waitFor({ state: "hidden" });
}

async function configureFrozenC16Match(page) {
  const setup = page.getByRole("dialog", { name: "Set the table" });
  await setup.waitFor();
  const nearSeat = setup.locator(".seat-0");
  const farSeat = setup.locator(".seat-1");
  await nearSeat.locator("select").nth(0).selectOption("blue");
  await farSeat.locator("select").nth(0).selectOption("blue");

  const policy = farSeat.locator("select").nth(1);
  assert.deepEqual(
    await policy.locator("option").evaluateAll((options) =>
      options
        .map((option) => option.value)
        .filter((value) => value.startsWith("learned-value")),
    ),
    ["learned-value-c16", "learned-value-g0"],
  );
  await policy.selectOption("learned-value-c16");

  const numberInputs = setup.locator('input[type="number"]');
  await numberInputs.nth(0).fill("42");
  assert.equal(await numberInputs.nth(1).inputValue(), "800");
  assert.equal(await numberInputs.nth(2).inputValue(), "424242");
  assert.equal(await numberInputs.nth(1).isEditable(), false);
  assert.equal(await numberInputs.nth(2).isEditable(), false);

  const responsePromise = page.waitForResponse((response) => {
    const url = new URL(response.url());
    return (
      response.request().method() === "POST" &&
      url.pathname === "/api/games"
    );
  });
  await setup.getByRole("button", { name: "Start match" }).click();
  const response = await responsePromise;
  assert.equal(response.ok(), true);
  const body = await response.json();
  await setup.waitFor({ state: "hidden" });
  return body.game;
}

function isActionRequest(request) {
  const requestUrl = new URL(request.url());
  return (
    request.method() === "POST" &&
    ACTION_PATH.test(requestUrl.pathname)
  );
}

async function captureActionResponses(page, count, trigger) {
  return new Promise((resolve, reject) => {
    const bodies = [];
    const timer = setTimeout(() => {
      page.off("response", onResponse);
      reject(
        new Error(
          `expected ${count} action responses, received ${bodies.length}`,
        ),
      );
    }, 10_000);
    const settle = (result, error) => {
      clearTimeout(timer);
      page.off("response", onResponse);
      if (error) reject(error);
      else resolve(result);
    };
    const onResponse = async (response) => {
      if (!isActionRequest(response.request())) return;
      try {
        assert.equal(response.ok(), true, "action response must succeed");
        bodies.push(await response.json());
        if (bodies.length === count) settle(bodies);
      } catch (error) {
        settle(undefined, error);
      }
    };
    page.on("response", onResponse);
    Promise.resolve()
      .then(trigger)
      .catch((error) => settle(undefined, error));
  });
}

async function playOpeningForest(page) {
  const hand = page.getByRole("list", { name: "Cards in your hand" });
  const forest = hand
    .getByRole("button", { name: "Forest, land", exact: true })
    .last();
  const handLabels = await hand
    .locator(".card-face")
    .evaluateAll((cards) => cards.map((card) => card.getAttribute("aria-label")));
  assert.equal(
    await forest.count(),
    1,
    `opening Forest must render; hand labels were ${JSON.stringify(handLabels)}`,
  );
  assert.equal(await forest.isEnabled(), true, "opening Forest must be legal");
  await forest.press("Enter");
  await page.waitForFunction(
    () =>
      document.querySelector(
        '.face-hand:not(.debug-hand) .card-face[aria-pressed="true"]',
      ) !== null,
  );
  const playSurface = page.locator(
    '.battlefield-side.player-side[data-priority-play-target="true"] ' +
      ".permanent-zone",
  );
  await playSurface.click({ position: { x: 8, y: 8 } });
}

async function renderedGeometry(page) {
  return page.evaluate(() => {
    const rectangle = (selector) => {
      const element = document.querySelector(selector);
      if (!element) return null;
      const bounds = element.getBoundingClientRect();
      return {
        left: bounds.left,
        top: bounds.top,
        right: bounds.right,
        bottom: bounds.bottom,
        width: bounds.width,
        height: bounds.height,
      };
    };
    const hand = rectangle(".face-hand:not(.debug-hand)");
    const dock = rectangle(".decision-dock");
    const stack = rectangle(".stack-rail");
    const overlap = (left, right) => {
      if (!left || !right) return null;
      return (
        Math.max(
          0,
          Math.min(left.right, right.right) - Math.max(left.left, right.left),
        ) *
        Math.max(
          0,
          Math.min(left.bottom, right.bottom) - Math.max(left.top, right.top),
        )
      );
    };
    return {
      viewport: { width: window.innerWidth, height: window.innerHeight },
      documentWidth: document.documentElement.scrollWidth,
      bodyWidth: document.body.scrollWidth,
      hand,
      dock,
      stack,
      handDockOverlap: overlap(hand, dock),
    };
  });
}

async function nativePointerDrag(page, source, target) {
  await source.scrollIntoViewIfNeeded();
  const sourceBounds = await source.boundingBox();
  assert.ok(sourceBounds, "drag source must have a rendered bounding box");
  await page.mouse.move(sourceBounds.x + 8, sourceBounds.y + 28);
  await page.mouse.down();
  try {
    await page.mouse.move(sourceBounds.x + 24, sourceBounds.y + 28, {
      steps: 4,
    });
    await page.waitForFunction(
      () => document.querySelector(".hand-card-wrap.is-dragging") !== null,
    );
    await page.waitForFunction(
      () => document.querySelector(".card-face.is-choice-target") !== null,
    );
    const targetBounds = await target.boundingBox();
    assert.ok(targetBounds, "drag target must have a rendered bounding box");
    await page.mouse.move(
      targetBounds.x + targetBounds.width / 2,
      targetBounds.y + targetBounds.height / 2,
      { steps: 12 },
    );
  } finally {
    await page.mouse.up();
  }
}

function assertVisibleRectangle(rectangle, viewport, label) {
  assert.ok(rectangle, `${label} must render`);
  assert.ok(
    rectangle.width > 0 && rectangle.height > 0,
    `${label} must have area`,
  );
  assert.ok(rectangle.left >= -0.5, `${label} extends left of the viewport`);
  assert.ok(rectangle.top >= -0.5, `${label} extends above the viewport`);
  assert.ok(
    rectangle.right <= viewport.width + 0.5,
    `${label} right ${rectangle.right} exceeds viewport ${viewport.width}`,
  );
  assert.ok(
    rectangle.bottom <= viewport.height + 0.5,
    `${label} bottom ${rectangle.bottom} exceeds viewport ${viewport.height}`,
  );
}

async function stackControllerMetrics(page) {
  return page.locator(".stack-entry").evaluateAll((entries) => {
    const rectangle = (element) => {
      if (!element) return null;
      const bounds = element.getBoundingClientRect();
      return {
        left: bounds.left,
        top: bounds.top,
        right: bounds.right,
        bottom: bounds.bottom,
        width: bounds.width,
        height: bounds.height,
      };
    };
    const overlap = (left, right) => {
      if (!left || !right) return null;
      return (
        Math.max(
          0,
          Math.min(left.right, right.right) - Math.max(left.left, right.left),
        ) *
        Math.max(
          0,
          Math.min(left.bottom, right.bottom) - Math.max(left.top, right.top),
        )
      );
    };
    return entries.map((entry) => {
      const controller = entry.querySelector(".stack-controller");
      const controllerRectangle = rectangle(controller);
      const orderRectangle = rectangle(entry.querySelector(".stack-order"));
      return {
        text: controller?.textContent?.trim() ?? null,
        accessibleName: controller?.getAttribute("aria-label") ?? null,
        entry: rectangle(entry),
        controller: controllerRectangle,
        order: orderRectangle,
        controllerOrderOverlap: overlap(
          controllerRectangle,
          orderRectangle,
        ),
      };
    });
  });
}

function assertStackControllerCues(cues, expected) {
  assert.equal(cues.length, expected.length);
  assert.deepEqual(
    cues.map(({ text }) => text),
    expected,
  );
  cues.forEach((cue, index) => {
    const readable =
      expected[index] === "YOU" ? "You" : "Opponent";
    assert.equal(cue.accessibleName, `Controlled by ${readable}`);
    assert.ok(cue.entry, `stack entry ${index} must render`);
    assert.ok(cue.controller, `stack controller ${index} must render`);
    assert.ok(cue.order, `stack order ${index} must render`);
    assert.ok(
      cue.controller.left >= cue.entry.left - 0.5 &&
        cue.controller.top >= cue.entry.top - 0.5 &&
        cue.controller.right <= cue.entry.right + 0.5 &&
        cue.controller.bottom <= cue.entry.bottom + 0.5,
      `stack controller ${index} must remain inside its entry`,
    );
    assert.ok(
      cue.controllerOrderOverlap <= 0.5,
      `stack controller ${index} overlaps its order cue by ` +
        `${cue.controllerOrderOverlap}px²`,
    );
  });
}

for (const viewport of VIEWPORTS) {
  test(
    `frozen C16 identity is loaded and conspicuous at ${viewport.width}x${viewport.height}`,
    { timeout: 60_000 },
    async (t) => {
      const { server, url } = await startRealEngine();
      t.after(() => closeServer(server));
      const browser = await launchBrowser();
      t.after(() => browser.close());
      const context = await browser.newContext({ viewport });
      t.after(() => context.close());
      const page = await context.newPage();

      await page.goto(url, { waitUntil: "networkidle" });
      const game = await configureFrozenC16Match(page);
      assert.equal(game.config.players[1].policyId, "learned-value-c16");
      assert.equal(game.config.learnedGenerations, 16);
      assert.equal(game.config.learnedRollouts, 8);
      assert.deepEqual(game.model, {
        family: "learned-value",
        generation: 16,
        searchWorlds: 8,
        horizonTurns: 4,
        source: "frozen-artifact",
        fingerprint:
          "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f",
      });

      const visiblePolicy = page.locator(
        ".opponent-side .player-hud .player-copy > span",
      );
      assert.equal(await visiblePolicy.count(), 1);
      assert.equal(await visiblePolicy.isVisible(), true);
      assert.match(await visiblePolicy.innerText(), /Learned Value C16/);
      await page
        .locator('summary[aria-label="Open exact reproduction settings"]')
        .click();
      const panel = page.locator(".repro-panel");
      await panel.waitFor();
      const panelText = await panel.innerText();
      assert.match(panelText, /Learned model/);
      assert.match(panelText, /learned-value C16 · K8\/H4/);
      assert.match(
        panelText,
        /68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f/,
      );
      const exact = await panel
        .getByLabel("Selectable exact reproduction summary")
        .inputValue();
      assert.match(exact, /opponent=blue\/learned-value-c16/);
      assert.match(exact, /learned-generations=16/);
      assert.match(exact, /model=learned-value\/C16/);
      assert.match(exact, /learned-search=K8\/H4/);
      assert.match(
        exact,
        /model-fingerprint=68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f/,
      );
      assert.equal(await page.getByRole("alert").count(), 0);

      const geometry = await page.evaluate(() => {
        const panel = document.querySelector(".repro-panel");
        const bounds = panel?.getBoundingClientRect();
        return {
          documentWidth: document.documentElement.scrollWidth,
          bodyWidth: document.body.scrollWidth,
          panel: bounds
            ? {
                left: bounds.left,
                top: bounds.top,
                right: bounds.right,
                bottom: bounds.bottom,
                width: bounds.width,
                height: bounds.height,
              }
            : null,
        };
      });
      assert.equal(geometry.documentWidth, viewport.width);
      assert.equal(geometry.bodyWidth, viewport.width);
      assertVisibleRectangle(
        geometry.panel,
        viewport,
        "C16 reproduction panel",
      );

      t.diagnostic(
        `${viewport.width}x${viewport.height}: real bridge loaded exact ` +
          "C16 fingerprint and rendered C16 · K8/H4 in REPRO",
      );
    },
  );
}

test(
  "forced empty blockers auto-submit exactly once even in Bluff mode",
  { timeout: 60_000 },
  async (t) => {
    const { server, url } = await startForcedEmptyBlockersFixture();
    t.after(() => closeServer(server));
    const browser = await launchBrowser();
    t.after(() => browser.close());
    const context = await browser.newContext({ viewport: REAL_ENGINE_VIEWPORT });
    t.after(() => context.close());
    const page = await context.newPage();
    const actionPayloads = [];
    page.on("request", (request) => {
      if (isActionRequest(request)) {
        actionPayloads.push(request.postDataJSON());
      }
    });

    await page.goto(url, { waitUntil: "networkidle" });
    const [progressed] = await captureActionResponses(page, 1, () =>
      configureFixedMatch(page, { bluffMode: true }),
    );

    assert.equal(progressed.game.config.seed, "42");
    assert.equal(progressed.game.config.bluffMode, true);
    assert.equal(progressed.game.decision.kind, "priority");
    assert.equal(
      progressed.game.decision.id,
      "forced-empty-blockers:next-priority",
    );
    assert.deepEqual(actionPayloads, [
      {
        decisionId: "forced-empty-blockers:opaque/17",
        pairs: [],
      },
    ]);
    assert.equal(
      await page.getByRole("button", { name: "No blocks" }).count(),
      0,
    );
    assert.equal(
      await page.getByRole("button", { name: "Pass priority" }).count(),
      1,
    );
    assert.equal(await page.getByRole("alert").count(), 0);

    const geometry = await renderedGeometry(page);
    assert.equal(geometry.viewport.width, REAL_ENGINE_VIEWPORT.width);
    assert.equal(geometry.viewport.height, REAL_ENGINE_VIEWPORT.height);
    assert.equal(geometry.documentWidth, REAL_ENGINE_VIEWPORT.width);
    assert.equal(geometry.bodyWidth, REAL_ENGINE_VIEWPORT.width);
    assertVisibleRectangle(
      geometry.dock,
      geometry.viewport,
      "post-block priority dock",
    );

    t.diagnostic(
      "The opaque empty-blocker decision auto-submitted exactly once and " +
        "settled on second-main priority while Bluff mode remained enabled",
    );
  },
);

test(
  "real engine auto-passes forced priority and empty attackers with Bluff off",
  { timeout: 60_000 },
  async (t) => {
    const { server, url } = await startRealEngine();
    t.after(() => closeServer(server));
    const browser = await launchBrowser();
    t.after(() => browser.close());
    const context = await browser.newContext({ viewport: REAL_ENGINE_VIEWPORT });
    t.after(() => context.close());
    const page = await context.newPage();
    const actionPayloads = [];
    page.on("request", (request) => {
      if (isActionRequest(request)) {
        actionPayloads.push(request.postDataJSON());
      }
    });

    await page.goto(url, { waitUntil: "networkidle" });
    await configureRealEngineMatch(page, { bluffMode: false });

    const responses = await captureActionResponses(page, 2, () =>
      playOpeningForest(page),
    );

    assert.equal(responses[0].game.snapshot.phase, "declare_attackers");
    assert.equal(responses[0].game.config.bluffMode, false);
    assert.equal(responses[0].game.decision.kind, "attackers");
    assert.deepEqual(responses[0].game.decision.eligible, []);
    assert.equal(
      responses[0].game.events.at(-1).kind,
      "priority_action",
      "engine must settle forced priority before asking for attackers",
    );
    assert.equal(responses[1].game.snapshot.turnNumber, 3);
    assert.equal(responses[1].game.snapshot.phase, "first_main");
    assert.equal(responses[1].game.decision.kind, "priority");
    assert.deepEqual(actionPayloads, [
      { decisionId: 1, index: 1 },
      {
        decisionId: responses[0].game.decision.id,
        ids: [],
      },
    ]);

    await page.waitForFunction(
      () =>
        document.querySelector(".turn-marker strong")?.textContent?.trim() ===
        "3",
    );
    assert.equal(
      await page.getByLabel("Current phase: first_main").count(),
      1,
    );
    assert.equal(
      await page.getByText("Continue — no attackers", { exact: true }).count(),
      0,
    );
    assert.equal(await page.getByRole("alert").count(), 0);

    t.diagnostic(
      "Bluff off: land action reached engine empty attackers, then the client " +
        "submitted that exact decision once and advanced to turn 3",
    );
  },
);

test(
  "real engine keeps forced priority and empty attackers visible in Bluff mode",
  { timeout: 60_000 },
  async (t) => {
    const { server, url } = await startRealEngine();
    t.after(() => closeServer(server));
    const browser = await launchBrowser();
    t.after(() => browser.close());
    const context = await browser.newContext({ viewport: REAL_ENGINE_VIEWPORT });
    t.after(() => context.close());
    const page = await context.newPage();
    const actionPayloads = [];
    page.on("request", (request) => {
      if (isActionRequest(request)) {
        actionPayloads.push(request.postDataJSON());
      }
    });

    await page.goto(url, { waitUntil: "networkidle" });
    await configureRealEngineMatch(page, { bluffMode: true });

    const [afterLand] = await captureActionResponses(page, 1, () =>
      playOpeningForest(page),
    );
    assert.equal(afterLand.game.snapshot.phase, "first_main");
    assert.equal(afterLand.game.config.bluffMode, true);
    assert.equal(afterLand.game.decision.kind, "priority");
    assert.deepEqual(
      afterLand.game.decision.options.map(({ kind }) => kind),
      ["pass"],
    );
    assert.deepEqual(actionPayloads, [{ decisionId: 1, index: 1 }]);

    const [afterFirstMainPass] = await captureActionResponses(page, 1, () =>
      page.getByRole("button", { name: "Pass priority" }).click(),
    );
    assert.equal(afterFirstMainPass.game.snapshot.phase, "begin_combat");
    assert.equal(afterFirstMainPass.game.decision.kind, "priority");
    assert.deepEqual(
      afterFirstMainPass.game.decision.options.map(({ kind }) => kind),
      ["pass"],
    );
    assert.deepEqual(actionPayloads, [
      { decisionId: 1, index: 1 },
      { decisionId: afterLand.game.decision.id, index: 0 },
    ]);

    const [emptyAttackers] = await captureActionResponses(page, 1, () =>
      page.getByRole("button", { name: "Pass priority" }).click(),
    );
    assert.equal(emptyAttackers.game.snapshot.phase, "declare_attackers");
    assert.equal(emptyAttackers.game.decision.kind, "attackers");
    assert.deepEqual(emptyAttackers.game.decision.eligible, []);
    assert.equal(
      await page
        .getByText(
          "Bluff mode paused before declaring no attackers.",
          { exact: true },
        )
        .count(),
      1,
    );
    const continueWithoutAttackers = page.getByRole("button", {
      name: "Continue — no attackers",
    });
    assert.equal(await continueWithoutAttackers.count(), 1);
    assert.equal(await continueWithoutAttackers.isEnabled(), true);
    assert.deepEqual(actionPayloads, [
      { decisionId: 1, index: 1 },
      { decisionId: afterLand.game.decision.id, index: 0 },
      { decisionId: afterFirstMainPass.game.decision.id, index: 0 },
    ]);
    assert.equal(await page.getByRole("alert").count(), 0);

    t.diagnostic(
      "Bluff on: first-main Pass, beginning-combat Pass, and the empty " +
        "attacker decision all remained explicit with no fourth request",
    );
  },
);

function assertStableGeometry(metrics) {
  assert.equal(metrics.documentWidth, metrics.viewport.width);
  assert.equal(metrics.bodyWidth, metrics.viewport.width);
  assertVisibleRectangle(metrics.hand, metrics.viewport, "human hand");
  assertVisibleRectangle(metrics.dock, metrics.viewport, "decision dock");
  assertVisibleRectangle(metrics.stack, metrics.viewport, "stack rail");
  assert.ok(
    metrics.handDockOverlap <= 0.5,
    `hand and decision dock overlap by ${metrics.handDockOverlap}px²`,
  );
}

for (const viewport of VIEWPORTS) {
  test(
    `native Giant Growth drag routes once at ${viewport.width}x${viewport.height}`,
    { timeout: 60_000 },
    async (t) => {
      const { server, url } = await startFixture();
      t.after(() => closeServer(server));
      const browser = await launchBrowser();
      t.after(() => browser.close());
      const context = await browser.newContext({ viewport });
      t.after(() => context.close());
      const page = await context.newPage();
      let actionRequests = 0;
      page.on("request", (request) => {
        const requestUrl = new URL(request.url());
        if (
          request.method() === "POST" &&
          ACTION_PATH.test(requestUrl.pathname)
        ) {
          actionRequests += 1;
        }
      });

      await page.goto(url, { waitUntil: "networkidle" });
      await configureFixedMatch(page);

      const chronicle = page.locator(".match-log");
      const showPriorityPasses = chronicle.getByRole("checkbox", {
        name: "Show priority passes",
      });
      const hand = page.getByRole("list", { name: "Cards in your hand" });
      const giantGrowth = hand
        .getByRole("button", { name: "Giant Growth, instant, Cost G" })
        .first();
      const bears = page.getByRole("button", {
        name: /Grizzly Bears, permanent #110,/,
      });
      assert.equal(await hand.getByRole("listitem").count(), 7);
      assert.equal(
        await hand.locator('.card-face[draggable="true"]').count(),
        2,
        "only the two legal Giant Growth copies may be draggable",
      );
      assert.equal(await page.locator(".stack-entry").count(), 2);
      assert.equal(
        await page.getByText("TARGET", { exact: true }).count(),
        2,
        "both public stack targets must remain explicit",
      );
      assert.match(
        await page.getByRole("complementary", { name: "The stack" }).innerText(),
        /Lightning Bolt[\s\S]*Grizzly Bears #110/,
      );
      assertStackControllerCues(
        await stackControllerMetrics(page),
        ["OPPONENT", "YOU"],
      );
      assert.equal(await showPriorityPasses.isChecked(), false);
      assert.equal(await showPriorityPasses.isVisible(), true);
      assert.equal(
        await chronicle.getByText("You: Pass priority", { exact: true }).count(),
        0,
      );
      assert.deepEqual(
        await chronicle.locator(".event-turn").allTextContents(),
        ["Turn 4"],
      );
      assertStableGeometry(await renderedGeometry(page));

      const responsePromise = page.waitForResponse(
        (response) => {
          const responseUrl = new URL(response.url());
          return (
            response.request().method() === "POST" &&
            ACTION_PATH.test(responseUrl.pathname)
          );
        },
        { timeout: 10_000 },
      );
      await nativePointerDrag(page, giantGrowth, bears);
      const response = await responsePromise;
      assert.equal(response.ok(), true);
      const responseBody = await response.json();
      assert.equal(responseBody.game.decision.id, "target-stack-priority-2");
      assert.equal(responseBody.game.snapshot.stack.length, 3);
      assert.equal(responseBody.game.snapshot.players[0].hand.length, 6);
      assert.equal(responseBody.game.snapshot.players[0].lands[1].tapped, true);

      await page.waitForFunction(
        () => document.querySelectorAll(".stack-entry").length === 3,
      );
      assert.equal(actionRequests, 1, "one pointer drag must submit exactly once");
      assert.equal(await hand.getByRole("listitem").count(), 6);
      assert.equal(await page.locator(".stack-entry").count(), 3);
      assert.equal(await page.getByText("TARGET", { exact: true }).count(), 2);
      assertStackControllerCues(
        await stackControllerMetrics(page),
        ["YOU", "OPPONENT", "YOU"],
      );
      assert.deepEqual(
        await chronicle.locator(".event-index").allTextContents(),
        ["01", "02"],
      );
      assert.deepEqual(
        await chronicle.locator(".event-turn").allTextContents(),
        ["Turn 4"],
      );
      assert.equal(await page.getByRole("alert").count(), 0);
      assert.equal(
        await page
          .getByRole("button", { name: /Forest, permanent #102,/ })
          .getAttribute("aria-label"),
        "Forest, permanent #102, land, Tapped",
      );
      assertStableGeometry(await renderedGeometry(page));

      const pass = page.getByRole("button", { name: "Pass toward resolution" });
      const passResponsePromise = page.waitForResponse((candidate) => {
        const responseUrl = new URL(candidate.url());
        return (
          candidate.request().method() === "POST" &&
          ACTION_PATH.test(responseUrl.pathname)
        );
      });
      await pass.click();
      const passResponse = await passResponsePromise;
      assert.equal(passResponse.ok(), true);
      const passBody = await passResponse.json();
      assert.equal(passBody.game.decision.id, "target-stack-priority-3");
      assert.equal(passBody.game.snapshot.stack.length, 2);
      await page.waitForFunction(
        () => document.querySelectorAll(".stack-entry").length === 2,
      );
      assert.equal(actionRequests, 2);
      assertStackControllerCues(
        await stackControllerMetrics(page),
        ["OPPONENT", "YOU"],
      );
      assert.equal(
        await chronicle.getByText("You: Pass priority", { exact: true }).count(),
        0,
      );
      assert.deepEqual(
        await chronicle.locator(".event-index").allTextContents(),
        ["01", "02", "03"],
      );
      assert.deepEqual(
        await chronicle.locator(".event-turn").allTextContents(),
        ["Turn 4"],
      );
      await showPriorityPasses.check();
      assert.equal(await showPriorityPasses.isChecked(), true);
      assert.equal(
        await chronicle.getByText("You: Pass priority", { exact: true }).count(),
        1,
      );
      assert.deepEqual(
        await chronicle.locator(".event-index").allTextContents(),
        ["01", "02", "03", "04"],
      );
      assert.deepEqual(
        await chronicle.locator(".event-turn").allTextContents(),
        ["Turn 4"],
      );
      assert.equal(
        await chronicle.locator(".event-count").innerText(),
        "4",
      );
      await showPriorityPasses.uncheck();
      assert.equal(
        await chronicle.getByText("You: Pass priority", { exact: true }).count(),
        0,
      );
      assert.equal(
        await chronicle.locator(".event-count").innerText(),
        "3",
      );
      assert.equal(await page.getByRole("alert").count(), 0);
      assertStableGeometry(await renderedGeometry(page));

      t.diagnostic(
        `${viewport.width}x${viewport.height}: 7→6 hand, 2→3→2 stack, ` +
          "priority passes hidden→shown→hidden with one Turn 4 marker, " +
          `${actionRequests} exact action requests, no horizontal overflow`,
      );
    },
  );
}
