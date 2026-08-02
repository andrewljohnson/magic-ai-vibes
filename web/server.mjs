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
const BUG_REPORT_SCHEMA = "old-school-arena-bug-report";
const BUG_REPORT_VERSION = 1;
const EVOLUTION_TIMEOUT_MS = 1_200_000;
const MAX_EVOLUTION_OUTPUT_BYTES = 512 * 1024;
const MAX_EVOLUTION_RESULTS = 32;
const MAX_SAVED_DECKS = 32;
const MAX_WEB_EVOLUTION_SEED = 4_294_967_295;

const CARD_NAMES = Object.freeze([
  "Forest",
  "Mountain",
  "Grizzly Bears",
  "Lightning Bolt",
  "Ironroot Treefolk",
  "Fire Elemental",
  "Island",
  "Counterspell",
  "Water Elemental",
  "Tsunami",
  "Plains",
  "Millstone",
  "Moat",
  "Flying Men",
  "Ironclaw Orcs",
  "Gray Ogre",
  "Hill Giant",
  "Disintegrate",
  "Giant Growth",
  "Mox Sapphire",
  "Sol Ring",
  "Ancestral Recall",
  "Time Walk",
  "Braingeyser",
  "Force Spike",
  "Air Elemental",
  "Black Lotus",
  "Channel",
  "Llanowar Elves",
  "Moss Beast",
  "Forest Colossus",
  "Savannah Lions",
  "Serendib Efreet",
  "Serra Angel",
  "Black Vise",
  "Mox Pearl",
  "Mox Ruby",
  "Disenchant",
  "Psionic Blast",
  "Swords to Plowshares",
  "Plateau",
  "Tundra",
  "Volcanic Island",
  "Wheel of Fortune",
  "Library of Alexandria",
  "Mishra's Factory",
  "Strip Mine",
  "Su-Chi",
  "Sage of Lat-Nam",
  "Triskelion",
  "Mana Drain",
  "Armageddon",
  "Demonic Tutor",
  "Fireball",
  "Mind Twist",
  "Recall",
  "Copy Artifact",
  "Fellwar Stone",
  "Mox Emerald",
  "Mox Jet",
  "City of Brass",
  "Underground Sea",
  "Badlands",
  "Timetwister",
]);

