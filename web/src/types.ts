export type PlayerIndex = 0 | 1;

export interface TargetReference {
  player: number;
  creature?: string | number;
  label?: string;
}

export type DisplayTarget = string | TargetReference;

export function formatTargetLabel(target: unknown): string | null {
  if (typeof target === "string") {
    const label = target.trim();
    return label || null;
  }
  if (typeof target === "number" && Number.isFinite(target)) {
    return `Target #${target}`;
  }
  if (!target || typeof target !== "object" || Array.isArray(target)) {
    return null;
  }

  const candidate = target as {
    label?: unknown;
    creature?: unknown;
    player?: unknown;
  };
  if (typeof candidate.label === "string" && candidate.label.trim()) {
    return candidate.label.trim();
  }
  if (
    typeof candidate.creature === "string" ||
    (typeof candidate.creature === "number" &&
      Number.isFinite(candidate.creature))
  ) {
    return `Creature #${candidate.creature}`;
  }
  if (candidate.player === 0) return "You";
  if (
    typeof candidate.player === "number" &&
    Number.isFinite(candidate.player)
  ) {
    return "Opponent";
  }
  return "Unknown target";
}

export interface Card {
  id: string | number;
  name: string;
  type?: string;
  cost?: string | number | Record<string, number>;
  costLabel?: string;
  power?: number;
  toughness?: number;
  flying?: boolean;
}

export function hasPublicCombatStats(
  cardType: string | undefined,
  power: number | undefined,
  toughness: number | undefined,
): boolean {
  if (cardType !== undefined) {
    return cardType.toLowerCase().includes("creature");
  }
  return power !== undefined || toughness !== undefined;
}

export interface Permanent {
  permanentId: string | number;
  card: Card;
  tapped?: boolean;
  summoningSick?: boolean;
  damage?: number;
  power?: number;
  toughness?: number;
}

export interface PlayerState {
  life: number;
  librarySize: number;
  handSize: number;
  hand?: Card[];
  revealedHand?: Card[];
  graveyard: Card[];
  exile: Card[];
  lands: Permanent[];
  creatures: Permanent[];
  artifacts: Permanent[];
  enchantments: Permanent[];
  manaPool?: Record<string, number> | string | number;
  extraTurns?: number;
}

export interface StackEntry {
  id?: string | number;
  stackId?: string | number;
  kind?: string;
  label?: string;
  controller?: number;
  card?: Card;
  target?: DisplayTarget;
  targets?: DisplayTarget[];
  spellTarget?: string | number;
  xValue?: number;
}

export function formatStackController(
  controller: number | undefined,
  humanSeat: number,
): "You" | "Opponent" | null {
  if (
    (controller !== 0 && controller !== 1) ||
    (humanSeat !== 0 && humanSeat !== 1)
  ) {
    return null;
  }
  return controller === humanSeat ? "You" : "Opponent";
}

export interface StackInteraction {
  label: string;
  targets: string[];
  summary: string;
}

export function formatStackEntryLabel(entry: StackEntry): string {
  if (entry.card) {
    return entry.kind === "activated_ability"
      ? `${entry.card.name} ability`
      : entry.card.name;
  }
  return entry.label ?? "Ability";
}

export function formatStackTargets(entry: StackEntry): string[] {
  const targets = [
    formatTargetLabel(entry.target),
    ...(entry.targets?.map(formatTargetLabel) ?? []),
    entry.spellTarget !== undefined ? `Stack #${entry.spellTarget}` : null,
  ].filter((value): value is string => value !== null);
  return Array.from(new Set(targets));
}

export function describeTopOfStack(
  stack: readonly StackEntry[],
): StackInteraction | null {
  const top = stack.at(-1);
  if (!top) return null;
  const label = formatStackEntryLabel(top);
  const targets = formatStackTargets(top);
  return {
    label,
    targets,
    summary: `${label}${
      targets.length > 0 ? ` targeting ${targets.join(", ")}` : ""
    } is next to resolve.`,
  };
}

export function stackPermanentTargetIds(
  stack: readonly StackEntry[],
): string[] {
  const ids = new Set<string>();
  for (const entry of stack) {
    for (const target of [entry.target, ...(entry.targets ?? [])]) {
      if (!target || typeof target !== "object" || Array.isArray(target)) {
        continue;
      }
      if (
        typeof target.creature === "string" ||
        (typeof target.creature === "number" &&
          Number.isFinite(target.creature))
      ) {
        ids.add(String(target.creature));
      }
    }
  }
  return [...ids];
}

