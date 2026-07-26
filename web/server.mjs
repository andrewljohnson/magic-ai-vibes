#!/usr/bin/env node

import { spawn as nodeSpawn } from "node:child_process";
import { randomBytes, randomUUID } from "node:crypto";
import { createReadStream, existsSync, statSync } from "node:fs";
import { createServer as createHttpServer } from "node:http";
import { fileURLToPath, pathToFileURL } from "node:url";
import path from "node:path";
import readline from "node:readline";

const SERVER_DIRECTORY = path.dirname(fileURLToPath(import.meta.url));
const UINT64_MAX = 18_446_744_073_709_551_615n;
const MAX_REQUEST_BYTES = 64 * 1024;
const MAX_LOG_ENTRIES = 2_000;
const FROZEN_C16_GENERATIONS = 16;
const FROZEN_C16_TRAIN_GAMES = 800;
const FROZEN_C16_TRAIN_SEED = "424242";

export const DECKS = Object.freeze([
  {
    id: "green",
    label: "Green",
    name: "Green",
    colors: ["green"],
    deckList:
      "18 Forest / 9 Grizzly Bears / 8 Ironroot Treefolk / 4 Giant Growth / 1 Tsunami",
    cards: [
      { name: "Forest", count: 18 },
      { name: "Grizzly Bears", count: 9 },
      { name: "Ironroot Treefolk", count: 8 },
      { name: "Giant Growth", count: 4 },
      { name: "Tsunami", count: 1 },
    ],
  },
  {
    id: "red",
    label: "Red",
    name: "Red",
    colors: ["red"],
    deckList:
      "15 Mountain / 9 Lightning Bolt / 7 Ironclaw Orcs / 4 Gray Ogre / 3 Hill Giant / 2 Fire Elemental",
    cards: [
      { name: "Mountain", count: 15 },
      { name: "Lightning Bolt", count: 9 },
      { name: "Ironclaw Orcs", count: 7 },
      { name: "Gray Ogre", count: 4 },
      { name: "Hill Giant", count: 3 },
      { name: "Fire Elemental", count: 2 },
    ],
  },
  {
    id: "blue",
    label: "Blue",
    name: "Blue",
    colors: ["blue"],
    deckList:
      "15 Island / 1 Mox Sapphire / 1 Sol Ring / 1 Ancestral Recall / 1 Time Walk / 1 Braingeyser / 4 Flying Men / 4 Force Spike / 8 Counterspell / 4 Air Elemental",
    cards: [
      { name: "Island", count: 15 },
      { name: "Mox Sapphire", count: 1 },
      { name: "Sol Ring", count: 1 },
      { name: "Ancestral Recall", count: 1 },
      { name: "Time Walk", count: 1 },
      { name: "Braingeyser", count: 1 },
      { name: "Flying Men", count: 4 },
      { name: "Force Spike", count: 4 },
      { name: "Counterspell", count: 8 },
      { name: "Air Elemental", count: 4 },
    ],
  },
  {
    id: "white",
    label: "White",
    name: "White",
    colors: ["white"],
    deckList: "22 Plains / 3 Millstone / 15 Moat",
    cards: [
      { name: "Plains", count: 22 },
      { name: "Millstone", count: 3 },
      { name: "Moat", count: 15 },
    ],
  },
  {
    id: "ru-aggro",
    label: "RU Aggro",
    name: "RU Aggro",
    colors: ["red", "blue"],
    deckList:
      "13 Mountain / 4 Island / 3 Flying Men / 5 Ironclaw Orcs / 2 Gray Ogre / 8 Hill Giant / 3 Lightning Bolt / 2 Disintegrate",
    cards: [
      { name: "Mountain", count: 13 },
      { name: "Island", count: 4 },
      { name: "Flying Men", count: 3 },
      { name: "Ironclaw Orcs", count: 5 },
      { name: "Gray Ogre", count: 2 },
      { name: "Hill Giant", count: 8 },
      { name: "Lightning Bolt", count: 3 },
      { name: "Disintegrate", count: 2 },
    ],
  },
]);

export const POLICIES = Object.freeze([
  {
    id: "random",
    label: "Random",
    name: "Random",
    description: "Chooses uniformly from legal actions.",
  },
  {
    id: "monte-carlo",
    label: "Monte Carlo",
    name: "Monte Carlo",
    description: "Samples short random futures before each choice.",
  },
  {
    id: "deep-monte-carlo",
    label: "Deep Monte Carlo",
    name: "Deep Monte Carlo",
    description: "Uses a larger rollout budget for each choice.",
  },
  {
    id: "handcrafted",
    label: "Handcoded Policy",
    name: "Handcoded Policy",
    description: "The compact rules-aware benchmark policy.",
  },
  {
    id: "learned-value-c16",
    label: "Learned Value C16",
    name: "Learned Value C16",
    description: "Frozen research baseline · C16 · K8/H4.",
  },
  {
    id: "learned-value-g0",
    label: "Learned Value G0",
    name: "Learned Value G0",
    description: "Trainable legacy model for quick test matches.",
  },
  {
    id: "learned-actor",
    label: "Learned Actor",
    name: "Learned Actor",
    description: "The learned actor-policy research model.",
  },
]);