export const DECKS = Object.freeze([
  {
    id: "green",
    label: "Green Growth",
    name: "Green Growth",
    colors: ["green"],
    deckList:
      "16 Forest / 4 Llanowar Elves / 6 Grizzly Bears / 2 Ironroot Treefolk / 4 Moss Beast / 4 Forest Colossus / 4 Giant Growth",
    cards: [
      { name: "Forest", count: 16 },
      { name: "Llanowar Elves", count: 4 },
      { name: "Grizzly Bears", count: 6 },
      { name: "Ironroot Treefolk", count: 2 },
      { name: "Moss Beast", count: 4 },
      { name: "Forest Colossus", count: 4 },
      { name: "Giant Growth", count: 4 },
    ],
  },
  {
    id: "red",
    label: "Creatures & Bolts",
    name: "Creatures & Bolts",
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
    label: "Counter Flyer",
    name: "Counter Flyer",
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
    label: "Moat Mill",
    name: "Moat Mill",
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
  {
    id: "lotus-combo",
    label: "Lotus Combo",
    name: "Lotus Combo",
    colors: ["green", "red"],
    deckList: "20 Black Lotus / 10 Channel / 10 Disintegrate",
    cards: [
      { name: "Black Lotus", count: 20 },
      { name: "Channel", count: 10 },
      { name: "Disintegrate", count: 10 },
    ],
  },
  {
    id: "burn",
    label: "Burn",
    name: "Burn",
    colors: ["red"],
    deckList: "12 Mountain / 18 Lightning Bolt",
    cards: [
      { name: "Mountain", count: 12 },
      { name: "Lightning Bolt", count: 18 },
    ],
  },
  {
    id: "uwr",
    label: "Lion-dib-bolt",
    name: "Lion-dib-bolt",
    colors: ["white", "blue", "red"],
    deckList:
      "4 Savannah Lions / 4 Serendib Efreet / 2 Serra Angel / 4 Black Vise / Mox Pearl / Mox Ruby / Mox Sapphire / Sol Ring / Ancestral Recall / 4 Disenchant / 8 Lightning Bolt / 4 Psionic Blast / 3 Swords to Plowshares / Time Walk / Wheel of Fortune / Timetwister / Library of Alexandria / 4 Mishra's Factory / 1 Plains / 4 Plateau / Strip Mine / 4 Tundra / 4 Volcanic Island",
    cards: [
      { name: "Savannah Lions", count: 4 },
      { name: "Serendib Efreet", count: 4 },
      { name: "Serra Angel", count: 2 },
      { name: "Black Vise", count: 4 },
      { name: "Mox Pearl", count: 1 },
      { name: "Mox Ruby", count: 1 },
      { name: "Mox Sapphire", count: 1 },
      { name: "Sol Ring", count: 1 },
      { name: "Ancestral Recall", count: 1 },
      { name: "Disenchant", count: 4 },
      { name: "Lightning Bolt", count: 8 },
      { name: "Psionic Blast", count: 4 },
      { name: "Swords to Plowshares", count: 3 },
      { name: "Time Walk", count: 1 },
      { name: "Wheel of Fortune", count: 1 },
      { name: "Timetwister", count: 1 },
      { name: "Library of Alexandria", count: 1 },
      { name: "Mishra's Factory", count: 4 },
      { name: "Plains", count: 1 },
      { name: "Plateau", count: 4 },
      { name: "Strip Mine", count: 1 },
      { name: "Tundra", count: 4 },
      { name: "Volcanic Island", count: 4 },
    ],
  },
  {
    id: "robots",
    label: "Robots",
    name: "Robots",
    colors: ["blue", "red", "black"],
    deckList:
      "4 Su-Chi / 3 Sage of Lat-Nam / 3 Triskelion / 3 Counterspell / 4 Lightning Bolt / 2 Psionic Blast / Ancestral Recall / Mana Drain / Armageddon / Demonic Tutor / Fireball / Mind Twist / Recall / Time Walk / Wheel of Fortune / 4 Copy Artifact / Black Lotus / Fellwar Stone / Mox Emerald / Mox Jet / Mox Pearl / Mox Ruby / Mox Sapphire / Sol Ring / 4 Mishra's Factory / 4 Tundra / 3 City of Brass / 3 Volcanic Island / 2 Underground Sea / Badlands / Library of Alexandria / Strip Mine",
    cards: [
      { name: "Su-Chi", count: 4 },
      { name: "Sage of Lat-Nam", count: 3 },
      { name: "Triskelion", count: 3 },
      { name: "Counterspell", count: 3 },
      { name: "Lightning Bolt", count: 4 },
      { name: "Psionic Blast", count: 2 },
      { name: "Ancestral Recall", count: 1 },
      { name: "Mana Drain", count: 1 },
      { name: "Armageddon", count: 1 },
      { name: "Demonic Tutor", count: 1 },
      { name: "Fireball", count: 1 },
      { name: "Mind Twist", count: 1 },
      { name: "Recall", count: 1 },
      { name: "Time Walk", count: 1 },
      { name: "Wheel of Fortune", count: 1 },
      { name: "Copy Artifact", count: 4 },
      { name: "Black Lotus", count: 1 },
      { name: "Fellwar Stone", count: 1 },
      { name: "Mox Emerald", count: 1 },
      { name: "Mox Jet", count: 1 },
      { name: "Mox Pearl", count: 1 },
      { name: "Mox Ruby", count: 1 },
      { name: "Mox Sapphire", count: 1 },
      { name: "Sol Ring", count: 1 },
      { name: "Mishra's Factory", count: 4 },
      { name: "Tundra", count: 4 },
      { name: "City of Brass", count: 3 },
      { name: "Volcanic Island", count: 3 },
      { name: "Underground Sea", count: 2 },
      { name: "Badlands", count: 1 },
      { name: "Library of Alexandria", count: 1 },
      { name: "Strip Mine", count: 1 },
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
    id: "spz",
    label: "Self-Play Zero (SPZ)",
    name: "Self-Play Zero (SPZ)",
    description:
      "General self-taught bot: a value net trained purely from mirror self-play, played with greedy rollout lookahead. Beats Handcoded Policy 62.4% in paired benchmarks.",
    versionDate: "2026-08-01",
    versionDateLabel: "Champion frozen",
    lifecycle: "Self-play champion",
  },
]);

const DECK_IDS = new Set(DECKS.map(({ id }) => id));
const POLICY_IDS = new Set(POLICIES.map(({ id }) => id));
const DEFAULT_CONFIG = Object.freeze({
  players: [
    { deckId: "robots", policyId: "human" },
    { deckId: "uwr", policyId: "spz" },
  ],
  debugReveal: true,
  bluffMode: false,
  rollouts: 2,
  deepRollouts: 8,
});

const LIMITS = Object.freeze({
  rollouts: { min: 1, max: 4_096 },
  deepRollouts: { min: 1, max: 4_096 },
});

export const EVOLUTION_PILOTS = Object.freeze([
  {
    id: "handcrafted",
    label: "Handcoded Policy",
    name: "Handcoded Policy",
    description: "Fast rules-aware evaluation against the five-deck metagame.",
  },
  {
    id: "spz",
    label: "Self-Play Zero (SPZ)",
    name: "Self-Play Zero (SPZ)",
    description:
      "The self-taught champion evaluates candidates with its fast myopic policy.",
  },
  {
    id: "random",
    label: "Random",
    name: "Random",
    description: "Uniform legal actions; a noisy but unbiased fitness signal.",
  },
  {
    id: "monte-carlo",
    label: "Monte Carlo",
    name: "Monte Carlo",
    description: "Short random-continuation sampling for each choice.",
  },
  {
    id: "deep-monte-carlo",
    label: "Deep Monte Carlo",
    name: "Deep Monte Carlo",
    description: "A larger rollout budget per choice; slower evaluation.",
  },
]);

export const EVOLUTION_DEFAULTS = Object.freeze({
  generations: 3,
  population: 9,
  games: 1,
  pilot: "handcrafted",
  seed: "424242",
});

export const EVOLUTION_LIMITS = Object.freeze({
  seed: { min: 0, max: MAX_WEB_EVOLUTION_SEED },
  generations: { min: 1, max: 200 },
  population: { min: 9, max: 32 },
  games: { min: 1, max: 16 },
  retainedResults: MAX_EVOLUTION_RESULTS,
  savedDecks: MAX_SAVED_DECKS,
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
  if (value.treatment !== undefined) {
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

function reportScalar(value) {
  if (typeof value === "string" || typeof value === "boolean") {
    return value;
  }
  if (typeof value === "number" && Number.isFinite(value)) {
    return value;
  }
  return undefined;
}

function reportId(value) {
  if (typeof value === "string") return value;
  if (typeof value === "number" && Number.isFinite(value)) return value;
  return undefined;
}

function reportCard(value) {
  if (typeof value === "string") return { name: value };
  if (!isRecord(value)) return null;
  const card = {};
  for (const field of [
    "id",
    "name",
    "type",
    "costLabel",
    "power",
    "toughness",
    "flying",
  ]) {
    const scalar = reportScalar(value[field]);
    if (scalar !== undefined) card[field] = scalar;
  }
  if (isRecord(value.cost)) {
    const cost = {};
    for (const field of [
      "generic",
      "green",
      "red",
      "blue",
      "white",
      "colorless",
    ]) {
      const scalar = reportScalar(value.cost[field]);
      if (scalar !== undefined) cost[field] = scalar;
    }
    if (Object.keys(cost).length > 0) card.cost = cost;
  } else {
    const cost = reportScalar(value.cost);
    if (cost !== undefined) card.cost = cost;
  }
  return Object.keys(card).length > 0 ? card : null;
}

function reportTarget(value) {
  const scalar = reportId(value);
  if (scalar !== undefined) return scalar;
  if (!isRecord(value)) return null;
  const target = {};
  for (const field of ["player", "creature", "label"]) {
    const entry = reportScalar(value[field]);
    if (entry !== undefined) target[field] = entry;
  }
  return Object.keys(target).length > 0 ? target : null;
}

function reportPermanent(value, zone) {
  if (typeof value === "string") return { zone, label: value };
  if (!isRecord(value)) return null;
  const permanent = { zone };
  const permanentId = reportId(value.permanentId);
  if (permanentId !== undefined) permanent.permanentId = permanentId;
  const card = reportCard(value.card);
  if (card !== null) permanent.card = card;
  for (const field of [
    "tapped",
    "summoningSick",
    "damage",
    "power",
    "toughness",
  ]) {
    const scalar = reportScalar(value[field]);
    if (scalar !== undefined) permanent[field] = scalar;
  }
  return permanent;
}

function reportBattlefield(player) {
  if (!isRecord(player)) return [];
  const battlefield = [];
  for (const [field, zone] of [
    ["lands", "land"],
    ["creatures", "creature"],
    ["artifacts", "artifact"],
    ["enchantments", "enchantment"],
    ["battlefield", "permanent"],
  ]) {
    if (!Array.isArray(player[field])) continue;
    for (const value of player[field]) {
      const permanent = reportPermanent(value, zone);
      if (permanent !== null) battlefield.push(permanent);
    }
  }
  return battlefield;
}

function reportManaPool(value) {
  const scalar = reportScalar(value);
  if (scalar !== undefined) return scalar;
  if (!isRecord(value)) return undefined;
  const mana = {};
  for (const field of [
    "generic",
    "green",
    "red",
    "blue",
    "white",
    "colorless",
  ]) {
    const entry = reportScalar(value[field]);
    if (entry !== undefined) mana[field] = entry;
  }
  return Object.keys(mana).length > 0 ? mana : undefined;
}

function reportPlayer(player, seat) {
  const source = isRecord(player) ? player : {};
  const handCount = Number.isSafeInteger(source.handSize)
    ? source.handSize
    : Array.isArray(source.hand)
      ? source.hand.length
      : Array.isArray(source.revealedHand)
        ? source.revealedHand.length
        : null;
  const libraryCount = Number.isSafeInteger(source.librarySize)
    ? source.librarySize
    : Array.isArray(source.library)
      ? source.library.length
      : null;
  const result = {
    seat,
    role: seat === 0 ? "human" : "opponent",
    life: Number.isFinite(source.life) ? source.life : null,
    handCount,
    libraryCount,
    graveyardCount: Array.isArray(source.graveyard)
      ? source.graveyard.length
      : 0,
    exileCount: Array.isArray(source.exile) ? source.exile.length : 0,
    battlefield: reportBattlefield(source),
  };
  const manaPool = reportManaPool(source.manaPool);
  if (manaPool !== undefined) result.manaPool = manaPool;
  if (typeof source.landPlayedThisTurn === "boolean") {
    result.landPlayedThisTurn = source.landPlayedThisTurn;
  }
  if (Number.isSafeInteger(source.extraTurns)) {
    result.extraTurns = source.extraTurns;
  }
  return result;
}

function reportStackEntry(value) {
  if (!isRecord(value)) return null;
  const entry = {};
  for (const field of ["stackId", "id", "kind", "label", "controller", "xValue"]) {
    const scalar = reportScalar(value[field]);
    if (scalar !== undefined) entry[field] = scalar;
  }
  const card = reportCard(value.card);
  if (card !== null) entry.card = card;
  const target = reportTarget(value.target);
  if (target !== null) entry.target = target;
  if (Array.isArray(value.targets)) {
    entry.targets = value.targets
      .map(reportTarget)
      .filter((candidate) => candidate !== null);
  }
  const spellTarget = reportId(value.spellTarget);
  if (spellTarget !== undefined) entry.spellTarget = spellTarget;
  return entry;
}

function reportPriorityOption(value) {
  if (!isRecord(value)) return null;
  const option = {};
  for (const field of [
    "index",
    "label",
    "kind",
    "sourcePermanent",
    "spellTarget",
    "xValue",
  ]) {
    const scalar = reportScalar(value[field]);
    if (scalar !== undefined) option[field] = scalar;
  }
  const card = reportCard(value.card);
  if (card !== null) option.card = card;
  const target = reportTarget(value.target);
  if (target !== null) option.target = target;
  return option;
}

function reportDecision(value) {
  if (!isRecord(value) || typeof value.kind !== "string") return null;
  const decision = {
    decisionId: reportId(value.id ?? value.decisionId) ?? null,
    kind: value.kind,
  };
  if (typeof value.phase === "string") decision.phase = value.phase;
  if (value.kind === "priority") {
    decision.options = Array.isArray(value.options)
      ? value.options
          .map(reportPriorityOption)
          .filter((option) => option !== null)
      : [];
  } else if (value.kind === "attackers") {
    decision.eligible = Array.isArray(value.eligible)
      ? value.eligible
          .map(reportId)
          .filter((id) => id !== undefined)
      : [];
  } else if (value.kind === "blockers") {
    decision.attackers = Array.isArray(value.attackers)
      ? value.attackers
          .map(reportId)
          .filter((id) => id !== undefined)
      : [];
    decision.choices = Array.isArray(value.choices)
      ? value.choices.flatMap((choice) => {
          if (!isRecord(choice)) return [];
          const blocker = reportId(choice.blocker);
          if (blocker === undefined) return [];
          return [{
            blocker,
            legalAttackers: Array.isArray(choice.legalAttackers)
              ? choice.legalAttackers
                  .map(reportId)
                  .filter((id) => id !== undefined)
              : [],
          }];
        })
      : [];
  } else if (value.kind === "damage_order") {
    decision.attacker = reportId(value.attacker) ?? null;
    decision.blockers = Array.isArray(value.blockers)
      ? value.blockers
          .map(reportId)
          .filter((id) => id !== undefined)
      : [];
  } else if (value.kind === "mulligan") {
    decision.handSize = Number.isSafeInteger(value.handSize)
      ? value.handSize
      : 0;
    decision.options = Array.isArray(value.options)
      ? value.options.flatMap((option) => {
          if (!isRecord(option) || !Number.isSafeInteger(option.index)) {
            return [];
          }
          return [{
            index: option.index,
            label:
              typeof option.label === "string"
                ? option.label
                : option.index === 0
                  ? "Keep this hand"
                  : "Mulligan",
          }];
        })
      : [];
  } else if (value.kind === "cleanup_discard") {
    decision.count = Number.isSafeInteger(value.count) ? value.count : 0;
    decision.options = Array.isArray(value.options)
      ? value.options.flatMap((option) => {
          if (!isRecord(option) || !Number.isSafeInteger(option.index)) {
            return [];
          }
          const card = reportCard(option.card);
          return [{
            index: option.index,
            ...(card === null ? {} : { card }),
          }];
        })
      : [];
  }
  return decision;
}

function explicitReportEventMessage(value) {
  if (typeof value === "string") return value;
  if (!isRecord(value)) return "Game event";
  for (const field of ["message", "text", "label"]) {
    if (typeof value[field] === "string") return value[field];
  }
  return "Game event";
}

function reportChronicleEntry(value, index) {
  const entry = {
    sequence: index + 1,
    message: explicitReportEventMessage(value),
  };
  if (!isRecord(value)) return entry;
  for (const field of ["turn", "player", "kind", "actionKind", "phase"]) {
    const scalar = reportScalar(value[field]);
    if (scalar !== undefined) entry[field] = scalar;
  }
  return entry;
}

function reportConfig(config) {
  return {
    players: config.players.map(({ deckId, policyId }) => ({
      deckId,
      policyId,
    })),
    seed: config.seed,
    debugReveal: config.debugReveal,
    bluffMode: config.bluffMode,
    rollouts: config.rollouts,
    deepRollouts: config.deepRollouts,
  };
}

function reportModel(model) {
  if (model === null) return null;
  return {
    family: model.family,
    generation: model.generation,
    searchWorlds: model.searchWorlds,
    horizonTurns: model.horizonTurns,
    source: model.source,
    fingerprint: model.fingerprint,
  };
}

function reportAction(action) {
  const submission = {
    decisionId: reportId(action.decisionId) ?? null,
  };
  if (Number.isSafeInteger(action.index)) submission.index = action.index;
  if (Array.isArray(action.ids)) {
    submission.ids = action.ids
      .map(reportId)
      .filter((id) => id !== undefined);
  }
  if (Array.isArray(action.pairs)) {
    submission.pairs = action.pairs.flatMap((pair) => {
      if (!Array.isArray(pair) || pair.length !== 2) return [];
      const attacker = reportId(pair[0]);
      const blocker = reportId(pair[1]);
      return attacker === undefined || blocker === undefined
        ? []
        : [[attacker, blocker]];
    });
  }
  if (Array.isArray(action.indices)) {
    submission.indices = action.indices.filter(Number.isSafeInteger);
  }
  return submission;
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

function validatedDeck(
  value,
  fieldName,
  fallback,
  validDeckIds = DECK_IDS,
) {
  const candidate = value === undefined ? fallback : value;
  if (typeof candidate !== "string" || !validDeckIds.has(candidate)) {
    throw new ApiError(
      400,
      "invalid_config",
      `${fieldName} must identify an available server deck`,
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

export function normalizeGameConfig(body, validDeckIds = DECK_IDS) {
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

  const debugReveal = body.debugReveal ?? body.debug ?? true;
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
  return {
    players: [
      {
        deckId: validatedDeck(
          humanDeck,
          "players[0].deckId",
          DEFAULT_CONFIG.players[0].deckId,
          validDeckIds,
        ),
        policyId: "human",
      },
      {
        deckId: validatedDeck(
          opponentDeck,
          "players[1].deckId",
          DEFAULT_CONFIG.players[1].deckId,
          validDeckIds,
        ),
        policyId: normalizedOpponentPolicy,
      },
    ],
    seed: normalizedUint64(body.seed, "seed", freshSeed()),
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
  };
}

export function normalizeEvolutionConfig(body) {
  if (!isRecord(body)) {
    throw new ApiError(
      400,
      "invalid_evolution_config",
      "The evolution body must be a JSON object",
    );
  }
  const pilot =
    body.pilot === undefined ? EVOLUTION_DEFAULTS.pilot : body.pilot;
  const validPilots = new Set(EVOLUTION_PILOTS.map(({ id }) => id));
  if (typeof pilot !== "string" || !validPilots.has(pilot)) {
    throw new ApiError(
      400,
      "invalid_evolution_config",
      "pilot must be one of " + [...validPilots].join(", "),
    );
  }
  try {
    const seed = normalizedUint64(
      body.seed,
      "seed",
      EVOLUTION_DEFAULTS.seed,
    );
    if (BigInt(seed) > BigInt(MAX_WEB_EVOLUTION_SEED)) {
      throw new ApiError(
        400,
        "invalid_config",
        `seed must be an integer from 0 to ${MAX_WEB_EVOLUTION_SEED}`,
      );
    }
    return {
      generations: positiveBoundedInteger(
        body.generations,
        "generations",
        EVOLUTION_LIMITS.generations,
        EVOLUTION_DEFAULTS.generations,
      ),
      population: positiveBoundedInteger(
        body.population,
        "population",
        EVOLUTION_LIMITS.population,
        EVOLUTION_DEFAULTS.population,
      ),
      games: positiveBoundedInteger(
        body.games,
        "games",
        EVOLUTION_LIMITS.games,
        EVOLUTION_DEFAULTS.games,
      ),
      pilot,
      seed,
    };
  } catch (error) {
    if (error instanceof ApiError && error.code === "invalid_config") {
      throw new ApiError(
        error.status,
        "invalid_evolution_config",
        error.message,
      );
    }
    throw error;
  }
}

function validatedPercentage(value, fieldName) {
  if (
    typeof value !== "number" ||
    !Number.isFinite(value) ||
    value < 0 ||
    value > 100
  ) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      `${fieldName} must be a finite percentage from 0 to 100`,
    );
  }
  return value;
}

function validatedEvolutionStats(value, fieldName, expectedGames) {
  if (!isRecord(value)) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      `${fieldName} must be a result object`,
    );
  }
  const integers = ["games", "wins", "losses", "draws"];
  for (const field of integers) {
    if (!Number.isSafeInteger(value[field]) || value[field] < 0) {
      throw new ApiError(
        502,
        "evolution_protocol_error",
        `${fieldName}.${field} must be a non-negative integer`,
      );
    }
  }
  if (
    value.games !== expectedGames ||
    value.wins + value.losses + value.draws !== value.games
  ) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      `${fieldName} has inconsistent game totals`,
    );
  }
  const winRate = validatedPercentage(
    value.winRate,
    `${fieldName}.winRate`,
  );
  const expectedWinRate = (100 * value.wins) / value.games;
  if (Math.abs(winRate - expectedWinRate) > 1e-6) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      `${fieldName}.winRate does not match its outcomes`,
    );
  }
  return {
    games: value.games,
    wins: value.wins,
    losses: value.losses,
    draws: value.draws,
    winRate,
  };
}

export function validateEvolutionResult(value, config) {
  if (
    !isRecord(value) ||
    value.type !== "evolution_result" ||
    value.schemaVersion !== 1 ||
    !isRecord(value.parameters) ||
    !isRecord(value.deck)
  ) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      "The evolution engine returned an invalid result",
    );
  }
  const seed = normalizedUint64(value.seed, "seed", undefined);
  if (seed !== config.seed || value.pilot !== config.pilot) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      "The evolution result does not match the requested seed and pilot",
    );
  }
  if (
    value.parameters.generations !== config.generations ||
    value.parameters.population !== config.population ||
    value.parameters.gamesPerOpponent !== config.games
  ) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      "The evolution result parameters do not match the request",
    );
  }
  if (
    !Array.isArray(value.generationBestWinRates) ||
    value.generationBestWinRates.length !== config.generations
  ) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      "The evolution result has an invalid generation trace",
    );
  }
  const generations = value.generationBestWinRates.map((rate, index) =>
    validatedPercentage(rate, `generationBestWinRates[${index}]`),
  );

  if (
    value.deck.size !== 40 ||
    !Array.isArray(value.deck.cardIds) ||
    value.deck.cardIds.length !== 40 ||
    !value.deck.cardIds.every(
      (id) =>
        Number.isSafeInteger(id) && id >= 0 && id < CARD_NAMES.length,
    ) ||
    !Array.isArray(value.deck.cards) ||
    value.deck.cards.length === 0
  ) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      "The evolution result has an invalid 40-card deck",
    );
  }
  const seenCards = new Set();
  let cardTotal = 0;
  const cards = value.deck.cards.map((card, index) => {
    if (
      !isRecord(card) ||
      !Number.isSafeInteger(card.id) ||
      card.id < 0 ||
      card.id >= CARD_NAMES.length ||
      typeof card.name !== "string" ||
      card.name !== CARD_NAMES[card.id] ||
      !Number.isSafeInteger(card.count) ||
      card.count < 1 ||
      seenCards.has(card.id)
    ) {
      throw new ApiError(
        502,
        "evolution_protocol_error",
        `deck.cards[${index}] is not a valid unique card row`,
      );
    }
    seenCards.add(card.id);
    cardTotal += card.count;
    return { id: card.id, name: card.name, count: card.count };
  });
  if (cardTotal !== 40) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      "The evolved deck must contain exactly 40 cards",
    );
  }
  const compactCounts = new Array(CARD_NAMES.length).fill(0);
  for (const { id, count } of cards) compactCounts[id] = count;
  const vectorCounts = new Array(CARD_NAMES.length).fill(0);
  for (const id of value.deck.cardIds) ++vectorCounts[id];
  if (
    compactCounts.some((count, index) => count !== vectorCounts[index])
  ) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      "The compact card manifest does not match the exact card vector",
    );
  }

  const gamesPerOpponent = config.games * 4;
  const stats = validatedEvolutionStats(
    value.fitness,
    "fitness",
    gamesPerOpponent * DECKS.length,
  );
  if (
    !Array.isArray(value.byOpponent) ||
    value.byOpponent.length !== DECKS.length
  ) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      "The evolution result must report every metagame opponent",
    );
  }
  const seenOpponents = new Set();
  const byOpponent = value.byOpponent.map((entry, index) => {
    if (
      !isRecord(entry) ||
      typeof entry.deck !== "string" ||
      !isRecord(entry.fitness) ||
      seenOpponents.has(entry.deck)
    ) {
      throw new ApiError(
        502,
        "evolution_protocol_error",
        `byOpponent[${index}] is invalid`,
      );
    }
    const expectedDeck = DECKS.find(({ id }) => id === entry.deck);
    if (expectedDeck === undefined || entry.name !== expectedDeck.name) {
      throw new ApiError(
        502,
        "evolution_protocol_error",
        `byOpponent[${index}] does not identify a metagame deck`,
      );
    }
    seenOpponents.add(entry.deck);
    return {
      deckId: entry.deck,
      name: entry.name,
      ...validatedEvolutionStats(
        entry.fitness,
        `byOpponent[${index}].fitness`,
        gamesPerOpponent,
      ),
    };
  });
  const opponentTotals = byOpponent.reduce(
    (totals, row) => ({
      games: totals.games + row.games,
      wins: totals.wins + row.wins,
      losses: totals.losses + row.losses,
      draws: totals.draws + row.draws,
    }),
    { games: 0, wins: 0, losses: 0, draws: 0 },
  );
  if (
    opponentTotals.games !== stats.games ||
    opponentTotals.wins !== stats.wins ||
    opponentTotals.losses !== stats.losses ||
    opponentTotals.draws !== stats.draws
  ) {
    throw new ApiError(
      502,
      "evolution_protocol_error",
      "Evolution aggregate fitness does not equal its opponent rows",
    );
  }

  const validatedCardRows = (rows, fieldName) => {
    if (!Array.isArray(rows) || rows.length === 0) {
      throw new ApiError(
        502,
        "evolution_protocol_error",
        `${fieldName} is not a valid card list`,
      );
    }
    const seen = new Set();
    let total = 0;
    const validated = rows.map((card, index) => {
      if (
        !isRecord(card) ||
        !Number.isSafeInteger(card.id) ||
        card.id < 0 ||
        card.id >= CARD_NAMES.length ||
        typeof card.name !== "string" ||
        card.name !== CARD_NAMES[card.id] ||
        !Number.isSafeInteger(card.count) ||
        card.count < 1 ||
        seen.has(card.id)
      ) {
        throw new ApiError(
          502,
          "evolution_protocol_error",
          `${fieldName}[${index}] is not a valid unique card row`,
        );
      }
      seen.add(card.id);
      total += card.count;
      return { id: card.id, name: card.name, count: card.count };
    });
    if (total !== 40) {
      throw new ApiError(
        502,
        "evolution_protocol_error",
        `${fieldName} must contain exactly 40 cards`,
      );
    }
    return validated;
  };
  let top = [];
  if (value.top !== undefined) {
    if (!Array.isArray(value.top) || value.top.length > 5) {
      throw new ApiError(
        502,
        "evolution_protocol_error",
        "top must list at most five decks",
      );
    }
    top = value.top.map((entry, index) => {
      if (!isRecord(entry)) {
        throw new ApiError(
          502,
          "evolution_protocol_error",
          `top[${index}] is invalid`,
        );
      }
      return {
        cards: validatedCardRows(entry.cards, `top[${index}].cards`),
        stats: validatedEvolutionStats(
          entry.fitness,
          `top[${index}].fitness`,
          gamesPerOpponent * DECKS.length,
        ),
      };
    });
  }

  return {
    result: {
      seed,
      pilot: value.pilot,
      generations,
      population: config.population,
      games: config.games,
      best: { cards, stats, byOpponent },
      top,
    },
    cardIds: [...value.deck.cardIds],
  };
}

