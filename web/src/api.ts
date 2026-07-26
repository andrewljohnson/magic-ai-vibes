import type {
  ActionRequest,
  DeckCard,
  GameConfig,
  GameSnapshot,
  MetaResponse,
} from "./types";
import { apiRequestErrorFromResponse } from "./errors";

const apiBase = (import.meta.env.VITE_API_BASE ?? "").replace(/\/$/, "");

type RawGameStatus =
  | GameSnapshot["status"]
  | "awaiting_action"
  | "advancing"
  | "failed";

interface SessionEnvelope {
  game?: {
    id: string;
    status: RawGameStatus;
    config?: GameSnapshot["config"];
    snapshot?: Partial<GameSnapshot> | GameSnapshot["state"];
    decision?: GameSnapshot["decision"];
    events?: GameSnapshot["log"];
    log?: GameSnapshot["log"];
    result?: GameSnapshot["result"];
    model?: GameSnapshot["model"];
    error?: string | { message?: string };
  };
}

function normalizeStatus(status: RawGameStatus): GameSnapshot["status"] {
  if (status === "finished") return "finished";
  if (status === "error" || status === "failed") return "error";
  return "playing";
}

function normalizeDecision(
  value: GameSnapshot["decision"] | (Record<string, unknown> & { id?: unknown }),
): GameSnapshot["decision"] {
  if (!value) return null;
  if ("decisionId" in value && value.decisionId !== undefined)
    return value as GameSnapshot["decision"];
  if ("id" in value && value.id !== undefined)
    return {
      ...value,
      decisionId: value.id,
    } as GameSnapshot["decision"];
  return value as GameSnapshot["decision"];
}

function parseDeckList(value: unknown): DeckCard[] {
  if (Array.isArray(value)) return value as DeckCard[];
  if (typeof value !== "string") return [];
  return value
    .split(/\r?\n|,\s*/)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => {
      const leadingCount = line.match(/^(\d+)\s*[x×]?\s+(.+)$/i);
      if (leadingCount)
        return { name: leadingCount[2].trim(), count: Number(leadingCount[1]) };
      const trailingCount = line.match(/^(.+?)\s*[x×]\s*(\d+)$/i);
      if (trailingCount)
        return { name: trailingCount[1].trim(), count: Number(trailingCount[2]) };
      return line;
    });
}

function normalizeMeta(value: unknown): MetaResponse {
  const raw = value as {
    decks?: Array<{
      id: string;
      name?: string;
      label?: string;
      cards?: unknown;
      deckList?: unknown;
    }>;
    policies?: Array<{
      id: string;
      name?: string;
      label?: string;
      description?: string;
    }>;
    defaults?: Record<string, unknown>;
  };
  return {
    decks: (raw.decks ?? []).map((deck) => ({
      id: deck.id,
      name: deck.name ?? deck.label ?? deck.id,
      cards: parseDeckList(deck.cards ?? deck.deckList),
    })),
    policies: (raw.policies ?? []).map((policy) => ({
      id: policy.id,
      name: policy.name ?? policy.label ?? policy.id,
      description: policy.description,
    })),
    defaults: raw.defaults,
  };
}

function errorMessage(value: unknown, fallback: string): string {
  if (typeof value === "string") return value;
  if (value && typeof value === "object" && "message" in value)
    return String(value.message);
  return fallback;
}

function normalizeSnapshot(value: GameSnapshot | SessionEnvelope): GameSnapshot {
  if (!("game" in value) || !value.game) return value as GameSnapshot;
  const game = value.game;
  const embedded = game.snapshot ?? {};
  const embeddedRecord = embedded as Partial<GameSnapshot>;
  const looksLikeState =
    embedded &&
    typeof embedded === "object" &&
    "players" in embedded &&
    "turnNumber" in embedded;
  return {
    id: game.id,
    status: normalizeStatus(game.status),
    config: game.config ?? embeddedRecord.config,
    state: looksLikeState
      ? (embedded as GameSnapshot["state"])
      : embeddedRecord.state,
    decision: normalizeDecision(
      (game.decision ?? embeddedRecord.decision ?? null) as
        | GameSnapshot["decision"]
        | (Record<string, unknown> & { id?: unknown }),
    ),
    log: game.events ?? game.log ?? embeddedRecord.log ?? [],
    result: game.result ?? embeddedRecord.result ?? null,
    model: game.model ?? embeddedRecord.model ?? null,
    error:
      errorMessage(game.error, "") ||
      (typeof embeddedRecord.error === "string" ? embeddedRecord.error : null),
  };
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(`${apiBase}${path}`, {
    ...init,
    headers: {
      Accept: "application/json",
      ...(init?.body ? { "Content-Type": "application/json" } : {}),
      ...init?.headers,
    },
  });

  const body = await response.json().catch(() => null);
  if (!response.ok) {
    throw apiRequestErrorFromResponse(response.status, body);
  }
  return body as T;
}

export function fetchMeta(signal?: AbortSignal): Promise<MetaResponse> {
  return request<unknown>("/api/meta", { signal }).then(normalizeMeta);
}

export function createGame(config: GameConfig): Promise<GameSnapshot> {
  return request<GameSnapshot | SessionEnvelope>("/api/games", {
    method: "POST",
    body: JSON.stringify(config),
  }).then(normalizeSnapshot);
}

export function fetchGame(
  id: string,
  signal?: AbortSignal,
): Promise<GameSnapshot> {
  return request<GameSnapshot | SessionEnvelope>(
    `/api/games/${encodeURIComponent(id)}`,
    { signal },
  ).then(normalizeSnapshot);
}

export function submitAction(
  id: string,
  action: ActionRequest,
): Promise<GameSnapshot> {
  return request<GameSnapshot | SessionEnvelope>(
    `/api/games/${encodeURIComponent(id)}/actions`,
    {
      method: "POST",
      body: JSON.stringify(action),
    },
  ).then(normalizeSnapshot);
}

export function deleteGame(id: string): Promise<void> {
  return request<unknown>(
    `/api/games/${encodeURIComponent(id)}`,
    { method: "DELETE" },
  ).then(() => undefined);
}
