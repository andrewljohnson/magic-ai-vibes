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
}

export interface PriorityDecision {
  kind: "priority";
  decisionId: string | number;
  options: PriorityOption[];
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

export type Decision =
  | PriorityDecision
  | AttackersDecision
  | BlockersDecision
  | DamageOrderDecision;

export interface GameResult {
  winner?: number | null;
  reason?: string;
  turns?: number;
  [key: string]: unknown;
}

export interface GameSnapshot {
  id: string;
  status: "playing" | "finished" | "error";
  config?: GameConfig | Record<string, unknown>;
  state?: GameState;
  decision?: Decision | null;
  log?: Array<string | LogEntry>;
  result?: GameResult | null;
  error?: string | null;
}

export interface LogEntry {
  id?: string | number;
  turn?: number;
  player?: number;
  message?: string;
  text?: string;
  label?: string;
  kind?: string;
  phase?: string;
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
}

export interface PolicyMeta {
  id: string;
  name: string;
  description?: string;
}

export interface MetaResponse {
  decks: DeckMeta[];
  policies: PolicyMeta[];
  defaults?: Record<string, unknown>;
}

export interface SeatConfig {
  deckId: string;
  policyId: string;
}

export interface GameConfig {
  seed: number;
  trainGames: number;
  trainSeed: number;
  debugReveal: boolean;
  players: [SeatConfig, SeatConfig];
}

export type ActionRequest =
  | { decisionId: string | number; index: number }
  | { decisionId: string | number; ids: Array<string | number> }
  | {
      decisionId: string | number;
      pairs: Array<[string | number, string | number]>;
    };