function validatedSavedDeckName(body) {
  if (
    !isRecord(body) ||
    Object.keys(body).some((key) => key !== "name") ||
    typeof body.name !== "string"
  ) {
    throw new ApiError(
      400,
      "invalid_deck_name",
      "The save body must contain only a deck name",
    );
  }
  const name = body.name.trim();
  if (
    name.length < 1 ||
    name.length > 48 ||
    /[\u0000-\u001f\u007f]/.test(name)
  ) {
    throw new ApiError(
      400,
      "invalid_deck_name",
      "Deck names must contain 1 to 48 visible characters",
    );
  }
  return name;
}

class EphemeralDeckCatalog {
  constructor(options = {}) {
    this.evolutions = new Map();
    this.decks = new Map();
    this.savedByEvolution = new Map();
    this.evolutionIdFactory =
      options.evolutionIdFactory ?? (() => randomUUID());
    this.deckIdFactory = options.deckIdFactory ?? (() => randomUUID());
  }

  #uniqueId(factory, occupied, kind) {
    for (let attempt = 0; attempt < 16; ++attempt) {
      const id = factory();
      if (
        typeof id === "string" &&
        id.length > 0 &&
        !id.includes("/") &&
        !occupied.has(id) &&
        !DECK_IDS.has(id)
      ) {
        return id;
      }
    }
    throw new ApiError(
      500,
      "id_generation_failed",
      `Could not allocate an opaque ${kind} ID`,
    );
  }

  record(config, validated) {
    const id = this.#uniqueId(
      this.evolutionIdFactory,
      this.evolutions,
      "evolution",
    );
    const publicEvolution = {
      id,
      ...structuredClone(validated.result),
    };
    this.evolutions.set(id, {
      publicEvolution,
      config: structuredClone(config),
      cardIds: [...validated.cardIds],
    });
    while (this.evolutions.size > MAX_EVOLUTION_RESULTS) {
      const oldest = this.evolutions.keys().next().value;
      this.evolutions.delete(oldest);
    }
    return structuredClone(publicEvolution);
  }

  save(evolutionId, body) {
    const name = validatedSavedDeckName(body);
    const stored = this.evolutions.get(evolutionId);
    if (stored === undefined) {
      throw new ApiError(
        404,
        "evolution_not_found",
        "No such completed evolution result",
      );
    }
    const evolution = stored.publicEvolution;
    if (this.savedByEvolution.has(evolutionId)) {
      throw new ApiError(
        409,
        "evolution_already_saved",
        "That evolution result is already saved",
      );
    }
    if (this.decks.size >= MAX_SAVED_DECKS) {
      throw new ApiError(
        409,
        "saved_deck_limit",
        "This server session has reached its saved-deck limit",
      );
    }
    const id = this.#uniqueId(this.deckIdFactory, this.decks, "deck");
    const colors = [
      evolution.best.cards.some(({ id: card }) => card === 0)
        ? "green"
        : null,
      evolution.best.cards.some(({ id: card }) => card === 1) ? "red" : null,
      evolution.best.cards.some(({ id: card }) => card === 6)
        ? "blue"
        : null,
      evolution.best.cards.some(({ id: card }) => card === 10)
        ? "white"
        : null,
    ].filter((color) => color !== null);
    const cards = structuredClone(evolution.best.cards);
    const metadata = {
      id,
      label: name,
      name,
      colors,
      deckList: cards
        .map(({ name: cardName, count }) => `${count} ${cardName}`)
        .join(" / "),
      cards,
      ephemeral: true,
      evolution: {
        id: evolution.id,
        seed: evolution.seed,
        pilot: evolution.pilot,
        generations: stored.config.generations,
        population: evolution.population,
        games: evolution.games,
        winRate: evolution.best.stats.winRate,
      },
    };
    this.decks.set(id, {
      metadata,
      cardIds: [...stored.cardIds],
    });
    this.savedByEvolution.set(evolutionId, id);
    return structuredClone(metadata);
  }

  publicDecks() {
    return [...this.decks.values()].map(({ metadata }) =>
      structuredClone(metadata),
    );
  }

  availableDeckIds() {
    return new Set([...DECK_IDS, ...this.decks.keys()]);
  }

  customCardIds(deckId) {
    return this.decks.get(deckId)?.cardIds ?? null;
  }
}