const DECK_IDS = new Set(DECKS.map(({ id }) => id));
const POLICY_IDS = new Set(POLICIES.map(({ id }) => id));

const DEFAULT_CONFIG = Object.freeze({
  players: [
    { deckId: "ru-aggro", policyId: "human" },
    { deckId: "ru-aggro", policyId: "learned-value-c16" },
  ],
  trainGames: FROZEN_C16_TRAIN_GAMES,
  trainSeed: FROZEN_C16_TRAIN_SEED,
  debugReveal: false,
  bluffMode: false,
  rollouts: 2,
  deepRollouts: 8,
  learnedRollouts: 8,
  learnedGenerations: FROZEN_C16_GENERATIONS,
});

const LIMITS = Object.freeze({
  trainGames: { min: 1, max: 100_000 },
  rollouts: { min: 1, max: 4_096 },
  deepRollouts: { min: 1, max: 4_096 },
  learnedRollouts: { min: 1, max: 4_096 },
  learnedGenerations: { min: 0, max: FROZEN_C16_GENERATIONS },
});

const MIME_TYPES = new Map([
  [".css", "text/css; charset=utf-8"],
  [".gif", "image/gif"],
  [".html", "text/html; charset=utf-8"],
  [".ico", "image/x-icon"],
  [".jpeg", "image/jpeg"],
  [".jpg", "image/jpeg"],
  [".js", "text/javascript; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".map", "application/json; charset=utf-8"],
  [".mjs", "text/javascript; charset=utf-8"],
  [".png", "image/png"],
  [".svg", "image/svg+xml"],
  [".txt", "text/plain; charset=utf-8"],
  [".webp", "image/webp"],
  [".woff", "font/woff"],
  [".woff2", "font/woff2"],
]);

class ApiError extends Error {
  constructor(status, code, message, details) {
    super(message);
    this.name = "ApiError";
    this.status = status;
    this.code = code;
    this.details = details;
  }
}

function isRecord(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function normalizedPublicModelIdentity(value) {
  if (!isRecord(value)) return null;
  if (
    typeof value.family !== "string" ||
    !Number.isSafeInteger(value.generation) ||
    value.generation < 0 ||
    !Number.isSafeInteger(value.searchWorlds) ||
    value.searchWorlds < 1 ||
    !Number.isSafeInteger(value.horizonTurns) ||
    value.horizonTurns < 0 ||
    typeof value.source !== "string" ||
    typeof value.fingerprint !== "string" ||
    !/^[0-9a-f]{64}$/.test(value.fingerprint)
  ) {
    return null;
  }
  return {
    family: value.family,
    generation: value.generation,
    searchWorlds: value.searchWorlds,
    horizonTurns: value.horizonTurns,
    source: value.source,
    fingerprint: value.fingerprint,
  };
}

function normalizedUint64(value, fieldName, fallback) {
  const candidate = value === undefined ? fallback : value;
  if (typeof candidate === "number") {
    if (!Number.isSafeInteger(candidate) || candidate < 0) {
      throw new ApiError(
        400,
        "invalid_config",
        `${fieldName} must be a non-negative safe integer or uint64 decimal string`,
      );
    }
    return String(candidate);
  }
  if (
    typeof candidate !== "string" ||
    !/^(0|[1-9][0-9]*)$/.test(candidate)
  ) {
    throw new ApiError(
      400,
      "invalid_config",
      `${fieldName} must be a non-negative safe integer or uint64 decimal string`,
    );
  }
  let parsed;
  try {
    parsed = BigInt(candidate);
  } catch {
    throw new ApiError(
      400,
      "invalid_config",
      `${fieldName} is not a valid uint64`,
    );
  }
  if (parsed > UINT64_MAX) {
    throw new ApiError(
      400,
      "invalid_config",
      `${fieldName} is larger than uint64`,
    );
  }
  return candidate;
}

function positiveBoundedInteger(value, fieldName, limits, fallback) {
  const candidate = value === undefined ? fallback : value;
  if (
    !Number.isSafeInteger(candidate) ||
    candidate < limits.min ||
    candidate > limits.max
  ) {
    throw new ApiError(
      400,
      "invalid_config",
      `${fieldName} must be an integer from ${limits.min} to ${limits.max}`,
    );
  }
  return candidate;
}

function boundedInteger(value, fieldName, limits, fallback) {
  const candidate = value === undefined ? fallback : value;
  if (
    !Number.isSafeInteger(candidate) ||
    candidate < limits.min ||
    candidate > limits.max
  ) {
    throw new ApiError(
      400,
      "invalid_config",
      `${fieldName} must be an integer from ${limits.min} to ${limits.max}`,
    );
  }
  return candidate;
}

function validatedDeck(value, fieldName, fallback) {
  const candidate = value === undefined ? fallback : value;
  if (typeof candidate !== "string" || !DECK_IDS.has(candidate)) {
    throw new ApiError(
      400,
      "invalid_config",
      `${fieldName} must be one of: ${[...DECK_IDS].join(", ")}`,
    );
  }
  return candidate;
}

function validatedPolicy(value, fieldName, fallback) {
  const candidate = value === undefined ? fallback : value;
  if (typeof candidate !== "string" || !POLICY_IDS.has(candidate)) {
    throw new ApiError(
      400,
      "invalid_config",
      `${fieldName} must be one of: ${[...POLICY_IDS].join(", ")}`,
    );
  }
  return candidate;
}

function freshSeed() {
  return BigInt(`0x${randomBytes(8).toString("hex")}`).toString();
}

export function normalizeGameConfig(body) {
  if (!isRecord(body)) {
    throw new ApiError(
      400,
      "invalid_config",
      "The new-game body must be a JSON object",
    );
  }

  let humanDeck;
  let opponentDeck;
  let opponentPolicy;
  if (body.players !== undefined) {
    if (
      !Array.isArray(body.players) ||
      body.players.length !== 2 ||
      !isRecord(body.players[0]) ||
      !isRecord(body.players[1])
    ) {
      throw new ApiError(
        400,
        "invalid_config",
        "players must contain exactly a human player and an opponent",
      );
    }
    if (body.players[0].policyId !== "human") {
      throw new ApiError(
        400,
        "invalid_config",
        "players[0].policyId must be human",
      );
    }
    humanDeck = body.players[0].deckId;
    opponentDeck = body.players[1].deckId;
    opponentPolicy = body.players[1].policyId;
  } else {
    humanDeck = body.humanDeck ?? body.playerDeck;
    opponentDeck = body.opponentDeck ?? body.botDeck;
    opponentPolicy =
      body.opponentPolicy ?? body.botPolicy ?? body.policy;
  }

  const debugReveal = body.debugReveal ?? body.debug ?? false;
  if (typeof debugReveal !== "boolean") {
    throw new ApiError(
      400,
      "invalid_config",
      "debugReveal must be a boolean",
    );
  }
  const bluffMode = body.bluffMode ?? false;
  if (typeof bluffMode !== "boolean") {
    throw new ApiError(
      400,
      "invalid_config",
      "bluffMode must be a boolean",
    );
  }

  const normalizedOpponentPolicy = validatedPolicy(
    opponentPolicy,
    "players[1].policyId",
    DEFAULT_CONFIG.players[1].policyId,
  );
  const trainGames = positiveBoundedInteger(
    body.trainGames,
    "trainGames",
    LIMITS.trainGames,
    DEFAULT_CONFIG.trainGames,
  );
  const trainSeed = normalizedUint64(
    body.trainSeed,
    "trainSeed",
    DEFAULT_CONFIG.trainSeed,
  );
  const expectedLearnedGenerations =
    normalizedOpponentPolicy === "learned-value-c16"
      ? FROZEN_C16_GENERATIONS
      : 0;
  const learnedGenerations = boundedInteger(
    body.learnedGenerations,
    "learnedGenerations",
    LIMITS.learnedGenerations,
    expectedLearnedGenerations,
  );
  if (learnedGenerations !== expectedLearnedGenerations) {
    throw new ApiError(
      400,
      "invalid_config",
      `${normalizedOpponentPolicy} requires learnedGenerations=${expectedLearnedGenerations}`,
    );
  }
  if (
    normalizedOpponentPolicy === "learned-value-c16" &&
    (trainGames !== FROZEN_C16_TRAIN_GAMES ||
      trainSeed !== FROZEN_C16_TRAIN_SEED)
  ) {
    throw new ApiError(
      400,
      "invalid_config",
      "Learned Value C16 requires trainGames=800 and trainSeed=424242; select Learned Value G0 for custom match training",
    );
  }

  return {
    players: [
      {
        deckId: validatedDeck(
          humanDeck,
          "players[0].deckId",
          DEFAULT_CONFIG.players[0].deckId,
        ),
        policyId: "human",
      },
      {
        deckId: validatedDeck(
          opponentDeck,
          "players[1].deckId",
          DEFAULT_CONFIG.players[1].deckId,
        ),
        policyId: normalizedOpponentPolicy,
      },
    ],
    seed: normalizedUint64(body.seed, "seed", freshSeed()),
    trainGames,
    trainSeed,
    debugReveal,
    bluffMode,
    rollouts: positiveBoundedInteger(
      body.rollouts,
      "rollouts",
      LIMITS.rollouts,
      DEFAULT_CONFIG.rollouts,
    ),
    deepRollouts: positiveBoundedInteger(
      body.deepRollouts,
      "deepRollouts",
      LIMITS.deepRollouts,
      DEFAULT_CONFIG.deepRollouts,
    ),
    learnedRollouts: positiveBoundedInteger(
      body.learnedRollouts,
      "learnedRollouts",
      LIMITS.learnedRollouts,
      DEFAULT_CONFIG.learnedRollouts,
    ),
    learnedGenerations,
  };
}

function bridgeArguments(config) {
  const args = [
    "--human-deck",
    config.players[0].deckId,
    "--opponent-deck",
    config.players[1].deckId,
    "--opponent-policy",
    config.players[1].policyId,
    "--seed",
    config.seed,
    "--train-games",
    String(config.trainGames),
    "--train-seed",
    config.trainSeed,
    "--rollouts",
    String(config.rollouts),
    "--deep-rollouts",
    String(config.deepRollouts),
    "--learned-rollouts",
    String(config.learnedRollouts),
    "--learned-generations",
    String(config.learnedGenerations),
  ];
  if (config.debugReveal) {
    args.push("--debug-reveal");
  }
  if (config.bluffMode) {
    args.push("--bluff-mode");
  }
  return args;
}

function decisionIdMatches(left, right) {
  const scalar = (value) =>
    typeof value === "string" || typeof value === "number";
  return scalar(left) && scalar(right) && String(left) === String(right);
}

function uniqueScalars(values) {
  const normalized = values.map((value) => String(value));
  return new Set(normalized).size === normalized.length;
}

function includesScalar(values, candidate) {
  return values.some((value) => decisionIdMatches(value, candidate));
}

function validateActionForDecision(input, decision) {
  if (!isRecord(input)) {
    throw new ApiError(
      400,
      "invalid_action",
      "The action body must be a JSON object",
    );
  }
  if (!decisionIdMatches(input.decisionId, decision.id)) {
    throw new ApiError(
      409,
      "stale_decision",
      "That choice belongs to an old or different decision",
      { expectedDecisionId: decision.id },
    );
  }
  if (input.kind !== undefined && input.kind !== decision.kind) {
    throw new ApiError(
      409,
      "wrong_decision_kind",
      `Expected a ${decision.kind} choice`,
      { expectedKind: decision.kind },
    );
  }

  if (decision.kind === "priority") {
    if (!Number.isSafeInteger(input.index)) {
      throw new ApiError(
        422,
        "illegal_action",
        "A priority choice needs an integer option index",
      );
    }
    const options = Array.isArray(decision.options) ? decision.options : [];
    if (!options.some((option) => option?.index === input.index)) {
      throw new ApiError(
        422,
        "illegal_action",
        "That priority option is not legal",
      );
    }
    return { decisionId: decision.id, index: input.index };
  }

  if (decision.kind === "attackers") {
    if (!Array.isArray(input.ids) || !uniqueScalars(input.ids)) {
      throw new ApiError(
        422,
        "illegal_action",
        "Attackers must be a unique array of permanent IDs",
      );
    }
    const eligible = Array.isArray(decision.eligible)
      ? decision.eligible
      : [];
    if (!input.ids.every((id) => includesScalar(eligible, id))) {
      throw new ApiError(
        422,
        "illegal_action",
        "At least one selected attacker is not eligible",
      );
    }
    return { decisionId: decision.id, ids: input.ids };
  }

  if (decision.kind === "blockers") {
    if (
      !Array.isArray(input.pairs) ||
      !input.pairs.every(
        (pair) => Array.isArray(pair) && pair.length === 2,
      )
    ) {
      throw new ApiError(
        422,
        "illegal_action",
        "Blocks must be [attacker, blocker] pairs",
      );
    }
    const attackers = input.pairs.map(([attacker]) => attacker);
    const blockers = input.pairs.map(([, blocker]) => blocker);
    if (!uniqueScalars(blockers)) {
      throw new ApiError(
        422,
        "illegal_action",
        "A blocker may only block once",
      );
    }
    const declaredAttackers = Array.isArray(decision.attackers)
      ? decision.attackers
      : [];
    const choices = Array.isArray(decision.choices)
      ? decision.choices
      : [];
    const legal = input.pairs.every(([attacker, blocker]) => {
      if (!includesScalar(declaredAttackers, attacker)) {
        return false;
      }
      const choice = choices.find((entry) =>
        decisionIdMatches(entry?.blocker, blocker),
      );
      return (
        choice !== undefined &&
        Array.isArray(choice.legalAttackers) &&
        includesScalar(choice.legalAttackers, attacker)
      );
    });
    if (!legal || attackers.length > choices.length) {
      throw new ApiError(
        422,
        "illegal_action",
        "At least one block assignment is not legal",
      );
    }
    return { decisionId: decision.id, pairs: input.pairs };
  }

  if (decision.kind === "damage_order") {
    if (
      !Array.isArray(input.ids) ||
      !uniqueScalars(input.ids) ||
      !Array.isArray(decision.blockers) ||
      input.ids.length !== decision.blockers.length ||
      !input.ids.every((id) => includesScalar(decision.blockers, id))
    ) {
      throw new ApiError(
        422,
        "illegal_action",
        "Damage order must be a permutation of every listed blocker",
      );
    }
    return { decisionId: decision.id, ids: input.ids };
  }

  if (decision.kind === "cleanup_discard") {
    const count = decision.count;
    const options = Array.isArray(decision.options) ? decision.options : [];
    if (
      !Number.isSafeInteger(count) ||
      count < 0 ||
      !Array.isArray(input.indices) ||
      input.indices.length !== count ||
      !input.indices.every(Number.isSafeInteger) ||
      !uniqueScalars(input.indices) ||
      !input.indices.every((index) =>
        options.some(
          (option) =>
            Number.isSafeInteger(option?.index) && option.index === index,
        ),
      )
    ) {
      throw new ApiError(
        422,
        "illegal_action",
        `Cleanup requires exactly ${Number.isSafeInteger(count) ? count : 0} unique legal hand positions`,
      );
    }
    return { decisionId: decision.id, indices: input.indices };
  }

  throw new ApiError(
    502,
    "bridge_protocol_error",
    `The bridge requested an unknown decision kind: ${decision.kind}`,
  );
}

class GameSession {
  constructor({
    id,
    config,
    bridgePath,
    bridgeArgsPrefix,
    spawnImpl,
    actionTimeoutMs,
  }) {
    this.id = id;
    this.config = config;
    this.status = "starting";
    this.snapshot = null;
    this.decision = null;
    this.result = null;
    this.model = null;
    this.error = null;
    this.message = "Starting game";
    this.events = [];
    this.log = [];
    this.latestEnvelope = null;
    this.stderr = "";
    this.closed = false;
    this.settlementVersion = 0;
    this.waiters = new Set();
    this.actionTimeoutMs = actionTimeoutMs;
    this.actionQueue = Promise.resolve();

    const args = [
      ...bridgeArgsPrefix,
      ...bridgeArguments(config),
    ];
    this.child = spawnImpl(bridgePath, args, {
      stdio: ["pipe", "pipe", "pipe"],
      windowsHide: true,
    });
    this.child.stdin.setDefaultEncoding("utf8");

    this.lines = readline.createInterface({
      input: this.child.stdout,
      crlfDelay: Infinity,
    });
    this.lines.on("line", (line) => this.#receiveLine(line));
    this.child.stderr.setEncoding("utf8");
    this.child.stderr.on("data", (chunk) => {
      this.stderr = `${this.stderr}${chunk}`.slice(-65_536);
    });
    this.child.once("error", (error) => {
      this.#fail(`Could not start the game engine: ${error.message}`);
    });
    this.child.once("exit", (code, signal) => {
      this.closed = true;
      if (this.status !== "finished" && this.status !== "failed") {
        const suffix =
          signal === null ? `code ${code ?? "unknown"}` : `signal ${signal}`;
        const detail = this.stderr.trim();
        this.#fail(
          `The game engine exited unexpectedly (${suffix})${
            detail ? `: ${detail}` : ""
          }`,
        );
      }
    });
  }

  #pushBounded(collection, entry) {
    collection.push(entry);
    if (collection.length > MAX_LOG_ENTRIES) {
      collection.splice(0, collection.length - MAX_LOG_ENTRIES);
    }
  }

  #receiveLine(line) {
    if (line.trim() === "" || this.status === "failed") {
      return;
    }
    let envelope;
    try {
      envelope = JSON.parse(line);
    } catch {
      this.#fail("The game engine produced invalid JSON");
      this.stop();
      return;
    }
    if (!isRecord(envelope) || typeof envelope.type !== "string") {
      this.#fail("The game engine produced an invalid message");
      this.stop();
      return;
    }

    this.latestEnvelope = envelope;
    if (envelope.type === "status") {
      if (typeof envelope.message === "string") {
        this.message = envelope.message;
      }
      if (envelope.model !== undefined) {
        const model = normalizedPublicModelIdentity(envelope.model);
        if (model === null) {
          this.#fail("The game engine produced invalid model identity");
          this.stop();
          return;
        }
        this.model = model;
      }
      this.#pushBounded(this.log, envelope);
      return;
    }
    if (envelope.type === "event") {
      if (envelope.state !== undefined) {
        this.snapshot = envelope.state;
      }
      const rawEvent = envelope.event ?? envelope;
      const publicEvent = isRecord(rawEvent)
        ? {
            ...rawEvent,
            ...(typeof rawEvent.label === "string"
              ? { message: rawEvent.label }
              : {}),
            ...(Number.isSafeInteger(envelope.state?.turnNumber)
              ? { turn: envelope.state.turnNumber }
              : {}),
          }
        : rawEvent;
      this.#pushBounded(this.events, publicEvent);
      this.#pushBounded(this.log, {
        type: "event",
        event: publicEvent,
      });
      return;
    }
    if (envelope.type === "decision") {
      if (!isRecord(envelope.decision)) {
        this.#fail("The game engine produced a decision without a prompt");
        this.stop();
        return;
      }
      this.snapshot = envelope.state ?? this.snapshot;
      this.decision = envelope.decision;
      this.status = "awaiting_action";
      this.message = "Your move";
      this.#settled();
      return;
    }
    if (envelope.type === "game_over") {
      this.snapshot = envelope.state ?? this.snapshot;
      this.result = envelope.result ?? null;
      this.decision = null;
      this.status = "finished";
      this.message = "Game over";
      this.#settled();
      return;
    }
    this.#fail(`The game engine produced unknown message type ${envelope.type}`);
    this.stop();
  }

  #settled() {
    this.settlementVersion += 1;
    for (const waiter of [...this.waiters]) {
      if (this.settlementVersion > waiter.afterVersion) {
        clearTimeout(waiter.timer);
        this.waiters.delete(waiter);
        waiter.resolve();
      }
    }
  }

  #fail(message) {
    if (this.status === "failed") {
      return;
    }
    this.status = "failed";
    this.decision = null;
    this.error = { code: "bridge_failed", message };
    this.message = message;
    this.#settled();
  }

  waitForSettled(afterVersion, timeoutMs) {
    if (this.settlementVersion > afterVersion) {
      return Promise.resolve();
    }
    return new Promise((resolve, reject) => {
      const waiter = {
        afterVersion,
        resolve,
        reject,
        timer: setTimeout(() => {
          this.waiters.delete(waiter);
          reject(
            new ApiError(
              504,
              "bridge_timeout",
              "The game engine took too long to reach the next decision",
            ),
          );
        }, timeoutMs),
      };
      this.waiters.add(waiter);
    });
  }

  publicState() {
    return {
      id: this.id,
      config: this.config,
      status: this.status,
      message: this.message,
      snapshot: this.snapshot,
      decision: this.decision,
      events: this.events,
      log: this.log,
      result: this.result,
      model: this.model,
      error: this.error,
    };
  }

  submitAction(input) {
    const operation = this.actionQueue.then(() =>
      this.#submitActionNow(input),
    );
    this.actionQueue = operation.catch(() => {});
    return operation;
  }

  async #submitActionNow(input) {
    if (this.status === "finished") {
      throw new ApiError(409, "game_over", "This game is already over");
    }
    if (this.status === "failed") {
      throw new ApiError(409, "game_failed", "This game engine has failed");
    }
    if (this.status !== "awaiting_action" || !isRecord(this.decision)) {
      throw new ApiError(
        409,
        "not_awaiting_action",
        "The game is not waiting for a choice",
      );
    }

    const action = validateActionForDecision(input, this.decision);
    const before = this.settlementVersion;
    this.status = "advancing";
    this.message = "Opponent thinking";
    this.decision = null;

    try {
      await new Promise((resolve, reject) => {
        this.child.stdin.write(`${JSON.stringify(action)}\n`, (error) => {
          if (error) {
            reject(
              new ApiError(
                502,
                "bridge_write_failed",
                "Could not send the choice to the game engine",
              ),
            );
          } else {
            resolve();
          }
        });
      });
      await this.waitForSettled(before, this.actionTimeoutMs);
    } catch (error) {
      this.#fail(error.message);
      this.stop();
      throw error;
    }
    return this.publicState();
  }

  stop() {
    if (!this.closed && this.child !== undefined) {
      this.child.kill("SIGTERM");
    }
    this.lines?.close();
  }
}

