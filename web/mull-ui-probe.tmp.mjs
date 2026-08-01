import path from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";
import { startServer } from "/Users/andrewjohnson/proj/magic-ai-vibes/web/server.mjs";

const bridge = "/Users/andrewjohnson/proj/magic-ai-vibes/build/old-school-web-bridge";
const dist = "/Users/andrewjohnson/proj/magic-ai-vibes/web/dist-game";
const server = await startServer({
  port: 0, bridgePath: bridge, distDirectory: dist,
});
const port = server.address().port;
const browser = await chromium.launch({ headless: true, executablePath: "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
await page.goto(`http://127.0.0.1:${port}/`);
await page.waitForTimeout(1500);
console.log("landing buttons:", await page.evaluate(() =>
  [...document.querySelectorAll("button")].map(b => b.textContent.trim()).slice(0, 15)));
await page.getByRole("button", { name: /start match/i }).click();
await page.waitForTimeout(3500);
const tree = async (tag) => {
  const info = await page.evaluate(() => {
    const walk = (el, depth) => {
      if (depth > 4) return [];
      const r = el.getBoundingClientRect();
      const line = `${"  ".repeat(depth)}${el.tagName.toLowerCase()}.${[...el.classList].join(".")} top=${Math.round(r.top)} h=${Math.round(r.height)}`;
      return [line, ...[...el.children].flatMap(c => walk(c, depth+1))];
    };
    const main = document.querySelector("main") ?? document.body;
    return walk(main, 0);
  });
  console.log(tag); console.log(info.join("\n"));
};
const dump = async (tag) => {
  const info = await page.evaluate(() => {
    const rect = (sel) => {
      const el = document.querySelector(sel);
      if (!el) return null;
      const r = el.getBoundingClientRect();
      return { top: Math.round(r.top), height: Math.round(r.height), visible: r.height > 0 && r.width > 0 };
    };
    return {
      decisionHeading: document.querySelector(".decision-dock h2, .decision-dock h3")?.textContent ?? document.querySelector(".decision-dock")?.getAttribute("aria-label"),
      opponentSide: rect(".opponent-side"),
      playerSide: rect(".player-side"),
      hand: rect(".hand-region, .player-console, [aria-label='Your hand']"),
      dock: rect(".decision-dock"),
      buttons: [...document.querySelectorAll(".decision-dock button")].map(b => b.textContent),
      bodyScrollHeight: document.body.scrollHeight,
    };
  });
  console.log(tag, JSON.stringify(info, null, 1));
};
await dump("MULLIGAN-TIME:"); await tree("TREE-MULLIGAN:"); await page.screenshot({ path: "mull-shot-1.png", fullPage: false });
const keep = page.getByRole("button", { name: /keep this hand/i });
if (await keep.count()) {
  await keep.click();
  await page.waitForTimeout(3000);
  await tree("TREE-AFTER-KEEP:"); await page.screenshot({ path: "mull-shot-2.png", fullPage: false });
} else {
  console.log("no keep button found");
}
await browser.close();
server.close();
process.exit(0);