export interface GameState {
  turnNumber: number;
  activePlayer: number;
  startingPlayer: number;
  phase: string;
  players: [PlayerState, PlayerState];
  stack: StackEntry[];
}

export interface PriorityOption {
  index: number;
  label: string;
  kind: string;
  card?: Card;
  target?: DisplayTarget;
  spellTarget?: string | number;
  sourcePermanent?: string | number;
  xValue?: number;
  chosenCard?: Card;
}

export interface PriorityDecision {
  kind: "priority";
  decisionId: string | number;
  options: PriorityOption[];
}

export function concisePriorityOptionLabel(option: PriorityOption): string {
  if (option.target === undefined && option.spellTarget === undefined) {
    return option.label;
  }
  const targetSeparator = option.label.indexOf(" → ");
  return targetSeparator < 0
    ? option.label
    : option.label.slice(0, targetSeparator);
}

export function priorityOptionsForCard(
  decision: PriorityDecision | undefined,
  cardId: string | number,
): PriorityOption[] {
  if (!decision) return [];
  const key = String(cardId);
  return decision.options.filter(
    (option) =>
      option.card !== undefined &&
      option.sourcePermanent === undefined &&
      String(option.card.id) === key,
  );
}

export function priorityOptionsForSourcePermanent(
  decision: PriorityDecision | undefined,
  permanentId: string | number,
): PriorityOption[] {
  if (!decision) return [];
  const key = String(permanentId);
  return decision.options.filter(
    (option) =>
      option.sourcePermanent !== undefined &&
      String(option.sourcePermanent) === key,
  );
}

// Kinds whose variants differ only by the chosen X value. The bridge
// omits xValue when it is zero, so missing means X=0 inside these kinds.
const X_SPELL_KINDS = new Set([
  "cast_disintegrate",
  "cast_braingeyser",
  "cast_mind_twist",
  "cast_recall",
]);

export interface XSpellGroup {
  card: Card;
  kind: string;
  targetLabel: string | null;
  min: number;
  max: number;
  byX: Map<number, PriorityOption>;
}

// When every pending option is the same X spell at the same target,
// collapse the option list into a single stepper control. The legal X
// range already encodes available mana: the engine only enumerates
// values the player can pay for.
export function xSpellGroupFromOptions(
  options: readonly PriorityOption[],
): XSpellGroup | null {
  if (options.length < 2) return null;
  const first = options[0];
  if (!first.card || !X_SPELL_KINDS.has(first.kind)) return null;
  const destination = priorityDestinationKey(first);
  const byX = new Map<number, PriorityOption>();
  for (const option of options) {
    if (
      option.kind !== first.kind ||
      priorityDestinationKey(option) !== destination
    ) {
      return null;
    }
    const x = option.xValue ?? 0;
    if (byX.has(x)) return null;
    byX.set(x, option);
  }
  const values = [...byX.keys()];
  return {
    card: first.card,
    kind: first.kind,
    targetLabel:
      formatTargetLabel(first.target) ??
      (first.spellTarget !== undefined
        ? `Stack #${first.spellTarget}`
        : null),
    min: Math.min(...values),
    max: Math.max(...values),
    byX,
  };
}

export function clampXValue(group: XSpellGroup, value: number): number {
  if (!Number.isFinite(value)) return group.max;
  let clamped = Math.min(group.max, Math.max(group.min, Math.round(value)));
  // The legal range can in principle be sparse; snap down to the nearest
  // enumerated value.
  while (clamped > group.min && !group.byX.has(clamped)) clamped -= 1;
  return clamped;
}

const TUTOR_TYPE_ORDER = [
  "creature",
  "artifact",
  "enchantment",
  "instant",
  "sorcery",
  "land",
];

function tutorTypeRank(type: string | undefined): number {
  const key = (type ?? "").toLowerCase();
  const rank = TUTOR_TYPE_ORDER.findIndex((entry) => key.includes(entry));
  return rank < 0 ? TUTOR_TYPE_ORDER.length : rank;
}

function cardCostTotal(cost: Card["cost"]): number {
  if (typeof cost === "number") return cost;
  if (!cost || typeof cost !== "object") return 0;
  return Object.values(cost).reduce(
    (total, pips) => total + (Number.isFinite(pips) ? Number(pips) : 0),
    0,
  );
}