class GameManager {
  constructor(options) {
    this.sessions = new Map();
    this.bridgePath = options.bridgePath;
    this.bridgeArgsPrefix = options.bridgeArgsPrefix;
    this.spawnImpl = options.spawnImpl;
    this.initialTimeoutMs = options.initialTimeoutMs;
    this.actionTimeoutMs = options.actionTimeoutMs;
    this.idFactory = options.idFactory;
  }

  async create(config) {
    const id = this.idFactory();
    const session = new GameSession({
      id,
      config,
      bridgePath: this.bridgePath,
      bridgeArgsPrefix: this.bridgeArgsPrefix,
      spawnImpl: this.spawnImpl,
      actionTimeoutMs: this.actionTimeoutMs,
    });
    this.sessions.set(id, session);
    try {
      await session.waitForSettled(0, this.initialTimeoutMs);
    } catch (error) {
      this.sessions.delete(id);
      session.stop();
      throw error;
    }
    if (session.status === "failed") {
      this.sessions.delete(id);
      session.stop();
      throw new ApiError(
        502,
        "bridge_failed",
        session.error?.message ?? "The game engine failed to start a game",
      );
    }
    return session;
  }

  get(id) {
    const session = this.sessions.get(id);
    if (session === undefined) {
      throw new ApiError(404, "game_not_found", "No such game session");
    }
    return session;
  }