export function evolutionArguments(config) {
  return [
    "--evolve-json",
    "--generations",
    String(config.generations),
    "--population",
    String(config.population),
    "--games",
    String(config.games),
    "--evolve-pilot",
    config.pilot,
    "--seed",
    config.seed,
  ];
}

class EvolutionManager {
  constructor(options) {
    this.bridgePath = options.bridgePath;
    this.bridgeArgsPrefix = options.bridgeArgsPrefix;
    this.spawnImpl = options.spawnImpl;
    this.timeoutMs = options.timeoutMs;
    this.terminationGraceMs = options.terminationGraceMs;
    this.maxOutputBytes = options.maxOutputBytes;
    this.deckCatalog = options.deckCatalog;
    this.active = null;
  }

  async create(config) {
    if (this.active !== null) {
      throw new ApiError(
        409,
        "evolution_busy",
        "Another deck evolution is already running",
      );
    }

    let child;
    try {
      child = this.spawnImpl(
        this.bridgePath,
        [...this.bridgeArgsPrefix, ...evolutionArguments(config)],
        {
          stdio: ["ignore", "pipe", "pipe"],
          windowsHide: true,
          shell: false,
        },
      );
    } catch (error) {
      throw new ApiError(
        502,
        "evolution_start_failed",
        `Could not start deck evolution: ${error.message}`,
      );
    }

    this.active = { child };
    try {
      const raw = await this.#readResult(child);
      const validated = validateEvolutionResult(raw, config);
      return this.deckCatalog.record(config, validated);
    } finally {
      if (this.active?.child === child) this.active = null;
    }
  }