// When every pending option is a library search (Demonic Tutor), show a
// browsable picker sorted like a deck list: by type, then cost, then name.
export function tutorChoicesFromOptions(
  options: readonly PriorityOption[],
): PriorityOption[] | null {
  if (options.length < 2) return null;
  if (
    !options.every(
      (option) =>
        option.chosenCard !== undefined &&
        option.kind === options[0].kind,
    )
  ) {
    return null;
  }
  return [...options].sort((left, right) => {
    const a = left.chosenCard as Card;
    const b = right.chosenCard as Card;
    const byType = tutorTypeRank(a.type) - tutorTypeRank(b.type);
    if (byType !== 0) return byType;
    const byCost = cardCostTotal(a.cost) - cardCostTotal(b.cost);
    if (byCost !== 0) return byCost;
    return a.name.localeCompare(b.name);
  });
}

export type PriorityDestinationKey =
  | "play"
  | `permanent:${string}`
  | `player:${number}`
  | `stack:${string}`;

export function priorityDestinationKey(
  option: PriorityOption,
): PriorityDestinationKey | null {
  if (
    option.target &&
    typeof option.target === "object" &&
    !Array.isArray(option.target)
  ) {
    if (
      typeof option.target.creature === "string" ||
      (typeof option.target.creature === "number" &&
        Number.isFinite(option.target.creature))
    ) {
      return `permanent:${String(option.target.creature)}`;
    }
    if (
      typeof option.target.player === "number" &&
      Number.isFinite(option.target.player)
    ) {
      return `player:${option.target.player}`;
    }
    return null;
  }
  if (
    typeof option.spellTarget === "string" ||
    (typeof option.spellTarget === "number" &&
      Number.isFinite(option.spellTarget))
  ) {
    return `stack:${String(option.spellTarget)}`;
  }
  if (option.target !== undefined) return null;
  return "play";
}

export interface AttackersDecision {
  kind: "attackers";
  decisionId: string | number;
  eligible: Array<string | number>;
}

export interface BlockerChoice {
  blocker: string | number;
  legalAttackers: Array<string | number>;
}

export interface BlockersDecision {
  kind: "blockers";
  decisionId: string | number;
  attackers: Array<string | number>;
  choices: BlockerChoice[];
}

export interface DamageOrderDecision {
  kind: "damage_order";
  decisionId: string | number;
  attacker: string | number;
  blockers: Array<string | number>;
}

export interface CleanupDiscardOption {
  index: number;
  card: Card;
}

export interface CleanupDiscardDecision {
  // Sylvan Library's return-2-to-library decision reuses the exact
  // cleanup-discard shape (count + hand-index options).
  kind: "cleanup_discard" | "sylvan_return";
  decisionId: string | number;
  count: number;
  options: CleanupDiscardOption[];
}

export interface MulliganOption {
  index: number;
  label: string;
}

export interface MulliganDecision {
  kind: "mulligan";
  decisionId: string | number;
  handSize: number;
  options: MulliganOption[];
}

export function restoreOpaqueIds(
  selectedKeys: Iterable<string>,
  legalIds: readonly (string | number)[],
): Array<string | number> {
  const byKey = new Map(legalIds.map((id) => [String(id), id]));
  return [...selectedKeys].flatMap((key) => {
    const id = byKey.get(key);
    return id === undefined ? [] : [id];
  });
}

export function blockerPairsFromKeys(
  assignments: Readonly<Record<string, string>>,
  choices: readonly BlockerChoice[],
): Array<[string | number, string | number]> {
  return Object.entries(assignments).flatMap(
    ([blockerKey, attackerKey]) => {
      if (attackerKey === "") return [];
      const choice = choices.find(
        ({ blocker }) => String(blocker) === blockerKey,
      );
      if (!choice) return [];
      const [attacker] = restoreOpaqueIds(
        [attackerKey],
        choice.legalAttackers,
      );
      return attacker === undefined
        ? []
        : [[attacker, choice.blocker] as [string | number, string | number]];
    },
  );
}

export type Decision =
  | PriorityDecision
  | AttackersDecision
  | BlockersDecision
  | DamageOrderDecision
  | CleanupDiscardDecision
  | MulliganDecision;

export interface GameResult {
  winner?: number | null;
  reason?: string;
  turns?: number;
  [key: string]: unknown;
}

export function formatGameResultTitle(
  status: GameSnapshot["status"],
  winner: unknown,
  humanSeat = 0,
): string {
  if (status === "error") return "Match interrupted";
  if (winner !== 0 && winner !== 1) return "The match is a draw";
  return winner === humanSeat ? "You won" : "Opponent won";
}