  delete(id) {
    const session = this.get(id);
    this.sessions.delete(id);
    session.stop();
  }

  shutdown() {
    for (const session of this.sessions.values()) {
      session.stop();
    }
    this.sessions.clear();
  }
}

function sendJson(response, status, payload, headers = {}) {
  const body = payload === undefined ? "" : JSON.stringify(payload);
  response.writeHead(status, {
    "cache-control": "no-store",
    "content-type": "application/json; charset=utf-8",
    "content-length": Buffer.byteLength(body),
    ...headers,
  });
  response.end(body);
}

function sendApiError(response, error) {
  const apiError =
    error instanceof ApiError
      ? error
      : new ApiError(500, "internal_error", "Internal server error");
  sendJson(response, apiError.status, {
    error: {
      code: apiError.code,
      message: apiError.message,
      ...(apiError.details === undefined
        ? {}
        : { details: apiError.details }),
    },
  });
}

async function readJson(request) {
  const chunks = [];
  let bytes = 0;
  for await (const chunk of request) {
    bytes += chunk.length;
    if (bytes > MAX_REQUEST_BYTES) {
      throw new ApiError(
        413,
        "request_too_large",
        "The JSON request is too large",
      );
    }
    chunks.push(chunk);
  }
  if (chunks.length === 0) {
    return {};
  }
  try {
    return JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch {
    throw new ApiError(400, "invalid_json", "Request body is not valid JSON");
  }
}

function methodNotAllowed(response, methods) {
  sendJson(
    response,
    405,
    {
      error: {
        code: "method_not_allowed",
        message: `Use ${methods.join(" or ")} for this resource`,
      },
    },
    { allow: methods.join(", ") },
  );
}

export function webGameMetadata() {
  return {
    apiVersion: 1,
    decks: DECKS,
    policies: POLICIES,
    decisionKinds: [
      "priority",
      "attackers",
      "blockers",
      "damage_order",
      "cleanup_discard",
    ],
    defaults: DEFAULT_CONFIG,
    limits: LIMITS,
  };
}

async function handleApi(request, response, pathname, manager) {
  if (pathname === "/api/meta") {
    if (request.method !== "GET") {
      methodNotAllowed(response, ["GET"]);
      return;
    }
    sendJson(response, 200, webGameMetadata());
    return;
  }

  if (pathname === "/api/games") {
    if (request.method !== "POST") {
      methodNotAllowed(response, ["POST"]);
      return;
    }
    const config = normalizeGameConfig(await readJson(request));
    const session = await manager.create(config);
    sendJson(response, 201, { game: session.publicState() });
    return;
  }

  const match = /^\/api\/games\/([^/]+)(?:\/(actions))?$/.exec(pathname);
  if (match === null) {
    throw new ApiError(404, "not_found", "No such API resource");
  }
  const id = decodeURIComponent(match[1]);
  const isActions = match[2] === "actions";
  if (isActions) {
    if (request.method !== "POST") {
      methodNotAllowed(response, ["POST"]);
      return;
    }
    const game = await manager.get(id).submitAction(await readJson(request));
    sendJson(response, 200, { game });
    return;
  }
  if (request.method === "GET") {
    sendJson(response, 200, { game: manager.get(id).publicState() });
    return;
  }
  if (request.method === "DELETE") {
    manager.delete(id);
    response.writeHead(204, { "cache-control": "no-store" });
    response.end();
    return;
  }
  methodNotAllowed(response, ["GET", "DELETE"]);
}

function safeStaticPath(distDirectory, pathname) {
  let decoded;
  try {
    decoded = decodeURIComponent(pathname);
  } catch {
    return null;
  }
  if (decoded.includes("\0")) {
    return null;
  }
  const root = path.resolve(distDirectory);
  const candidate = path.resolve(root, `.${decoded}`);
  if (candidate !== root && !candidate.startsWith(`${root}${path.sep}`)) {
    return null;
  }
  return candidate;
}

function staticFileForRequest(distDirectory, pathname) {
  const candidate = safeStaticPath(distDirectory, pathname);
  if (candidate === null) {
    return null;
  }
  try {
    if (statSync(candidate).isFile()) {
      return candidate;
    }
    if (statSync(candidate).isDirectory()) {
      const index = path.join(candidate, "index.html");
      if (statSync(index).isFile()) {
        return index;
      }
    }
  } catch {
    // The SPA fallback below handles client-side routes and missing files.
  }
  if (path.extname(pathname) === "") {
    const index = path.join(path.resolve(distDirectory), "index.html");
    if (existsSync(index)) {
      return index;
    }
  }
  return null;
}

function serveStatic(request, response, pathname, distDirectory) {
  if (request.method !== "GET" && request.method !== "HEAD") {
    methodNotAllowed(response, ["GET", "HEAD"]);
    return;
  }
  const file = staticFileForRequest(distDirectory, pathname);
  if (file === null) {
    sendJson(response, 404, {
      error: { code: "not_found", message: "No such file" },
    });
    return;
  }
  const extension = path.extname(file).toLowerCase();
  const stats = statSync(file);
  response.writeHead(200, {
    "content-type":
      MIME_TYPES.get(extension) ?? "application/octet-stream",
    "content-length": stats.size,
    "cache-control":
      extension === ".html"
        ? "no-cache"
        : "public, max-age=31536000, immutable",
  });
  if (request.method === "HEAD") {
    response.end();
    return;
  }
  createReadStream(file).pipe(response);
}

function createGameManager(options = {}) {
  const defaultBridge = path.resolve(
    SERVER_DIRECTORY,
    "../build/old-school-web-bridge",
  );
  return new GameManager({
    bridgePath:
      options.bridgePath ??
      process.env.OLD_SCHOOL_WEB_BRIDGE ??
      defaultBridge,
    bridgeArgsPrefix: options.bridgeArgsPrefix ?? [],
    spawnImpl: options.spawnImpl ?? nodeSpawn,
    initialTimeoutMs: options.initialTimeoutMs ?? 120_000,
    actionTimeoutMs: options.actionTimeoutMs ?? 120_000,
    idFactory: options.idFactory ?? randomUUID,
  });
}

export function createGameContractHarness(options = {}) {
  const manager = createGameManager(options);
  return {
    metadata() {
      return webGameMetadata();
    },
    async create(config) {
      const session = await manager.create(normalizeGameConfig(config));
      return { game: session.publicState() };
    },
    get(id) {
      return { game: manager.get(id).publicState() };
    },
    async action(id, choice) {
      const game = await manager.get(id).submitAction(choice);
      return { game };
    },
    delete(id) {
      manager.delete(id);
    },
    shutdown() {
      manager.shutdown();
    },
  };
}

export function createServer(options = {}) {
  const distDirectory = path.resolve(
    options.distDirectory ??
      process.env.OLD_SCHOOL_WEB_DIST ??
      path.join(SERVER_DIRECTORY, "dist-game"),
  );
  const manager = createGameManager(options);

  const server = createHttpServer(async (request, response) => {
    try {
      const url = new URL(request.url ?? "/", "http://localhost");
      if (url.pathname.startsWith("/api/")) {
        await handleApi(request, response, url.pathname, manager);
      } else {
        serveStatic(request, response, url.pathname, distDirectory);
      }
    } catch (error) {
      sendApiError(response, error);
    }
  });

  server.gameManager = manager;
  server.once("close", () => manager.shutdown());
  return server;
}

export async function startServer(options = {}) {
  const server = createServer(options);
  const port = options.port ?? Number.parseInt(process.env.PORT ?? "4173", 10);
  const host = options.host ?? process.env.HOST ?? "127.0.0.1";
  if (!Number.isInteger(port) || port < 0 || port > 65_535) {
    throw new Error("PORT must be an integer from 0 to 65535");
  }
  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(port, host, resolve);
  });
  return server;
}

function runningAsMainModule() {
  return (
    process.argv[1] !== undefined &&
    import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href
  );
}

if (runningAsMainModule()) {
  const server = await startServer();
  const address = server.address();
  const displayHost =
    typeof address === "object" && address !== null
      ? address.address
      : "127.0.0.1";
  const displayPort =
    typeof address === "object" && address !== null ? address.port : 4173;
  process.stdout.write(
    `Old School Magic Arena: http://${displayHost}:${displayPort}\n`,
  );

  let shuttingDown = false;
  const shutdown = () => {
    if (shuttingDown) {
      return;
    }
    shuttingDown = true;
    server.gameManager.shutdown();
    server.close(() => process.exit(0));
  };
  process.once("SIGINT", shutdown);
  process.once("SIGTERM", shutdown);
}