  #readResult(child) {
    return new Promise((resolve, reject) => {
      if (
        child?.stdout === undefined ||
        child?.stderr === undefined ||
        typeof child.once !== "function"
      ) {
        reject(
          new ApiError(
            502,
            "evolution_start_failed",
            "Deck evolution did not start with bounded output streams",
          ),
        );
        return;
      }

      let stdout = "";
      let stderr = "";
      let outputBytes = 0;
      let settled = false;
      let pendingTerminationError = null;
      let killTimer = null;
      const timer = setTimeout(() => {
        terminate(
          new ApiError(
            504,
            "evolution_timeout",
            "Deck evolution exceeded its runtime limit",
          ),
        );
      }, this.timeoutMs);

      const finish = (error) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        if (killTimer !== null) clearTimeout(killTimer);
        if (error !== null) {
          reject(error);
          return;
        }
        const text = stdout.trim();
        let parsed;
        try {
          parsed = JSON.parse(text);
        } catch {
          reject(
            new ApiError(
              502,
              "evolution_protocol_error",
              "The evolution engine did not return one valid JSON object",
            ),
          );
          return;
        }
        if (!isRecord(parsed)) {
          reject(
            new ApiError(
              502,
              "evolution_protocol_error",
              "The evolution engine returned a non-object result",
            ),
          );
          return;
        }
        resolve(parsed);
      };

      const terminate = (error) => {
        if (settled || pendingTerminationError !== null) return;
        pendingTerminationError = error;
        clearTimeout(timer);
        if (typeof child.kill === "function") child.kill("SIGTERM");
        killTimer = setTimeout(() => {
          if (!settled && typeof child.kill === "function") {
            child.kill("SIGKILL");
          }
        }, this.terminationGraceMs);
      };

      const receive = (channel, chunk) => {
        if (settled || pendingTerminationError !== null) return;
        const text = String(chunk);
        outputBytes += Buffer.byteLength(text);
        if (outputBytes > this.maxOutputBytes) {
          terminate(
            new ApiError(
              502,
              "evolution_output_too_large",
              "Deck evolution exceeded its output limit",
            ),
          );
          return;
        }
        if (channel === "stdout") stdout += text;
        else stderr += text;
      };

      child.stdout.setEncoding?.("utf8");
      child.stderr.setEncoding?.("utf8");
      child.stdout.on("data", (chunk) => receive("stdout", chunk));
      child.stderr.on("data", (chunk) => receive("stderr", chunk));
      child.once("error", (error) => {
        finish(
          new ApiError(
            502,
            "evolution_start_failed",
            `Could not start deck evolution: ${error.message}`,
          ),
        );
      });
      child.once("close", (code, signal) => {
        if (pendingTerminationError !== null) {
          finish(pendingTerminationError);
          return;
        }
        if (code !== 0) {
          const reason =
            signal === null
              ? `code ${code ?? "unknown"}`
              : `signal ${signal}`;
          finish(
            new ApiError(
              502,
              "evolution_failed",
              `The deck evolution engine exited with ${reason}`,
              stderr.trim()
                ? { stderr: stderr.trim().slice(-2_048) }
                : undefined,
            ),
          );
          return;
        }
        finish(null);
      });
    });
  }

  isActive() {
    return this.active !== null;
  }

  shutdown() {
    if (
      this.active?.child !== undefined &&
      typeof this.active.child.kill === "function"
    ) {
      this.active.child.kill("SIGTERM");
    }
  }
}