export function formatGameResultReason(reason: unknown): string | null {
  if (typeof reason !== "string" || !reason.trim()) return null;
  const normalized = reason.trim().toLowerCase();
  if (normalized === "life" || normalized === "life_total") {
    return "The losing player’s life total reached zero.";
  }
  if (normalized === "empty_library") {
    return "The losing player tried to draw from an empty library.";
  }
  if (normalized === "turn_limit") {
    return "The match reached the turn limit.";
  }
  const readable = normalized
    .replaceAll("_", " ")
    .replaceAll("-", " ")
    .replace(/\s+/g, " ");
  return `Match ended: ${readable.charAt(0).toUpperCase()}${readable.slice(1)}.`;
}

export interface PendingCombat {
  attackers: Array<string | number>;
  blocks: Array<Array<string | number>>;
}

export interface GameSnapshot {
  id: string;
  status: "playing" | "finished" | "error";
  pendingCombat?: PendingCombat | null;
  config?: GameConfig | Record<string, unknown>;
  state?: GameState;
  decision?: Decision | null;
  log?: Array<string | LogEntry>;
  result?: GameResult | null;
  model?: ModelIdentity | null;
  error?: string | null;
}

export interface BugReport {
  schema: "old-school-arena-bug-report";
  version: 1;
  match: {
    id: string;
    status: string;
    config: GameConfig;
    model: ModelIdentity | null;
  };
  successfulHumanActions: Array<Record<string, unknown>>;
  publicState: Record<string, unknown>;
  publicChronicle: Array<Record<string, unknown>>;
  currentDecision: Record<string, unknown> | null;
}

export interface LogEntry {
  id?: string | number;
  turn?: number;
  player?: number;
  message?: string;
  text?: string;
  label?: string;
  kind?: string;
  actionKind?: string;
  phase?: string;
}

export interface PublicLogEntry {
  message: string;
  turn?: number;
  player?: number;
  kind?: string;
  actionKind?: string;
}

export interface ChronicleEntry {
  entry: PublicLogEntry;
  sourceIndex: number;
  startsTurn: boolean;
}

function explicitPublicLogMessage(
  entry: string | LogEntry,
): string | undefined {
  if (typeof entry === "string") return entry;
  for (const value of [entry.message, entry.text, entry.label]) {
    if (typeof value === "string") return value;
  }
  return undefined;
}

export function formatPublicLogEntry(
  entry: string | LogEntry,
): PublicLogEntry {
  if (typeof entry === "string") return { message: entry };
  return {
    message: explicitPublicLogMessage(entry) ?? "Game event",
    turn: Number.isSafeInteger(entry.turn) ? entry.turn : undefined,
    player: Number.isSafeInteger(entry.player) ? entry.player : undefined,
    kind: typeof entry.kind === "string" ? entry.kind : undefined,
    ...(typeof entry.actionKind === "string"
      ? { actionKind: entry.actionKind }
      : {}),
  };
}

export function chronicleEntries(
  entries: readonly (string | LogEntry)[],
  showPriorityPasses: boolean,
): ChronicleEntry[] {
  const visible: ChronicleEntry[] = [];
  let previousTurn: number | undefined;

  entries.forEach((raw, sourceIndex) => {
    const entry = formatPublicLogEntry(raw);
    if (
      !showPriorityPasses &&
      entry.kind === "priority_action" &&
      entry.actionKind === "pass"
    ) {
      return;
    }

    const startsTurn =
      entry.turn !== undefined && entry.turn !== previousTurn;
    visible.push({ entry, sourceIndex, startsTurn });
    if (entry.turn !== undefined) previousTurn = entry.turn;
  });

  return visible;
}

export function latestPublicEventMessage(
  entries: readonly (string | LogEntry)[],
): string | undefined {
  const latest = entries.at(-1);
  if (latest === undefined) return undefined;
  return explicitPublicLogMessage(latest);
}

export type DeckCard =
  | string
  | {
      name: string;
      count?: number;
    };

export interface DeckMeta {
  id: string;
  name: string;
  cards: DeckCard[];
  ephemeral?: boolean;
}

export interface PolicyMeta {
  id: string;
  name: string;
  description?: string;
  versionDate?: string;
  versionDateLabel?: string;
  lifecycle?: string;
}

export interface EvolutionPilotMeta {
  id: string;
  name: string;
  description?: string;
}

export interface EvolutionNumericLimit {
  min: number | string;
  max: number | string;
}

export interface EvolutionConfig {
  seed: number | string;
  generations: number;
  population: number;
  games: number;
  pilot: string;
}

