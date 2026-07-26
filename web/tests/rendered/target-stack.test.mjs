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
const VIEWPORTS = [
  { width: 1280, height: 720 },
  { width: 1440, height: 900 },
];
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

async function closeServer(server) {
  if (!server.listening) return;
  await new Promise((resolve, reject) => {
    server.close((error) => (error ? reject(error) : resolve()));
  });
}

async function configureFixedMatch(page) {
  const setup = page.getByRole("dialog", { name: "Set the table" });
  await setup.waitFor();
  const nearSeat = setup.locator(".seat-0");
  const farSeat = setup.locator(".seat-1");
  await nearSeat.locator("select").nth(0).selectOption("green");
  await farSeat.locator("select").nth(0).selectOption("red");
  await farSeat.locator("select").nth(1).selectOption("learned-value");

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

  await setup.getByRole("button", { name: "Start match" }).click();
  await setup.waitFor({ state: "hidden" });
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
      assert.equal(await page.getByRole("alert").count(), 0);
      assertStableGeometry(await renderedGeometry(page));

      t.diagnostic(
        `${viewport.width}x${viewport.height}: 7→6 hand, 2→3→2 stack, ` +
          `${actionRequests} exact action requests, no horizontal overflow`,
      );
    },
  );
}