function bridgeArguments(config, deckCatalog) {
  const humanCustomCards =
    deckCatalog?.customCardIds(config.players[0].deckId) ?? null;
  const opponentCustomCards =
    deckCatalog?.customCardIds(config.players[1].deckId) ?? null;
  const args = [
    "--human-deck",
    humanCustomCards === null ? config.players[0].deckId : "ru-aggro",
    "--opponent-deck",
    opponentCustomCards === null
      ? config.players[1].deckId
      : "ru-aggro",
    "--opponent-policy",
    config.players[1].policyId,
    "--seed",
    config.seed,
    "--rollouts",
    String(config.rollouts),
    "--deep-rollouts",
    String(config.deepRollouts),
  ];
  if (humanCustomCards !== null) {
    args.push("--human-deck-cards", humanCustomCards.join(","));
  }
  if (opponentCustomCards !== null) {
    args.push(
      "--opponent-deck-cards",
      opponentCustomCards.join(","),
    );
  }
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

  if (decision.kind === "mulligan") {
    if (!Number.isSafeInteger(input.index)) {
      throw new ApiError(
        422,
        "illegal_action",
        "A mulligan choice needs an integer option index",
      );
    }
    const options = Array.isArray(decision.options) ? decision.options : [];
    if (!options.some((option) => option?.index === input.index)) {
      throw new ApiError(
        422,
        "illegal_action",
        "That mulligan option is not legal",
      );
    }
    return { decisionId: decision.id, index: input.index };
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
    deckCatalog,
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
    this.successfulHumanActions = [];
    this.latestEnvelope = null;
    this.stderr = "";
    this.closed = false;
    this.settlementVersion = 0;
    this.waiters = new Set();
    this.actionTimeoutMs = actionTimeoutMs;
    this.actionQueue = Promise.resolve();

    const args = [
      ...bridgeArgsPrefix,
      ...bridgeArguments(config, deckCatalog),
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
      if (isRecord(rawEvent)) {
        // Track declared-but-unresolved combat for client visuals.
        if (rawEvent.kind === "attackers_declared") {
          this.pendingCombat =
            Array.isArray(rawEvent.attackers) &&
            rawEvent.attackers.length > 0
              ? { attackers: rawEvent.attackers, blocks: [] }
              : null;
        } else if (
          rawEvent.kind === "blockers_declared" ||
          rawEvent.kind === "damage_order"
        ) {
          if (this.pendingCombat && Array.isArray(rawEvent.blocks)) {
            this.pendingCombat = {
              attackers: this.pendingCombat.attackers,
              blocks: rawEvent.blocks,
            };
          }
        } else if (
          rawEvent.kind === "combat_resolved" ||
          rawEvent.kind === "turn_started"
        ) {
          this.pendingCombat = null;
        }
      }
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
      pendingCombat: this.pendingCombat ?? null,
      decision: this.decision,
      events: this.events,
      log: this.log,
      result: this.result,
      model: this.model,
      error: this.error,
    };
  }

  bugReport() {
    const state = isRecord(this.snapshot) ? this.snapshot : {};
    const players = Array.isArray(state.players) ? state.players : [];
    const turnNumber = Number.isSafeInteger(state.turnNumber)
      ? state.turnNumber
      : Number.isSafeInteger(state.turn)
        ? state.turn
        : null;
    const phase =
      typeof state.phase === "string"
        ? state.phase
        : typeof this.decision?.phase === "string"
          ? this.decision.phase
          : null;
    return {
      schema: BUG_REPORT_SCHEMA,
      version: BUG_REPORT_VERSION,
      match: {
        id: this.id,
        status: this.status,
        config: reportConfig(this.config),
        model: reportModel(this.model),
      },
      successfulHumanActions: this.successfulHumanActions,
      publicState: {
        turnNumber,
        phase,
        activePlayer: Number.isSafeInteger(state.activePlayer)
          ? state.activePlayer
          : null,
        startingPlayer: Number.isSafeInteger(state.startingPlayer)
          ? state.startingPlayer
          : null,
        priority: {
          holder:
            this.decision?.kind === "priority" ? "human" : "none",
          seat: this.decision?.kind === "priority" ? 0 : null,
        },
        players: [
          reportPlayer(players[0], 0),
          reportPlayer(players[1], 1),
        ],
        stack: Array.isArray(state.stack)
          ? state.stack
              .map(reportStackEntry)
              .filter((entry) => entry !== null)
          : [],
      },
      publicChronicle: this.events.map(reportChronicleEntry),
      currentDecision: reportDecision(this.decision),
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

    const submittedDecision = this.decision;
    const action = validateActionForDecision(input, submittedDecision);
    const submittedState = isRecord(this.snapshot) ? this.snapshot : {};
    const actionContext = {
      turnNumber: Number.isSafeInteger(submittedState.turnNumber)
        ? submittedState.turnNumber
        : Number.isSafeInteger(submittedState.turn)
          ? submittedState.turn
          : null,
      phase:
        typeof submittedState.phase === "string"
          ? submittedState.phase
          : typeof submittedDecision.phase === "string"
            ? submittedDecision.phase
            : null,
      decision: {
        decisionId: reportId(submittedDecision.id) ?? null,
        kind: submittedDecision.kind,
      },
      submission: reportAction(action),
    };
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
      if (this.status !== "failed") {
        this.successfulHumanActions.push({
          sequence: this.successfulHumanActions.length + 1,
          ...actionContext,
        });
      }
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
    this.deckCatalog = options.deckCatalog;
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
      deckCatalog: this.deckCatalog,
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

export function webGameMetadata(deckCatalog = null, evolutionManager = null) {
  return {
    apiVersion: 1,
    decks: [...DECKS, ...(deckCatalog?.publicDecks() ?? [])],
    policies: POLICIES,
    decisionKinds: [
      "priority",
      "attackers",
      "blockers",
      "damage_order",
      "cleanup_discard",
      "mulligan",
    ],
    defaults: DEFAULT_CONFIG,
    limits: LIMITS,
    evolution: {
      pilots: EVOLUTION_PILOTS,
      defaults: EVOLUTION_DEFAULTS,
      limits: EVOLUTION_LIMITS,
      active: evolutionManager?.isActive() ?? false,
      storage: "server-memory",
      notice: "Saved decks last until this server restarts.",
    },
  };
}

async function handleApi(
  request,
  response,
  pathname,
  manager,
  evolutionManager,
  deckCatalog,
) {
  if (pathname === "/api/meta") {
    if (request.method !== "GET") {
      methodNotAllowed(response, ["GET"]);
      return;
    }
    sendJson(
      response,
      200,
      webGameMetadata(deckCatalog, evolutionManager),
    );
    return;
  }

  if (pathname === "/api/evolutions") {
    if (request.method !== "POST") {
      methodNotAllowed(response, ["POST"]);
      return;
    }
    const config = normalizeEvolutionConfig(await readJson(request));
    const evolution = await evolutionManager.create(config);
    sendJson(response, 201, { evolution });
    return;
  }

  const evolutionMatch =
    /^\/api\/evolutions\/([^/]+)\/save$/.exec(pathname);
  if (evolutionMatch !== null) {
    if (request.method !== "POST") {
      methodNotAllowed(response, ["POST"]);
      return;
    }
    let id;
    try {
      id = decodeURIComponent(evolutionMatch[1]);
    } catch {
      throw new ApiError(
        400,
        "invalid_evolution_id",
        "The evolution ID is not valid",
      );
    }
    const deck = deckCatalog.save(id, await readJson(request));
    sendJson(response, 201, { deck });
    return;
  }

  if (pathname === "/api/games") {
    if (request.method !== "POST") {
      methodNotAllowed(response, ["POST"]);
      return;
    }
    const config = normalizeGameConfig(
      await readJson(request),
      deckCatalog.availableDeckIds(),
    );
    const session = await manager.create(config);
    sendJson(response, 201, { game: session.publicState() });
    return;
  }

  const match =
    /^\/api\/games\/([^/]+)(?:\/(actions|bug-report))?$/.exec(pathname);
  if (match === null) {
    throw new ApiError(404, "not_found", "No such API resource");
  }
  const id = decodeURIComponent(match[1]);
  const isActions = match[2] === "actions";
  const isBugReport = match[2] === "bug-report";
  if (isActions) {
    if (request.method !== "POST") {
      methodNotAllowed(response, ["POST"]);
      return;
    }
    const game = await manager.get(id).submitAction(await readJson(request));
    sendJson(response, 200, { game });
    return;
  }
  if (isBugReport) {
    if (request.method !== "GET") {
      methodNotAllowed(response, ["GET"]);
      return;
    }
    sendJson(response, 200, {
      report: manager.get(id).bugReport(),
    });
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
    deckCatalog: options.deckCatalog,
  });
}

function createEvolutionManager(options = {}) {
  const defaultBridge = path.resolve(
    SERVER_DIRECTORY,
    "../build/old-school-web-bridge",
  );
  return new EvolutionManager({
    bridgePath:
      options.bridgePath ??
      process.env.OLD_SCHOOL_WEB_BRIDGE ??
      defaultBridge,
    bridgeArgsPrefix: options.bridgeArgsPrefix ?? [],
    spawnImpl: options.spawnImpl ?? nodeSpawn,
    timeoutMs: options.evolutionTimeoutMs ?? EVOLUTION_TIMEOUT_MS,
    terminationGraceMs: options.evolutionTerminationGraceMs ?? 250,
    maxOutputBytes:
      options.evolutionMaxOutputBytes ?? MAX_EVOLUTION_OUTPUT_BYTES,
    deckCatalog: options.deckCatalog,
  });
}

export function createGameContractHarness(options = {}) {
  const deckCatalog = new EphemeralDeckCatalog(options);
  const manager = createGameManager({ ...options, deckCatalog });
  const evolutionManager = createEvolutionManager({
    ...options,
    deckCatalog,
  });
  return {
    metadata() {
      return webGameMetadata(deckCatalog, evolutionManager);
    },
    async create(config) {
      const session = await manager.create(
        normalizeGameConfig(config, deckCatalog.availableDeckIds()),
      );
      return { game: session.publicState() };
    },
    async evolve(config) {
      const evolution = await evolutionManager.create(
        normalizeEvolutionConfig(config),
      );
      return { evolution };
    },
    saveEvolution(id, body) {
      return { deck: deckCatalog.save(id, body) };
    },
    get(id) {
      return { game: manager.get(id).publicState() };
    },
    report(id) {
      return { report: manager.get(id).bugReport() };
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
      evolutionManager.shutdown();
    },
  };
}

export function createServer(options = {}) {
  const distDirectory = path.resolve(
    options.distDirectory ??
      process.env.OLD_SCHOOL_WEB_DIST ??
      path.join(SERVER_DIRECTORY, "dist-game"),
  );
  const deckCatalog = new EphemeralDeckCatalog(options);
  const manager = createGameManager({ ...options, deckCatalog });
  const evolutionManager = createEvolutionManager({
    ...options,
    deckCatalog,
  });

  const server = createHttpServer(async (request, response) => {
    try {
      const url = new URL(request.url ?? "/", "http://localhost");
      if (url.pathname.startsWith("/api/")) {
        await handleApi(
          request,
          response,
          url.pathname,
          manager,
          evolutionManager,
          deckCatalog,
        );
      } else {
        serveStatic(request, response, url.pathname, distDirectory);
      }
    } catch (error) {
      sendApiError(response, error);
    }
  });

  server.gameManager = manager;
  server.evolutionManager = evolutionManager;
  server.deckCatalog = deckCatalog;
  server.once("close", () => {
    manager.shutdown();
    evolutionManager.shutdown();
  });
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
