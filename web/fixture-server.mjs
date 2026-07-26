#!/usr/bin/env node

import { fileURLToPath } from "node:url";
import path from "node:path";

import { startServer } from "./server.mjs";

const directory = path.dirname(fileURLToPath(import.meta.url));
const fixtures = new Map([
  [
    "target-stack",
    path.join(directory, "tests", "fixtures", "target-stack-bridge.mjs"),
  ],
]);
const fixtureName = process.argv[2] ?? "target-stack";
const fixturePath = fixtures.get(fixtureName);

if (!fixturePath) {
  process.stderr.write(
    `Unknown web fixture ${JSON.stringify(fixtureName)}. Available: ${
      [...fixtures.keys()].join(", ")
    }\n`,
  );
  process.exitCode = 2;
} else {
  const server = await startServer({
    bridgePath: process.execPath,
    bridgeArgsPrefix: [fixturePath],
  });
  const address = server.address();
  const host =
    typeof address === "object" && address !== null
      ? address.address
      : "127.0.0.1";
  const port =
    typeof address === "object" && address !== null ? address.port : 4173;
  process.stdout.write(
    `Old School Arena (${fixtureName} fixture): http://${host}:${port}\n`,
  );

  let shuttingDown = false;
  const shutdown = () => {
    if (shuttingDown) return;
    shuttingDown = true;
    server.close(() => {
      process.exitCode = 0;
    });
  };
  process.once("SIGINT", shutdown);
  process.once("SIGTERM", shutdown);
}