export interface EvolutionMeta {
  pilots: EvolutionPilotMeta[];
  defaults: EvolutionConfig;
  limits: {
    seed: EvolutionNumericLimit;
    generations: EvolutionNumericLimit;
    population: EvolutionNumericLimit;
    games: EvolutionNumericLimit;
  };
  lifetime: string;
  active?: boolean;
  storage?: string;
}

export interface EvolutionStats {
  games: number;
  wins: number;
  losses: number;
  draws: number;
  winRate: number;
}

export interface EvolvedCard {
  id: string | number;
  name: string;
  count: number;
}

export function evolvedDeckCardCount(cards: readonly EvolvedCard[]): number {
  return cards.reduce((total, card) => total + card.count, 0);
}

export function formatEvolutionPercent(value: number): string {
  return `${Number.isFinite(value) ? value.toFixed(1) : "0.0"}%`;
}

export interface EvolutionOpponentStats extends EvolutionStats {
  deckId: string;
  name: string;
}

export interface EvolutionResult {
  id: string;
  seed: number | string;
  pilot: string;
  config?: EvolutionConfig;
  generations: number[];
  population?: number;
  games?: number;
  best: {
    cards: EvolvedCard[];
    stats: EvolutionStats;
    byOpponent: EvolutionOpponentStats[];
  };
  top?: {
    cards: EvolvedCard[];
    stats: EvolutionStats;
  }[];
}

export function formatPolicyVersionDate(value: string): string {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value);
  if (!match) return value;
  const month = Number(match[2]);
  const day = Number(match[3]);
  const monthNames = [
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec",
  ];
  if (month < 1 || month > monthNames.length || day < 1 || day > 31) {
    return value;
  }
  return `${monthNames[month - 1]} ${day}, ${match[1]}`;
}

export interface MetaResponse {
  decks: DeckMeta[];
  policies: PolicyMeta[];
  defaults?: Record<string, unknown>;
  evolution?: EvolutionMeta;
}

export interface SeatConfig {
  deckId: string;
  policyId: string;
}

export interface GameConfig {
  seed: number | string;
  debugReveal: boolean;
  bluffMode: boolean;
  rollouts: number;
  deepRollouts: number;
  players: [SeatConfig, SeatConfig];
}

export interface ModelIdentity {
  family: string;
  generation: number;
  searchWorlds: number;
  horizonTurns: number;
  source: string;
  fingerprint: string;
}

export interface ReproductionPublicContext {
  turnNumber?: number;
  phase?: string;
  priorityHolder: "You" | "None" | "Unknown";
  latestEvent?: string;
  model?: ModelIdentity | null;
}

export function reproductionPriorityHolder(
  decisionKind: string | undefined,
  hasPublicState: boolean,
): ReproductionPublicContext["priorityHolder"] {
  if (decisionKind === "priority") return "You";
  return hasPublicState ? "None" : "Unknown";
}

export function formatReproductionSummary(
  config: GameConfig,
  context: ReproductionPublicContext,
): string {
  const turnNumber = Number.isSafeInteger(context.turnNumber)
    ? String(context.turnNumber)
    : "unknown";
  const phase = context.phase?.length ? context.phase : "unknown";
  const latestEvent =
    context.latestEvent === undefined
      ? "none"
      : JSON.stringify(context.latestEvent);
  return [
    `you=${config.players[0].deckId}/${config.players[0].policyId}`,
    `opponent=${config.players[1].deckId}/${config.players[1].policyId}`,
    `game-seed=${String(config.seed)}`,
    `rollouts=${config.rollouts}`,
    `deep-rollouts=${config.deepRollouts}`,
    context.model
      ? `model=${context.model.family}/C${context.model.generation}`
      : "model=none",
    context.model
      ? `model-fingerprint=${context.model.fingerprint}`
      : "model-fingerprint=none",
    context.model
      ? `model-search=K${context.model.searchWorlds}/H${context.model.horizonTurns}`
      : "model-search=none",
    `bluff=${config.bluffMode ? "on" : "off"}`,
    `reveal=${config.debugReveal ? "on" : "off"}`,
    `turn=${turnNumber}`,
    `phase=${phase}`,
    `priority-holder=${context.priorityHolder}`,
    `latest-event=${latestEvent}`,
  ].join(" | ");
}

export type ActionRequest =
  | { decisionId: string | number; index: number }
  | { decisionId: string | number; ids: Array<string | number> }
  | {
      decisionId: string | number;
      pairs: Array<[string | number, string | number]>;
    }
  | { decisionId: string | number; indices: number[] };
