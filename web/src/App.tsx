import {
  type CSSProperties,
  type DragEvent as ReactDragEvent,
  FormEvent,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import {
  createGame,
  deleteGame,
  fetchGame,
  fetchMeta,
  submitAction,
} from "./api";
import { ApiRequestError } from "./errors";
import {
  blockerPairsFromKeys,
  concisePriorityOptionLabel,
  describeTopOfStack,
  formatStackEntryLabel,
  formatStackTargets,
  formatGameResultReason,
  formatGameResultTitle,
  formatTargetLabel,
  priorityDestinationKey,
  priorityOptionsForCard,
  priorityOptionsForSourcePermanent,
  restoreOpaqueIds,
  stackPermanentTargetIds,
  type ActionRequest,
  type AttackersDecision,
  type BlockersDecision,
  type Card,
  type DamageOrderDecision,
  type Decision,
  type DeckMeta,
  type GameConfig,
  type GameSnapshot,
  type LogEntry,
  type MetaResponse,
  type Permanent,
  type PlayerIndex,
  type PlayerState,
  type PolicyMeta,
  type PriorityDecision,
  type PriorityDestinationKey,
  type PriorityOption,
  type StackInteraction,
  type StackEntry,
} from "./types";

const PHASES = ["Beginning", "Main I", "Combat", "Main II", "Ending"];

const FALLBACK_POLICIES: PolicyMeta[] = [
  { id: "random", name: "Random", description: "Fast legal random play." },
  {
    id: "monte-carlo",
    name: "Monte Carlo",
    description: "Samples short random futures.",
  },
  {
    id: "deep-monte-carlo",
    name: "Deep Monte Carlo",
    description: "Spends more rollouts per decision.",
  },
  {
    id: "handcrafted",
    name: "HandcodedPolicy",
    description: "A compact rules-aware benchmark.",
  },
  {
    id: "learned-value",
    name: "Learned Value",
    description: "Hidden-information-safe learned search.",
  },
  {
    id: "learned-actor",
    name: "Learned Actor",
    description: "A learned policy distilled from self-play.",
  },
];

const HUMAN_POLICY: PolicyMeta = {
  id: "human",
  name: "You — Interactive",
  description: "Stop for your choices and play in this browser.",
};

function readDefault<T>(
  defaults: Record<string, unknown> | undefined,
  keys: string[],
  fallback: T,
): T {
  for (const key of keys) {
    const value = defaults?.[key];
    if (value !== undefined && value !== null) return value as T;
  }
  return fallback;
}

function randomSeed(): number {
  const buffer = new Uint32Array(1);
  crypto.getRandomValues(buffer);
  return buffer[0] || 1;
}

function phaseIndex(phase: string): number {
  const value = phase.toLowerCase();
  if (
    value.includes("untap") ||
    value.includes("upkeep") ||
    value.includes("draw") ||
    value.includes("begin")
  )
    return 0;
  if (value.includes("precombat") || value.includes("main 1")) return 1;
  if (
    value.includes("combat") ||
    value.includes("attack") ||
    value.includes("block") ||
    value.includes("damage")
  )
    return 2;
  if (value.includes("postcombat") || value.includes("main 2")) return 3;
  return 4;
}

function formatPhase(phase: string): string {
  return phase
    .replaceAll("_", " ")
    .replace(/\b\w/g, (letter) => letter.toUpperCase());
}

function cardColor(card: Card): string {
  const cost = formatCost(card.cost, card.costLabel).toUpperCase();
  const text =
    `${card.type ?? ""} ${card.name}`.toUpperCase();
  if (
    cost.includes("U") ||
    text.includes(" ISLAND") ||
    text.startsWith("ISLAND")
  )
    return "blue";
  if (
    cost.includes("R") ||
    text.includes(" MOUNTAIN") ||
    text.startsWith("MOUNTAIN")
  )
    return "red";
  if (
    cost.includes("G") ||
    text.includes(" FOREST") ||
    text.startsWith("FOREST")
  )
    return "green";
  if (
    cost.includes("W") ||
    text.includes(" PLAINS") ||
    text.startsWith("PLAINS")
  )
    return "white";
  if (text.includes("ARTIFACT") || text.includes("MOX") || text.includes("SOL RING"))
    return "artifact";
  return "neutral";
}

function formatCost(
  cost?: Card["cost"],
  costLabel?: Card["costLabel"],
): string {
  if (costLabel) return costLabel.replaceAll("{", "").replaceAll("}", "");
  if (typeof cost === "string")
    return cost.replaceAll("{", "").replaceAll("}", "").replaceAll(" ", "");
  if (typeof cost === "number") return cost > 0 ? String(cost) : "";
  if (cost && typeof cost === "object") {
    const preferredOrder = ["generic", "colorless", "W", "U", "B", "R", "G"];
    return Object.entries(cost)
      .filter(([, amount]) => amount > 0)
      .sort(([left], [right]) => {
        const leftRank = preferredOrder.indexOf(left);
        const rightRank = preferredOrder.indexOf(right);
        return (leftRank < 0 ? 99 : leftRank) - (rightRank < 0 ? 99 : rightRank);
      })
      .map(([symbol, amount]) => {
        const display =
          symbol === "generic" || symbol === "colorless"
            ? String(amount)
            : symbol.toUpperCase().repeat(amount);
        return display;
      })
      .join("");
  }
  return "";
}

function zoneCount(zone?: unknown[]): number {
  return Array.isArray(zone) ? zone.length : 0;
}

function permanentId(permanent: Permanent): string {
  return String(permanent.permanentId);
}

function priorityOptionElementId(
  decisionId: string | number,
  optionIndex: number,
): string {
  return `priority-option-${encodeURIComponent(String(decisionId))}-${optionIndex}`;
}

function cardFromPermanent(
  permanent: Permanent | undefined,
): Card | undefined {
  return permanent?.card;
}

function findPermanent(
  state: PlayerState | undefined,
  id: string | number,
): Permanent | undefined {
  if (!state) return undefined;
  return [
    ...(state.lands ?? []),
    ...(state.creatures ?? []),
    ...(state.artifacts ?? []),
    ...(state.enchantments ?? []),
  ].find((permanent) => String(permanent.permanentId) === String(id));
}

function playerLabel(snapshot: GameSnapshot | null, seat: PlayerIndex): string {
  const config = snapshot?.config as GameConfig | undefined;
  const policyId = config?.players?.[seat]?.policyId;
  if (policyId === "human") return "You";
  if (seat === 0) return "Player One";
  return "Opponent";
}

function policyLabel(
  snapshot: GameSnapshot | null,
  meta: MetaResponse | null,
  seat: PlayerIndex,
): string {
  const config = snapshot?.config as GameConfig | undefined;
  const id = config?.players?.[seat]?.policyId;
  if (id === "human") return "Interactive";
  return (
    meta?.policies.find((policy) => policy.id === id)?.name ??
    FALLBACK_POLICIES.find((policy) => policy.id === id)?.name ??
    id ??
    "Pilot"
  );
}

function deckLabel(
  snapshot: GameSnapshot | null,
  meta: MetaResponse | null,
  seat: PlayerIndex,
): string {
  const config = snapshot?.config as GameConfig | undefined;
  const id = config?.players?.[seat]?.deckId;
  return meta?.decks.find((deck) => deck.id === id)?.name ?? id ?? "Deck";
}

interface CardFaceProps {
  card: Card;
  permanent?: Permanent;
  className?: string;
  compact?: boolean;
  selected?: boolean;
  actionable?: boolean;
  targeted?: boolean;
  choiceTarget?: boolean;
  choiceOrigin?: boolean;
  draggable?: boolean;
  onClick?: () => void;
  onDoubleClick?: () => void;
  onDragStart?: (event: ReactDragEvent<HTMLButtonElement>) => void;
  onDragEnd?: (event: ReactDragEvent<HTMLButtonElement>) => void;
  onDragOver?: (event: ReactDragEvent<HTMLButtonElement>) => void;
  onDrop?: (event: ReactDragEvent<HTMLButtonElement>) => void;
}

function CardFace({
  card,
  permanent,
  className = "",
  compact = false,
  selected = false,
  actionable = false,
  targeted = false,
  choiceTarget = false,
  choiceOrigin = false,
  draggable = false,
  onClick,
  onDoubleClick,
  onDragStart,
  onDragEnd,
  onDragOver,
  onDrop,
}: CardFaceProps) {
  const power = permanent?.power ?? card.power;
  const toughness = permanent?.toughness ?? card.toughness;
  const hasStats = power !== undefined || toughness !== undefined;
  const title = [
    card.name,
    card.type,
    card.cost || card.costLabel
      ? `Cost ${formatCost(card.cost, card.costLabel)}`
      : undefined,
    permanent?.tapped ? "Tapped" : undefined,
    permanent?.summoningSick ? "Summoning sick" : undefined,
    permanent?.damage ? `${permanent.damage} damage marked` : undefined,
    targeted ? "Targeted by an object on the stack" : undefined,
    choiceTarget ? "Legal destination for the selected action" : undefined,
    choiceOrigin ? "Legal activated-ability origin" : undefined,
  ]
    .filter(Boolean)
    .join(" • ");

  return (
    <button
      className={`card-face card-${cardColor(card)} ${
        compact ? "card-compact" : ""
      } ${selected ? "is-selected" : ""} ${
        actionable ? "is-actionable" : ""
      } ${targeted ? "is-targeted" : ""} ${
        choiceTarget ? "is-choice-target" : ""
      } ${choiceOrigin ? "is-choice-origin" : ""} ${className}`}
      type="button"
      title={title}
      onClick={onClick}
      onDoubleClick={onDoubleClick}
      draggable={draggable}
      onDragStart={onDragStart}
      onDragEnd={onDragEnd}
      onDragOver={onDragOver}
      onDrop={onDrop}
      disabled={!onClick && !draggable}
      aria-pressed={actionable ? selected : undefined}
      aria-label={`${card.name}${permanent?.tapped ? ", tapped" : ""}${
        targeted ? ", targeted by the stack" : ""
      }`}
    >
      <span className="card-edge" />
      <span className="card-heading">
        <span className="card-name">{card.name}</span>
        {(card.cost || card.costLabel) && (
          <span className="mana-cost">
            {formatCost(card.cost, card.costLabel)}
          </span>
        )}
      </span>
      <span className="card-field">
        <span className="card-glyph" aria-hidden="true">
          {card.flying ? "✦" : card.type?.toLowerCase().includes("land") ? "◆" : "◇"}
        </span>
      </span>
      <span className="card-footer">
        <span className="card-type">{card.type ?? "Card"}</span>
        {hasStats && (
          <span className="combat-stats">
            {power ?? "–"}/{toughness ?? "–"}
          </span>
        )}
      </span>
      {permanent?.summoningSick && (
        <span className="status-token status-sick">SICK</span>
      )}
      {!!permanent?.damage && (
        <span className="status-token status-damage">−{permanent.damage}</span>
      )}
      {targeted && <span className="status-token status-targeted">TARGET</span>}
      {choiceTarget && <span className="status-token status-choice">CHOOSE</span>}
      {choiceOrigin && (
        <span className="status-token status-origin">ACTIVATE</span>
      )}
    </button>
  );
}

function HiddenHand({ count }: { count: number }) {
  const visible = Math.min(count, 10);
  return (
    <div className="hidden-hand" aria-label={`${count} hidden cards in hand`}>
      {Array.from({ length: visible }, (_, index) => (
        <div
          className="card-back"
          key={index}
          style={
            {
              "--hand-angle": `${
                (index - Math.max(visible - 1, 1) / 2) * 3
              }deg`,
              "--hand-lift": `${Math.abs(
                index - Math.max(visible - 1, 1) / 2,
              )}px`,
            } as CSSProperties
          }
        >
          <span>OLD<br />SCHOOL</span>
        </div>
      ))}
      {count > visible && <span className="hand-overflow">+{count - visible}</span>}
    </div>
  );
}

function FaceUpHand({
  cards,
  label,
  debug = false,
  playableCardIds,
  onCardClick,
  onCardDoubleClick,
  selectedCardKey,
  draggedCardKey,
  onCardDragStart,
  onCardDragEnd,
}: {
  cards: Card[];
  label: string;
  debug?: boolean;
  playableCardIds?: ReadonlySet<string>;
  onCardClick?: (card: Card, renderKey: string) => void;
  onCardDoubleClick?: (card: Card, renderKey: string) => void;
  selectedCardKey?: string;
  draggedCardKey?: string;
  onCardDragStart?: (
    card: Card,
    renderKey: string,
    event: ReactDragEvent<HTMLButtonElement>,
  ) => void;
  onCardDragEnd?: () => void;
}) {
  const playableCount = cards.filter((card) =>
    playableCardIds?.has(String(card.id)),
  ).length;
  return (
    <section
      className={`face-hand ${debug ? "debug-hand" : ""}`}
      aria-label={`${label}, ${cards.length} cards`}
    >
      <div className="hand-label">
        {debug && <span className="debug-pill">DEBUG REVEAL</span>}
        <span>{label}</span>
        <strong>{cards.length}</strong>
        {!debug && playableCount > 0 && (
          <small>{playableCount} playable</small>
        )}
      </div>
      <div className="hand-fan" role="list" aria-label="Cards in your hand">
        {cards.map((card, index) => {
          const playable =
            !debug && (playableCardIds?.has(String(card.id)) ?? false);
          const renderKey = `${String(card.id)}:${index}`;
          return (
            <div
              className={`hand-card-wrap ${playable ? "is-playable" : ""} ${
                selectedCardKey === renderKey ? "is-selected" : ""
              } ${
                draggedCardKey === renderKey ? "is-dragging" : ""
              }`}
              key={`${card.id}-${index}`}
              role="listitem"
              style={
                {
                  "--fan-angle": `${
                    (index - (cards.length - 1) / 2) * 1.25
                  }deg`,
                  "--fan-lift": `${Math.min(
                    Math.abs(index - (cards.length - 1) / 2) * 1.2,
                    8,
                  )}px`,
                } as CSSProperties
              }
            >
              <CardFace
                card={card}
                actionable={playable}
                draggable={playable && Boolean(onCardDragStart)}
                onDragStart={
                  playable && onCardDragStart
                    ? (event) => onCardDragStart(card, renderKey, event)
                    : undefined
                }
                onDragEnd={
                  playable && onCardDragEnd ? onCardDragEnd : undefined
                }
                onClick={
                  playable && onCardClick
                    ? () => onCardClick(card, renderKey)
                    : undefined
                }
                onDoubleClick={
                  playable && onCardDoubleClick
                    ? () => onCardDoubleClick(card, renderKey)
                    : undefined
                }
              />
            </div>
          );
        })}
      </div>
    </section>
  );
}

function ZoneBadge({
  label,
  count,
  subtle = false,
}: {
  label: string;
  count: number;
  subtle?: boolean;
}) {
  return (
    <div className={`zone-badge ${subtle ? "is-subtle" : ""}`}>
      <span>{label}</span>
      <strong>{count}</strong>
    </div>
  );
}

function ManaPool({ value }: { value: PlayerState["manaPool"] }) {
  let entries: Array<[string, number]> = [];
  if (typeof value === "number" && value > 0) entries = [["◇", value]];
  if (typeof value === "string" && value.length > 0)
    entries = [[value.toUpperCase(), 1]];
  if (value && typeof value === "object")
    entries = Object.entries(value).filter(([, amount]) => amount > 0);
  if (entries.length === 0) return null;
  return (
    <div className="mana-pool" aria-label="Mana pool">
      {entries.map(([symbol, amount]) => (
        <span className={`mana mana-${symbol.toLowerCase()}`} key={symbol}>
          {symbol}
          {amount > 1 && <sup>{amount}</sup>}
        </span>
      ))}
    </div>
  );
}

function PlayerHud({
  player,
  label,
  deck,
  policy,
  active,
  opponent,
  priorityTarget,
  draggingPriority,
  onChoosePriorityTarget,
  onDropPriorityTarget,
}: {
  player: PlayerState;
  label: string;
  deck: string;
  policy: string;
  active: boolean;
  opponent?: boolean;
  priorityTarget?: boolean;
  draggingPriority?: boolean;
  onChoosePriorityTarget?: () => void;
  onDropPriorityTarget?: () => void;
}) {
  return (
    <div
      className={`player-hud ${active ? "is-active" : ""} ${
        opponent ? "is-opponent" : ""
      }`}
    >
      <div className="identity-orb" aria-hidden="true">
        {label.slice(0, 1)}
      </div>
      <div className="player-copy">
        <div className="player-line">
          <strong>{label}</strong>
          {active && <span className="active-chip">ACTIVE</span>}
        </div>
        <span>
          {deck} · {policy}
        </span>
      </div>
      <div className="life-medallion">
        <span>LIFE</span>
        <strong>{player.life}</strong>
      </div>
      <div className="hud-zones">
        <ZoneBadge label="LIB" count={player.librarySize} />
        {zoneCount(player.graveyard) > 0 && (
          <ZoneBadge label="GY" count={zoneCount(player.graveyard)} />
        )}
        {zoneCount(player.exile) > 0 && (
          <ZoneBadge label="EX" count={zoneCount(player.exile)} />
        )}
      </div>
      <ManaPool value={player.manaPool} />
      {!!player.extraTurns && (
        <div className="extra-turns">+{player.extraTurns} TURN</div>
      )}
      {priorityTarget && (
        <button
          className={`surface-action player-surface-action ${
            draggingPriority ? "is-drop-target" : ""
          }`}
          type="button"
          onClick={onChoosePriorityTarget}
          onDragOver={(event) => {
            if (!draggingPriority) return;
            event.preventDefault();
            event.dataTransfer.dropEffect = "move";
          }}
          onDrop={(event) => {
            if (!draggingPriority) return;
            event.preventDefault();
            event.stopPropagation();
            onDropPriorityTarget?.();
          }}
        >
          {draggingPriority ? "DROP ON PLAYER" : "CHOOSE PLAYER"}
        </button>
      )}
    </div>
  );
}

function PermanentRow({
  title,
  permanents,
  eligibleIds,
  selectedIds,
  targetedIds,
  priorityDestinationIds,
  priorityOriginIds,
  selectedPriorityOriginId,
  draggingPriority,
  onToggle,
  onChoosePriorityDestination,
  onSelectPriorityOrigin,
  onStartPriorityOriginDrag,
  onEndPriorityDrag,
}: {
  title: string;
  permanents: Permanent[];
  eligibleIds?: Set<string>;
  selectedIds?: Set<string>;
  targetedIds?: ReadonlySet<string>;
  priorityDestinationIds?: ReadonlySet<string>;
  priorityOriginIds?: ReadonlySet<string>;
  selectedPriorityOriginId?: string;
  draggingPriority?: boolean;
  onToggle?: (id: string) => void;
  onChoosePriorityDestination?: (id: string) => void;
  onSelectPriorityOrigin?: (id: string) => void;
  onStartPriorityOriginDrag?: (
    id: string,
    event: ReactDragEvent<HTMLButtonElement>,
  ) => void;
  onEndPriorityDrag?: () => void;
}) {
  if (permanents.length === 0) return null;
  return (
    <div className="permanent-row">
      <span className="row-label">{title}</span>
      <div className="permanent-list">
        {permanents.map((permanent) => {
          const id = permanentId(permanent);
          const eligible = eligibleIds?.has(id) ?? false;
          const priorityDestination =
            priorityDestinationIds?.has(id) ?? false;
          const priorityOrigin = priorityOriginIds?.has(id) ?? false;
          const onClick = priorityDestination
            ? () => onChoosePriorityDestination?.(id)
            : priorityOrigin
              ? () => onSelectPriorityOrigin?.(id)
              : eligible && onToggle
                ? () => onToggle(id)
                : undefined;
          return (
            <div
              className={`permanent-slot ${
                permanent.tapped ? "is-tapped" : ""
              } ${priorityDestination ? "is-priority-destination" : ""} ${
                priorityOrigin ? "is-priority-origin" : ""
              }`}
              key={id}
            >
              <CardFace
                card={permanent.card}
                permanent={permanent}
                selected={
                  selectedIds?.has(id) || selectedPriorityOriginId === id
                }
                actionable={eligible || priorityDestination || priorityOrigin}
                targeted={targetedIds?.has(id)}
                choiceTarget={priorityDestination}
                choiceOrigin={priorityOrigin}
                onClick={onClick}
                draggable={priorityOrigin && Boolean(onStartPriorityOriginDrag)}
                onDragStart={
                  priorityOrigin && onStartPriorityOriginDrag
                    ? (event) => onStartPriorityOriginDrag(id, event)
                    : undefined
                }
                onDragEnd={
                  priorityOrigin && onEndPriorityDrag
                    ? onEndPriorityDrag
                    : undefined
                }
                onDragOver={
                  priorityDestination && draggingPriority
                    ? (event) => {
                        event.preventDefault();
                        event.dataTransfer.dropEffect = "move";
                      }
                    : undefined
                }
                onDrop={
                  priorityDestination && draggingPriority
                    ? (event) => {
                        event.preventDefault();
                        event.stopPropagation();
                        onEndPriorityDrag?.();
                        onChoosePriorityDestination?.(id);
                      }
                    : undefined
                }
              />
            </div>
          );
        })}
      </div>
    </div>
  );
}

function BattlefieldSide({
  player,
  seat,
  snapshot,
  meta,
  active,
  opponent,
  attackerDecision,
  selectedAttackers,
  targetedPermanentIds,
  priorityDestinationIds,
  priorityOriginIds,
  priorityPlayerTargets,
  priorityPlayTarget,
  selectedPriorityOriginId,
  draggingPriority,
  onToggleAttacker,
  onChoosePriorityDestination,
  onChoosePriorityPlayer,
  onChoosePriorityPlay,
  onSelectPriorityOrigin,
  onStartPriorityOriginDrag,
  onEndPriorityDrag,
}: {
  player: PlayerState;
  seat: PlayerIndex;
  snapshot: GameSnapshot;
  meta: MetaResponse | null;
  active: boolean;
  opponent?: boolean;
  attackerDecision?: AttackersDecision;
  selectedAttackers?: Set<string>;
  targetedPermanentIds?: ReadonlySet<string>;
  priorityDestinationIds?: ReadonlySet<string>;
  priorityOriginIds?: ReadonlySet<string>;
  priorityPlayerTargets?: ReadonlySet<number>;
  priorityPlayTarget?: boolean;
  selectedPriorityOriginId?: string;
  draggingPriority?: boolean;
  onToggleAttacker?: (id: string) => void;
  onChoosePriorityDestination?: (id: string) => void;
  onChoosePriorityPlayer?: (seat: PlayerIndex) => void;
  onChoosePriorityPlay?: () => void;
  onSelectPriorityOrigin?: (id: string) => void;
  onStartPriorityOriginDrag?: (
    id: string,
    event: ReactDragEvent<HTMLButtonElement>,
  ) => void;
  onEndPriorityDrag?: () => void;
}) {
  const eligibleIds = useMemo(
    () =>
      new Set(
        attackerDecision?.eligible.map((id) => String(id)) ??
          ([] as string[]),
      ),
    [attackerDecision],
  );
  const nonlands = [
    ...(player.creatures ?? []),
    ...(player.artifacts ?? []),
    ...(player.enchantments ?? []),
  ];

  return (
    <section
      className={`battlefield-side ${opponent ? "opponent-side" : "player-side"} ${
        active ? "is-active" : ""
      } ${priorityPlayTarget ? "is-play-destination" : ""} ${
        priorityPlayTarget && draggingPriority ? "is-drop-target" : ""
      }`}
      aria-label={`${playerLabel(snapshot, seat)} battlefield`}
      data-priority-play-target={priorityPlayTarget || undefined}
      onClick={(event) => {
        if (!priorityPlayTarget) return;
        const target = event.target as HTMLElement;
        if (target.closest("button")) return;
        if (target.closest(".permanent-zone")) onChoosePriorityPlay?.();
      }}
      onDragOver={(event) => {
        if (!priorityPlayTarget || !draggingPriority) return;
        event.preventDefault();
        event.dataTransfer.dropEffect = "move";
      }}
      onDrop={(event) => {
        if (!priorityPlayTarget || !draggingPriority) return;
        event.preventDefault();
        event.stopPropagation();
        onEndPriorityDrag?.();
        onChoosePriorityPlay?.();
      }}
    >
      <PlayerHud
        player={player}
        label={playerLabel(snapshot, seat)}
        deck={deckLabel(snapshot, meta, seat)}
        policy={policyLabel(snapshot, meta, seat)}
        active={active}
        opponent={opponent}
        priorityTarget={priorityPlayerTargets?.has(seat)}
        draggingPriority={draggingPriority}
        onChoosePriorityTarget={() => onChoosePriorityPlayer?.(seat)}
        onDropPriorityTarget={() => {
          onEndPriorityDrag?.();
          onChoosePriorityPlayer?.(seat);
        }}
      />
      {opponent && (
        <div className="opponent-hand-space">
          <HiddenHand count={player.handSize} />
          {(player.revealedHand ?? player.hand)?.length ? (
            <FaceUpHand
              cards={(player.revealedHand ?? player.hand) as Card[]}
              label="Opponent hand"
              debug
            />
          ) : null}
        </div>
      )}
      <div className="permanent-zone">
        <PermanentRow
          title="Permanents"
          permanents={nonlands}
          eligibleIds={eligibleIds}
          selectedIds={selectedAttackers}
          targetedIds={targetedPermanentIds}
          priorityDestinationIds={priorityDestinationIds}
          priorityOriginIds={priorityOriginIds}
          selectedPriorityOriginId={selectedPriorityOriginId}
          draggingPriority={draggingPriority}
          onToggle={onToggleAttacker}
          onChoosePriorityDestination={onChoosePriorityDestination}
          onSelectPriorityOrigin={onSelectPriorityOrigin}
          onStartPriorityOriginDrag={onStartPriorityOriginDrag}
          onEndPriorityDrag={onEndPriorityDrag}
        />
        <PermanentRow
          title="Lands"
          permanents={player.lands ?? []}
          targetedIds={targetedPermanentIds}
          priorityDestinationIds={priorityDestinationIds}
          priorityOriginIds={priorityOriginIds}
          selectedPriorityOriginId={selectedPriorityOriginId}
          draggingPriority={draggingPriority}
          onChoosePriorityDestination={onChoosePriorityDestination}
          onSelectPriorityOrigin={onSelectPriorityOrigin}
          onStartPriorityOriginDrag={onStartPriorityOriginDrag}
          onEndPriorityDrag={onEndPriorityDrag}
        />
        {nonlands.length === 0 && (player.lands ?? []).length === 0 && (
          <span className="open-ground">Open battlefield</span>
        )}
      </div>
    </section>
  );
}

function PhaseRibbon({
  phase,
  turn,
  activeLabel,
  hasPriority,
}: {
  phase: string;
  turn: number;
  activeLabel: string;
  hasPriority: boolean;
}) {
  const activePhase = phaseIndex(phase);
  return (
    <div className="phase-ribbon">
      <div className="turn-marker">
        <span>TURN</span>
        <strong>{turn}</strong>
      </div>
      <ol className="phase-steps" aria-label={`Current phase: ${phase}`}>
        {PHASES.map((item, index) => (
          <li className={index === activePhase ? "current" : ""} key={item}>
            <span>{item}</span>
          </li>
        ))}
      </ol>
      <div className={`priority-marker ${hasPriority ? "has-priority" : ""}`}>
        <span>{hasPriority ? "YOUR PRIORITY" : `${activeLabel}'S TURN`}</span>
        <strong>{formatPhase(phase)}</strong>
      </div>
    </div>
  );
}

function formatLogEntry(entry: string | LogEntry): {
  message: string;
  turn?: number;
  player?: number;
  kind?: string;
} {
  if (typeof entry === "string") return { message: entry };
  return {
    message: entry.message ?? entry.text ?? entry.label ?? JSON.stringify(entry),
    turn: entry.turn,
    player: entry.player,
    kind: entry.kind,
  };
}

function MatchLog({ entries }: { entries: Array<string | LogEntry> }) {
  const endRef = useRef<HTMLDivElement>(null);
  useEffect(() => {
    endRef.current?.scrollIntoView({ block: "nearest" });
  }, [entries.length]);
  return (
    <aside className="match-log">
      <div className="panel-heading">
        <span className="eyebrow">MATCH</span>
        <h2>Chronicle</h2>
        <span className="event-count">{entries.length}</span>
      </div>
      <div className="event-list" role="log" aria-live="polite">
        {entries.length === 0 && (
          <div className="log-empty">
            <span className="log-sigil">◌</span>
            The first draw is moments away.
          </div>
        )}
        {entries.map((raw, index) => {
          const entry = formatLogEntry(raw);
          return (
            <div
              className={`event event-${entry.kind ?? "game"} ${
                entry.player === 0 ? "event-you" : ""
              }`}
              key={`${entry.turn ?? "x"}-${index}-${entry.message}`}
            >
              <span className="event-index">{String(index + 1).padStart(2, "0")}</span>
              <div>
                {entry.turn !== undefined && (
                  <span className="event-turn">Turn {entry.turn}</span>
                )}
                <p>{entry.message}</p>
              </div>
            </div>
          );
        })}
        <div ref={endRef} />
      </div>
    </aside>
  );
}

function StackRail({
  stack,
  priorityStackTargetIds,
  draggingPriority,
  onChoosePriorityStack,
  onEndPriorityDrag,
}: {
  stack: StackEntry[];
  priorityStackTargetIds?: ReadonlySet<string>;
  draggingPriority?: boolean;
  onChoosePriorityStack?: (id: string) => void;
  onEndPriorityDrag?: () => void;
}) {
  if (stack.length === 0) return null;
  return (
    <aside className="stack-rail" aria-label="The stack" aria-live="polite">
      <div className="panel-heading">
        <span className="eyebrow">RESOLVING</span>
        <h2>The Stack</h2>
        <span className="event-count">{stack.length}</span>
      </div>
      <div className="stack-list">
        {[...stack].reverse().map((entry, index) => {
          const label = formatStackEntryLabel(entry);
          const targets = formatStackTargets(entry);
          const stackId = entry.stackId ?? entry.id;
          const priorityTarget =
            stackId !== undefined &&
            priorityStackTargetIds?.has(String(stackId));
          return (
            <div
              className={`stack-entry ${
                priorityTarget ? "is-priority-destination" : ""
              }`}
              key={stackId ?? `${label}-${index}`}
            >
              <span className="stack-order">
                {index === 0 ? "NEXT" : `+${index}`}
              </span>
              {entry.card ? (
                <CardFace card={entry.card} compact />
              ) : (
                <div className="ability-card">
                  <span>ABILITY</span>
                  <strong>{label}</strong>
                </div>
              )}
              {entry.card && <strong>{label}</strong>}
              {targets.length ? (
                <span className="stack-target">
                  {targets.length === 1 ? "Target" : "Targets"} →{" "}
                  <strong>{targets.join(", ")}</strong>
                </span>
              ) : null}
              {priorityTarget && (
                <button
                  className={`surface-action stack-surface-action ${
                    draggingPriority ? "is-drop-target" : ""
                  }`}
                  type="button"
                  onClick={() => onChoosePriorityStack?.(String(stackId))}
                  onDragOver={(event) => {
                    if (!draggingPriority) return;
                    event.preventDefault();
                    event.dataTransfer.dropEffect = "move";
                  }}
                  onDrop={(event) => {
                    if (!draggingPriority) return;
                    event.preventDefault();
                    event.stopPropagation();
                    onEndPriorityDrag?.();
                    onChoosePriorityStack?.(String(stackId));
                  }}
                >
                  {draggingPriority ? "DROP ON SPELL" : "CHOOSE SPELL"}
                </button>
              )}
            </div>
          );
        })}
      </div>
    </aside>
  );
}

function priorityOptionDetail(option: PriorityOption): string {
  const target =
    formatTargetLabel(option.target) ??
    (option.spellTarget !== undefined
      ? `Stack #${option.spellTarget}`
      : null);
  return target ? `Target → ${target}` : option.kind.replaceAll("_", " ");
}

function PriorityControls({
  decision,
  stackInteraction,
  onSubmit,
  busy,
  pendingOptions,
  onChooseExact,
}: {
  decision: PriorityDecision;
  stackInteraction: StackInteraction | null;
  onSubmit: (action: ActionRequest) => void;
  busy: boolean;
  pendingOptions: PriorityOption[];
  onChooseExact: (option: PriorityOption) => void;
}) {
  const passOptions = decision.options.filter(
    (option) => option.kind === "pass",
  );
  return (
    <div
      className={`priority-control arena-priority ${
        stackInteraction ? "has-stack-context" : ""
      }`}
    >
      {stackInteraction && (
        <div className="stack-choice-context">
          <span>NEXT ON STACK</span>
          <strong>{stackInteraction.label}</strong>
          {stackInteraction.targets.length > 0 && (
            <small>
              Target → {stackInteraction.targets.join(", ")}
            </small>
          )}
        </div>
      )}
      <div className="arena-priority-actions">
        {pendingOptions.length > 1 && (
          <div className="priority-parameter-chooser">
            <span>CHOOSE EXACT VERSION</span>
            {pendingOptions.map((option) => (
              <button
                id={priorityOptionElementId(decision.decisionId, option.index)}
                type="button"
                title={option.label}
                key={`${option.index}-${option.label}`}
                onClick={() => onChooseExact(option)}
                disabled={busy}
              >
                <strong>{concisePriorityOptionLabel(option)}</strong>
                <small>{priorityOptionDetail(option)}</small>
              </button>
            ))}
          </div>
        )}

        <div className="pass-priority-controls">
          {passOptions.map((option) => (
            <button
              id={priorityOptionElementId(decision.decisionId, option.index)}
              type="button"
              key={`${option.index}-${option.label}`}
              onClick={() =>
                onSubmit({
                  decisionId: decision.decisionId,
                  index: option.index,
                })
              }
              disabled={busy}
            >
              {stackInteraction ? "Pass toward resolution" : "Pass priority"}
            </button>
          ))}
        </div>
      </div>
    </div>
  );
}

function AttackersControls({
  decision,
  player,
  selected,
  setSelected,
  onSubmit,
  busy,
}: {
  decision: AttackersDecision;
  player: PlayerState;
  selected: Set<string>;
  setSelected: (next: Set<string>) => void;
  onSubmit: (action: ActionRequest) => void;
  busy: boolean;
}) {
  if (decision.eligible.length === 0) {
    return (
      <div className="decision-actions">
        <button
          type="button"
          className="button-primary"
          onClick={() =>
            onSubmit({ decisionId: decision.decisionId, ids: [] })
          }
          disabled={busy}
        >
          Continue — no attackers
        </button>
      </div>
    );
  }

  const toggle = (id: string) => {
    const next = new Set(selected);
    if (next.has(id)) next.delete(id);
    else next.add(id);
    setSelected(next);
  };
  return (
    <div className="combat-control">
      <div className="combat-picks" aria-label="Eligible attackers">
        {decision.eligible.map((id) => {
          const permanent = findPermanent(player, id);
          return (
            <button
              type="button"
              className={selected.has(String(id)) ? "is-selected" : ""}
              onClick={() => toggle(String(id))}
              key={String(id)}
            >
              <span className="pick-dot" />
              {cardFromPermanent(permanent)?.name ?? `Permanent ${id}`}
            </button>
          );
        })}
      </div>
      <div className="decision-actions">
        <button
          type="button"
          className="button-secondary"
          onClick={() =>
            onSubmit({ decisionId: decision.decisionId, ids: [] })
          }
          disabled={busy}
        >
          No attacks
        </button>
        <button
          type="button"
          className="button-primary"
          onClick={() =>
            onSubmit({
              decisionId: decision.decisionId,
              ids: restoreOpaqueIds(selected, decision.eligible),
            })
          }
          disabled={busy || selected.size === 0}
        >
          {selected.size === 0
            ? "Select attackers"
            : `Attack with ${selected.size}`}
        </button>
      </div>
    </div>
  );
}

function BlockersControls({
  decision,
  player,
  opponent,
  assignments,
  setAssignments,
  onSubmit,
  busy,
}: {
  decision: BlockersDecision;
  player: PlayerState;
  opponent: PlayerState;
  assignments: Record<string, string>;
  setAssignments: (next: Record<string, string>) => void;
  onSubmit: (action: ActionRequest) => void;
  busy: boolean;
}) {
  return (
    <div className="blocker-control">
      <div className="blocker-grid">
        {decision.choices.map((choice) => {
          const blocker = findPermanent(player, choice.blocker);
          const blockerKey = String(choice.blocker);
          return (
            <label key={blockerKey}>
              <span>
                <strong>
                  {cardFromPermanent(blocker)?.name ?? `Blocker ${choice.blocker}`}
                </strong>
                blocks
              </span>
              <select
                value={assignments[blockerKey] ?? ""}
                onChange={(event) =>
                  setAssignments({
                    ...assignments,
                    [blockerKey]: event.target.value,
                  })
                }
              >
                <option value="">No one</option>
                {choice.legalAttackers.map((attacker) => {
                  const permanent = findPermanent(opponent, attacker);
                  return (
                    <option value={String(attacker)} key={String(attacker)}>
                      {cardFromPermanent(permanent)?.name ?? `Attacker ${attacker}`}
                    </option>
                  );
                })}
              </select>
            </label>
          );
        })}
      </div>
      <button
        type="button"
        className="button-primary"
        onClick={() => {
          const pairs = blockerPairsFromKeys(assignments, decision.choices);
          onSubmit({ decisionId: decision.decisionId, pairs });
        }}
        disabled={busy}
      >
        Confirm blocks
      </button>
    </div>
  );
}

function DamageOrderControls({
  decision,
  opponent,
  order,
  setOrder,
  onSubmit,
  busy,
}: {
  decision: DamageOrderDecision;
  opponent: PlayerState;
  order: Array<string | number>;
  setOrder: (next: Array<string | number>) => void;
  onSubmit: (action: ActionRequest) => void;
  busy: boolean;
}) {
  const move = (index: number, offset: number) => {
    const nextIndex = index + offset;
    if (nextIndex < 0 || nextIndex >= order.length) return;
    const next = [...order];
    [next[index], next[nextIndex]] = [next[nextIndex], next[index]];
    setOrder(next);
  };
  return (
    <div className="damage-order-control">
      <div className="damage-order-list">
        {order.map((id, index) => {
          const permanent = findPermanent(opponent, id);
          return (
            <div key={id}>
              <span className="order-number">{index + 1}</span>
              <strong>{cardFromPermanent(permanent)?.name ?? `Blocker ${id}`}</strong>
              <span className="reorder-buttons">
                <button
                  type="button"
                  aria-label="Move earlier"
                  onClick={() => move(index, -1)}
                  disabled={index === 0}
                >
                  ←
                </button>
                <button
                  type="button"
                  aria-label="Move later"
                  onClick={() => move(index, 1)}
                  disabled={index === order.length - 1}
                >
                  →
                </button>
              </span>
            </div>
          );
        })}
      </div>
      <button
        type="button"
        className="button-primary"
        onClick={() =>
          onSubmit({ decisionId: decision.decisionId, ids: order })
        }
        disabled={busy}
      >
        Confirm damage order
      </button>
    </div>
  );
}

function DecisionDock({
  decision,
  state,
  busy,
  selectedAttackers,
  setSelectedAttackers,
  onSubmit,
  pendingPriorityOptions,
  onChoosePriorityExact,
}: {
  decision: Decision;
  state: NonNullable<GameSnapshot["state"]>;
  busy: boolean;
  selectedAttackers: Set<string>;
  setSelectedAttackers: (next: Set<string>) => void;
  onSubmit: (action: ActionRequest) => void;
  pendingPriorityOptions: PriorityOption[];
  onChoosePriorityExact: (option: PriorityOption) => void;
}) {
  const [assignments, setAssignments] = useState<Record<string, string>>({});
  const [damageOrder, setDamageOrder] = useState<Array<string | number>>([]);

  useEffect(() => {
    setAssignments({});
    setSelectedAttackers(new Set());
    setDamageOrder(
      decision.kind === "damage_order"
        ? [...decision.blockers]
        : [],
    );
  }, [decision.decisionId, decision.kind, setSelectedAttackers]);

  const humanSeat: PlayerIndex = 0;
  const player = state.players[humanSeat];
  const opponent = state.players[(humanSeat === 0 ? 1 : 0) as PlayerIndex];
  const stackInteraction =
    decision.kind === "priority" ? describeTopOfStack(state.stack ?? []) : null;

  let heading = "Make a game choice";
  let helper = "The engine is waiting for your exact selection.";
  if (decision.kind === "priority") {
    heading = stackInteraction ? "Respond to the stack" : "Choose your play";
    helper = stackInteraction
      ? `${stackInteraction.summary} Cast a response, or pass priority to continue.`
      : "Select a playable card or permanent, then choose its highlighted destination.";
  } else if (decision.kind === "attackers") {
    heading =
      decision.eligible.length === 0
        ? "Continue combat"
        : "Declare attackers";
    helper =
      decision.eligible.length === 0
        ? "Automatic advance was interrupted. Continue without attackers."
        : "Select any creatures you want to send into combat.";
  } else if (decision.kind === "blockers") {
    heading = "Declare blockers";
    helper = "Each blocker can intercept one legal attacker.";
  } else if (decision.kind === "damage_order") {
    heading = "Order combat damage";
    helper = "Damage is assigned from left to right.";
  }

  return (
    <section className="decision-dock" aria-live="polite">
      <div className="decision-heading">
        <span className="decision-pulse" />
        <div>
          <span className="eyebrow">YOUR DECISION</span>
          <h2>{heading}</h2>
          <p>{helper}</p>
        </div>
      </div>
      <div className="decision-body">
        {decision.kind === "priority" && (
          <PriorityControls
            decision={decision}
            stackInteraction={stackInteraction}
            onSubmit={onSubmit}
            busy={busy}
            pendingOptions={pendingPriorityOptions}
            onChooseExact={onChoosePriorityExact}
          />
        )}
        {decision.kind === "attackers" && (
          <AttackersControls
            decision={decision}
            player={player}
            selected={selectedAttackers}
            setSelected={setSelectedAttackers}
            onSubmit={onSubmit}
            busy={busy}
          />
        )}
        {decision.kind === "blockers" && (
          <BlockersControls
            decision={decision}
            player={player}
            opponent={opponent}
            assignments={assignments}
            setAssignments={setAssignments}
            onSubmit={onSubmit}
            busy={busy}
          />
        )}
        {decision.kind === "damage_order" && (
          <DamageOrderControls
            decision={decision}
            opponent={opponent}
            order={damageOrder}
            setOrder={setDamageOrder}
            onSubmit={onSubmit}
            busy={busy}
          />
        )}
      </div>
      {busy && (
        <div className="decision-busy">
          <span className="spinner" />
          Playing out the response…
        </div>
      )}
    </section>
  );
}

function DeckManifest({ deck }: { deck?: DeckMeta }) {
  if (!deck) return null;
  return (
    <div className="deck-manifest">
      <div>
        <span className="eyebrow">DECK LIST</span>
        <strong>{deck.name}</strong>
      </div>
      <ul>
        {deck.cards.map((entry, index) => {
          const name = typeof entry === "string" ? entry : entry.name;
          const count = typeof entry === "string" ? undefined : entry.count;
          return (
            <li key={`${name}-${index}`}>
              <span>{name}</span>
              {count !== undefined && <strong>×{count}</strong>}
            </li>
          );
        })}
      </ul>
    </div>
  );
}

function SetupDrawer({
  open,
  meta,
  loadingMeta,
  metaError,
  initialConfig,
  creating,
  onClose,
  onRetryMeta,
  onCreate,
}: {
  open: boolean;
  meta: MetaResponse | null;
  loadingMeta: boolean;
  metaError: string | null;
  initialConfig: GameConfig | null;
  creating: boolean;
  onClose: () => void;
  onRetryMeta: () => void;
  onCreate: (config: GameConfig) => void;
}) {
  const [config, setConfig] = useState<GameConfig | null>(initialConfig);

  useEffect(() => {
    if (initialConfig) setConfig(initialConfig);
  }, [initialConfig]);

  if (!open) return null;

  const botPolicies = meta?.policies.length
    ? meta.policies
    : FALLBACK_POLICIES;
  const decks = meta?.decks ?? [];
  const updateSeat = (
    seat: PlayerIndex,
    field: "deckId" | "policyId",
    value: string,
  ) => {
    if (!config) return;
    const players = [...config.players] as GameConfig["players"];
    players[seat] = { ...players[seat], [field]: value };
    setConfig({ ...config, players });
  };
  const submit = (event: FormEvent) => {
    event.preventDefault();
    if (config) onCreate(config);
  };

  return (
    <div className="drawer-layer" role="dialog" aria-modal="true" aria-label="New match">
      <button
        className="drawer-scrim"
        type="button"
        onClick={onClose}
        aria-label="Close new match setup"
      />
      <aside className="setup-drawer">
        <header>
          <div>
            <span className="eyebrow">MATCH ROOM</span>
            <h1>Set the table</h1>
            <p>
              Choose both decks and pilots. Every seed replays the same match.
            </p>
          </div>
          <button className="icon-button" type="button" onClick={onClose}>
            <span aria-hidden="true">×</span>
            <span className="sr-only">Close</span>
          </button>
        </header>

        {loadingMeta && (
          <div className="drawer-loading">
            <span className="spinner" />
            Reading decks and pilots…
          </div>
        )}
        {metaError && (
          <div className="drawer-error">
            <strong>Couldn’t reach the game engine.</strong>
            <p>{metaError}</p>
            <button type="button" onClick={onRetryMeta}>
              Try again
            </button>
          </div>
        )}
        {config && decks.length > 0 && (
          <form onSubmit={submit}>
            <div className="seat-setups">
              {([0, 1] as PlayerIndex[]).map((seat) => {
                const policies = seat === 0 ? [HUMAN_POLICY] : botPolicies;
                const deck = decks.find(
                  (item) => item.id === config.players[seat].deckId,
                );
                const policy = policies.find(
                  (item) => item.id === config.players[seat].policyId,
                );
                return (
                  <section className={`seat-setup seat-${seat}`} key={seat}>
                    <div className="seat-title">
                      <span>{seat === 0 ? "01" : "02"}</span>
                      <div>
                        <strong>{seat === 0 ? "Near seat" : "Far seat"}</strong>
                        <small>{seat === 0 ? "Your side" : "Across the table"}</small>
                      </div>
                    </div>
                    <label>
                      <span>Deck</span>
                      <select
                        value={config.players[seat].deckId}
                        onChange={(event) =>
                          updateSeat(seat, "deckId", event.target.value)
                        }
                      >
                        {decks.map((item) => (
                          <option value={item.id} key={item.id}>
                            {item.name}
                          </option>
                        ))}
                      </select>
                    </label>
                    <label>
                      <span>Pilot</span>
                      <select
                        value={config.players[seat].policyId}
                        onChange={(event) =>
                          updateSeat(seat, "policyId", event.target.value)
                        }
                      >
                        {policies.map((item) => (
                          <option value={item.id} key={`${seat}-${item.id}`}>
                            {item.name}
                          </option>
                        ))}
                      </select>
                    </label>
                    <p className="policy-description">
                      {policy?.description ?? "A legal Old School pilot."}
                    </p>
                    <DeckManifest deck={deck} />
                  </section>
                );
              })}
            </div>

            <section className="simulation-settings">
              <div className="settings-title">
                <span className="eyebrow">DETERMINISTIC SETUP</span>
                <strong>Simulation controls</strong>
              </div>
              <label>
                <span>Game seed</span>
                <div className="seed-field">
                  <input
                    type="number"
                    min="1"
                    max="4294967295"
                    value={config.seed}
                    onChange={(event) =>
                      setConfig({ ...config, seed: Number(event.target.value) })
                    }
                  />
                  <button
                    type="button"
                    onClick={() => setConfig({ ...config, seed: randomSeed() })}
                    title="Generate a random seed"
                  >
                    Shuffle
                  </button>
                </div>
              </label>
              <label>
                <span>Learned train games</span>
                <input
                  type="number"
                  min="1"
                  max="100000"
                  value={config.trainGames}
                  onChange={(event) =>
                    setConfig({
                      ...config,
                      trainGames: Number(event.target.value),
                    })
                  }
                />
              </label>
              <label>
                <span>Learned train seed</span>
                <input
                  type="number"
                  min="1"
                  max="4294967295"
                  value={config.trainSeed}
                  onChange={(event) =>
                    setConfig({
                      ...config,
                      trainSeed: Number(event.target.value),
                    })
                  }
                />
              </label>
              <label className="toggle-field">
                <input
                  type="checkbox"
                  checked={config.debugReveal}
                  onChange={(event) =>
                    setConfig({
                      ...config,
                      debugReveal: event.target.checked,
                    })
                  }
                />
                <span className="toggle-track">
                  <span />
                </span>
                <span>
                  <strong>Reveal opponent hand</strong>
                  <small>Debug display only — never given to the bot</small>
                </span>
              </label>
            </section>

            <footer>
              <p>
                Human pilots pause at priority, attackers, blockers, and damage
                ordering.
              </p>
              <button
                type="submit"
                className="button-primary launch-button"
                disabled={creating}
              >
                {creating ? (
                  <>
                    <span className="spinner" />
                    Preparing match…
                  </>
                ) : (
                  <>
                    Start match
                    <span aria-hidden="true">→</span>
                  </>
                )}
              </button>
            </footer>
          </form>
        )}
      </aside>
    </div>
  );
}

function WelcomeTable({
  loading,
  error,
  deckCount,
  policyCount,
  onNewGame,
  onRetry,
}: {
  loading: boolean;
  error: string | null;
  deckCount: number;
  policyCount: number;
  onNewGame: () => void;
  onRetry: () => void;
}) {
  return (
    <main className="welcome-table">
      <div className="welcome-rings" aria-hidden="true">
        <span />
        <span />
        <span />
      </div>
      <section>
        <span className="eyebrow">FAST · DETERMINISTIC · OLD SCHOOL</span>
        <h1>
          Every choice,
          <br />
          on the table.
        </h1>
        <p>
          Pilot {deckCount} classic archetypes against {policyCount} bot
          policies: Random, Monte Carlo, Deep Monte Carlo, HandcodedPolicy,
          Learned Value, and Learned Actor.
        </p>
        {error ? (
          <div className="welcome-error">
            <strong>The table isn’t ready.</strong>
            <span>{error}</span>
            <button type="button" onClick={onRetry}>
              Reconnect
            </button>
          </div>
        ) : (
          <button
            className="button-primary welcome-button"
            type="button"
            onClick={onNewGame}
            disabled={loading}
          >
            {loading ? "Reading the metagame…" : "Choose your matchup"}
            {!loading && <span aria-hidden="true">→</span>}
          </button>
        )}
        <div className="welcome-details">
          <span>
            <strong>{deckCount}</strong> decks
          </span>
          <span>
            <strong>{policyCount}</strong> bot policies
          </span>
          <span>
            <strong>1</strong> exact seed
          </span>
        </div>
      </section>
    </main>
  );
}

function GameOver({
  snapshot,
  meta,
  busy,
  onRematch,
  onNewGame,
}: {
  snapshot: GameSnapshot;
  meta: MetaResponse | null;
  busy: boolean;
  onRematch: () => void;
  onNewGame: () => void;
}) {
  if (snapshot.status !== "finished" && snapshot.status !== "error") return null;
  const winner = snapshot.result?.winner;
  const winnerSeat =
    winner === 0 || winner === 1 ? (winner as PlayerIndex) : undefined;
  const config = snapshot.config as GameConfig | undefined;
  const configuredHumanSeat = config?.players.findIndex(
    ({ policyId }) => policyId === "human",
  );
  const humanSeat =
    configuredHumanSeat === 0 || configuredHumanSeat === 1
      ? configuredHumanSeat
      : 0;
  const title = formatGameResultTitle(snapshot.status, winner, humanSeat);
  const reason = formatGameResultReason(snapshot.result?.reason);
  return (
    <div className="game-over-layer" role="dialog" aria-modal="true">
      <section>
        <span className="result-sigil" aria-hidden="true">
          {snapshot.status === "error" ? "!" : "✦"}
        </span>
        <span className="eyebrow">
          {snapshot.status === "error" ? "ENGINE MESSAGE" : "MATCH COMPLETE"}
        </span>
        <h1>{title}</h1>
        <p>
          {snapshot.error ??
            reason ??
            (winnerSeat !== undefined
              ? `${playerLabel(snapshot, winnerSeat)} piloted ${deckLabel(
                  snapshot,
                  meta,
                  winnerSeat,
                )} to the win.`
              : "Neither player was left standing.")}
        </p>
        <div className="result-stats">
          <span>
            <strong>{snapshot.state?.turnNumber ?? snapshot.result?.turns ?? "—"}</strong>
            turns
          </span>
          <span>
            <strong>{snapshot.log?.length ?? 0}</strong>
            events
          </span>
        </div>
        <div className="result-actions">
          <button
            type="button"
            className="button-secondary"
            onClick={onNewGame}
            disabled={busy}
          >
            Change matchup
          </button>
          <button
            type="button"
            className="button-primary"
            onClick={onRematch}
            disabled={busy}
          >
            {busy ? (
              <>
                <span className="spinner" />
                Preparing rematch…
              </>
            ) : (
              "Replay seed"
            )}
          </button>
        </div>
      </section>
    </div>
  );
}

type PriorityOriginSelection =
  | {
      kind: "hand";
      cardId: string;
      renderKey: string;
      label: string;
    }
  | {
      kind: "permanent";
      permanentId: string;
      label: string;
    };

export default function App() {
  const [meta, setMeta] = useState<MetaResponse | null>(null);
  const [snapshot, setSnapshot] = useState<GameSnapshot | null>(null);
  const [setupOpen, setSetupOpen] = useState(true);
  const [loadingMeta, setLoadingMeta] = useState(true);
  const [creating, setCreating] = useState(false);
  const [acting, setActing] = useState(false);
  const [metaError, setMetaError] = useState<string | null>(null);
  const [gameError, setGameError] = useState<string | null>(null);
  const [selectedPriorityOrigin, setSelectedPriorityOrigin] =
    useState<PriorityOriginSelection | null>(null);
  const [draggedPriorityOrigin, setDraggedPriorityOrigin] =
    useState<PriorityOriginSelection | null>(null);
  const [pendingPriorityOptions, setPendingPriorityOptions] = useState<
    PriorityOption[]
  >([]);
  const [selectedAttackers, setSelectedAttackers] = useState<Set<string>>(
    new Set(),
  );
  const [autoAdvanceFailedDecision, setAutoAdvanceFailedDecision] = useState<
    string | null
  >(null);
  const autoSubmittedAttackerDecision = useRef<string | null>(null);
  const creatingRef = useRef(false);
  const actingRef = useRef(false);

  const loadMeta = useCallback(() => {
    const controller = new AbortController();
    setLoadingMeta(true);
    setMetaError(null);
    fetchMeta(controller.signal)
      .then((response) => {
        setMeta(response);
        setLoadingMeta(false);
      })
      .catch((error: unknown) => {
        if (controller.signal.aborted) return;
        setMetaError(error instanceof Error ? error.message : String(error));
        setLoadingMeta(false);
      });
    return () => controller.abort();
  }, []);

  useEffect(() => loadMeta(), [loadMeta]);

  const initialConfig = useMemo<GameConfig | null>(() => {
    if (!meta?.decks.length) return null;
    const defaults = meta.defaults;
    const firstDeck = meta.decks[0].id;
    const secondDeck = meta.decks[1]?.id ?? firstDeck;
    const defaultOpponentPolicy =
      meta.policies.find((policy) => policy.id === "learned-value")?.id ??
      meta.policies[0]?.id ??
      "learned-value";
    const defaultPlayers = readDefault<
      Array<{ deckId?: string; policyId?: string }>
    >(defaults, ["players"], []);
    return {
      seed: Number(readDefault(defaults, ["seed", "gameSeed"], 42)),
      trainGames: Number(
        readDefault(defaults, ["trainGames", "learnedTrainGames"], 800),
      ),
      trainSeed: Number(
        readDefault(defaults, ["trainSeed", "learnedTrainSeed"], 424242),
      ),
      debugReveal: Boolean(
        readDefault(defaults, ["debugReveal", "revealHands"], false),
      ),
      players: [
        {
          deckId:
            defaultPlayers[0]?.deckId ??
            String(
              readDefault(defaults, ["playerDeck", "playerDeckId"], firstDeck),
            ),
          policyId: defaultPlayers[0]?.policyId ?? "human",
        },
        {
          deckId:
            defaultPlayers[1]?.deckId ??
            String(
              readDefault(
                defaults,
                ["opponentDeck", "opponentDeckId"],
                secondDeck,
              ),
            ),
          policyId:
            defaultPlayers[1]?.policyId ??
            String(
              readDefault(
                defaults,
                ["opponentPolicy", "opponentPolicyId"],
                defaultOpponentPolicy,
              ),
            ),
        },
      ],
    };
  }, [meta]);

  const currentConfig =
    (snapshot?.config as GameConfig | undefined) ?? initialConfig;

  const startGame = useCallback(
    (config: GameConfig) => {
      if (creatingRef.current) return;
      creatingRef.current = true;
      const previousGameId = snapshot?.id;
      setCreating(true);
      setGameError(null);
      createGame(config)
        .then((next) => {
          setSnapshot(next);
          setSetupOpen(false);
          if (previousGameId && previousGameId !== next.id) {
            void deleteGame(previousGameId).catch(() => {
              // The server also cleans every child up on shutdown.
            });
          }
        })
        .catch((error: unknown) => {
          setGameError(error instanceof Error ? error.message : String(error));
        })
        .finally(() => {
          creatingRef.current = false;
          setCreating(false);
        });
    },
    [snapshot?.id],
  );

  useEffect(() => {
    const gameId = snapshot?.id;
    if (!gameId) return;
    const abandonOnPageExit = () => {
      void fetch(`/api/games/${encodeURIComponent(gameId)}`, {
        method: "DELETE",
        keepalive: true,
      });
    };
    window.addEventListener("pagehide", abandonOnPageExit);
    return () => {
      window.removeEventListener("pagehide", abandonOnPageExit);
    };
  }, [snapshot?.id]);

  const act = useCallback(
    (action: ActionRequest, onError?: () => void) => {
      if (!snapshot || actingRef.current) return;
      actingRef.current = true;
      const gameId = snapshot.id;
      setActing(true);
      setGameError(null);
      submitAction(gameId, action)
        .then((next) => {
          setSnapshot(next);
        })
        .catch(async (error: unknown) => {
          onError?.();
          if (
            error instanceof ApiRequestError &&
            ["stale_decision", "not_awaiting_action", "game_over"].includes(
              error.code,
            )
          ) {
            try {
              setSnapshot(await fetchGame(gameId));
              setGameError(
                `${error.message}. The current game state was refreshed.`,
              );
            } catch (refreshError: unknown) {
              const refreshMessage =
                refreshError instanceof Error
                  ? refreshError.message
                  : String(refreshError);
              setGameError(`${error.message}. Refresh failed: ${refreshMessage}`);
            }
            return;
          }
          setGameError(error instanceof Error ? error.message : String(error));
        })
        .finally(() => {
          actingRef.current = false;
          setActing(false);
        });
    },
    [snapshot],
  );

  useEffect(() => {
    const decision = snapshot?.decision;
    if (
      snapshot?.status !== "playing" ||
      decision?.kind !== "attackers" ||
      decision.eligible.length !== 0 ||
      acting
    )
      return;

    const decisionKey = `${snapshot.id}:${String(decision.decisionId)}`;
    if (autoAdvanceFailedDecision === decisionKey) return;
    if (autoSubmittedAttackerDecision.current === decisionKey) return;
    autoSubmittedAttackerDecision.current = decisionKey;
    act(
      { decisionId: decision.decisionId, ids: [] },
      () => setAutoAdvanceFailedDecision(decisionKey),
    );
  }, [act, acting, autoAdvanceFailedDecision, snapshot]);

  useEffect(() => {
    if (
      !snapshot ||
      snapshot.status !== "playing" ||
      snapshot.decision ||
      acting
    )
      return;
    const controller = new AbortController();
    const timer = window.setTimeout(() => {
      fetchGame(snapshot.id, controller.signal)
        .then(setSnapshot)
        .catch((error: unknown) => {
          if (!controller.signal.aborted)
            setGameError(error instanceof Error ? error.message : String(error));
        });
    }, 700);
    return () => {
      controller.abort();
      window.clearTimeout(timer);
    };
  }, [snapshot, acting]);

  useEffect(() => {
    const keydown = (event: KeyboardEvent) => {
      if (event.key === "Escape" && selectedPriorityOrigin) {
        event.preventDefault();
        setSelectedPriorityOrigin(null);
        setPendingPriorityOptions([]);
        return;
      }
      if (event.key === "Escape" && setupOpen && snapshot) setSetupOpen(false);
    };
    window.addEventListener("keydown", keydown);
    return () => window.removeEventListener("keydown", keydown);
  }, [selectedPriorityOrigin, setupOpen, snapshot]);

  useEffect(() => {
    setSelectedPriorityOrigin(null);
    setDraggedPriorityOrigin(null);
    setPendingPriorityOptions([]);
  }, [snapshot?.id, snapshot?.decision?.decisionId]);

  const replay = () => {
    if (currentConfig) startGame(currentConfig);
  };

  if (!snapshot) {
    return (
      <div className="app-shell">
        <TopBar
          inGame={false}
          onNewGame={() => setSetupOpen(true)}
          seed={initialConfig?.seed}
        />
        <WelcomeTable
          loading={loadingMeta}
          error={metaError}
          deckCount={meta?.decks.length || 5}
          policyCount={meta?.policies.length || FALLBACK_POLICIES.length}
          onNewGame={() => setSetupOpen(true)}
          onRetry={loadMeta}
        />
        <SetupDrawer
          open={setupOpen}
          meta={meta}
          loadingMeta={loadingMeta}
          metaError={metaError}
          initialConfig={initialConfig}
          creating={creating}
          onClose={() => setSetupOpen(false)}
          onRetryMeta={loadMeta}
          onCreate={startGame}
        />
        {(gameError || creating) && (
          <div className={`toast ${gameError ? "toast-error" : ""}`}>
            {creating && <span className="spinner" />}
            {gameError ?? "Shuffling and training the selected pilot…"}
          </div>
        )}
      </div>
    );
  }

  const state = snapshot.state;
  const stack = state?.stack ?? [];
  const targetedPermanentIds = new Set(stackPermanentTargetIds(stack));
  const humanSeat =
    (currentConfig?.players.findIndex((seat) => seat.policyId === "human") ??
      0) as PlayerIndex;
  const nearSeat: PlayerIndex =
    humanSeat === 0 || humanSeat === 1 ? humanSeat : 0;
  const farSeat: PlayerIndex = nearSeat === 0 ? 1 : 0;
  const attackerDecision =
    snapshot.decision?.kind === "attackers"
      ? snapshot.decision
      : undefined;
  const emptyAttackDecisionKey =
    attackerDecision?.eligible.length === 0
      ? `${snapshot.id}:${String(attackerDecision.decisionId)}`
      : null;
  const hasVisibleDecision =
    emptyAttackDecisionKey === null ||
    autoAdvanceFailedDecision === emptyAttackDecisionKey;
  const priorityDecision =
    snapshot.decision?.kind === "priority"
      ? snapshot.decision
      : undefined;
  const playableHandCardIds = new Set(
    priorityDecision?.options
      .flatMap((option) =>
        option.card && option.sourcePermanent === undefined
          ? [String(option.card.id)]
          : [],
      ) ??
      [],
  );
  const selectedPriorityOptions =
    selectedPriorityOrigin?.kind === "hand"
      ? priorityOptionsForCard(
          priorityDecision,
          selectedPriorityOrigin.cardId,
        )
      : selectedPriorityOrigin?.kind === "permanent"
        ? priorityOptionsForSourcePermanent(
            priorityDecision,
            selectedPriorityOrigin.permanentId,
          )
        : [];
  const priorityDestinationGroups = new Map<
    PriorityDestinationKey,
    PriorityOption[]
  >();
  for (const option of selectedPriorityOptions) {
    const destination = priorityDestinationKey(option);
    if (!destination) continue;
    const group = priorityDestinationGroups.get(destination) ?? [];
    group.push(option);
    priorityDestinationGroups.set(destination, group);
  }
  const priorityPlayOptions =
    priorityDestinationGroups.get("play") ?? [];
  const priorityPermanentDestinationIds = new Set(
    [...priorityDestinationGroups.keys()].flatMap((key) =>
      key.startsWith("permanent:") ? [key.slice("permanent:".length)] : [],
    ),
  );
  const priorityPlayerTargets = new Set(
    [...priorityDestinationGroups.keys()].flatMap((key) =>
      key.startsWith("player:")
        ? [Number(key.slice("player:".length))]
        : [],
    ),
  );
  const priorityStackTargetIds = new Set(
    [...priorityDestinationGroups.keys()].flatMap((key) =>
      key.startsWith("stack:") ? [key.slice("stack:".length)] : [],
    ),
  );
  const priorityOriginIds = new Set(
    selectedPriorityOrigin?.kind === "permanent"
      ? [selectedPriorityOrigin.permanentId]
      : selectedPriorityOrigin?.kind === "hand"
        ? []
        : (priorityDecision?.options.flatMap((option) =>
            option.sourcePermanent === undefined
              ? []
              : [String(option.sourcePermanent)],
          ) ?? []),
  );
  const clearPrioritySelection = () => {
    setSelectedPriorityOrigin(null);
    setDraggedPriorityOrigin(null);
    setPendingPriorityOptions([]);
  };
  const submitPriorityOption = (option: PriorityOption) => {
    const decision = priorityDecision;
    if (!decision) return;
    clearPrioritySelection();
    act({ decisionId: decision.decisionId, index: option.index });
  };
  const choosePriorityDestination = (destination: PriorityDestinationKey) => {
    const options = priorityDestinationGroups.get(destination) ?? [];
    if (options.length === 1) {
      submitPriorityOption(options[0]);
    } else if (options.length > 1) {
      setPendingPriorityOptions(options);
    }
  };
  const selectHandCard = (card: Card, renderKey: string) => {
    if (priorityOptionsForCard(priorityDecision, card.id).length === 0) return;
    setSelectedPriorityOrigin({
      kind: "hand",
      cardId: String(card.id),
      renderKey,
      label: card.name,
    });
    setPendingPriorityOptions([]);
  };
  const doubleClickHandCard = (card: Card, renderKey: string) => {
    const options = priorityOptionsForCard(priorityDecision, card.id);
    const playOptions = options.filter(
      (option) => priorityDestinationKey(option) === "play",
    );
    selectHandCard(card, renderKey);
    if (playOptions.length === 1) {
      submitPriorityOption(playOptions[0]);
    } else if (playOptions.length > 1) {
      setPendingPriorityOptions(playOptions);
    }
  };
  const startHandCardDrag = (
    card: Card,
    renderKey: string,
    event: ReactDragEvent<HTMLButtonElement>,
  ) => {
    if (priorityOptionsForCard(priorityDecision, card.id).length === 0) {
      event.preventDefault();
      return;
    }
    event.dataTransfer.effectAllowed = "move";
    event.dataTransfer.setData("text/plain", renderKey);
    setPendingPriorityOptions([]);
    const origin: PriorityOriginSelection = {
      kind: "hand",
      cardId: String(card.id),
      renderKey,
      label: card.name,
    };
    setSelectedPriorityOrigin(origin);
    setDraggedPriorityOrigin(origin);
  };
  const selectPriorityPermanentOrigin = (permanentId: string) => {
    const options = priorityOptionsForSourcePermanent(
      priorityDecision,
      permanentId,
    );
    if (options.length === 0) return;
    const permanent =
      findPermanent(state?.players[0], permanentId) ??
      findPermanent(state?.players[1], permanentId);
    const origin: PriorityOriginSelection = {
      kind: "permanent",
      permanentId,
      label: permanent?.card.name ?? `Permanent ${permanentId}`,
    };
    setSelectedPriorityOrigin(origin);
    setPendingPriorityOptions([]);
    const destinations = new Set(
      options.map(priorityDestinationKey).filter((key) => key !== null),
    );
    if (
      options.length === 1 &&
      destinations.size === 1 &&
      destinations.has("play")
    ) {
      submitPriorityOption(options[0]);
    }
  };
  const startPriorityPermanentDrag = (
    permanentId: string,
    event: ReactDragEvent<HTMLButtonElement>,
  ) => {
    const options = priorityOptionsForSourcePermanent(
      priorityDecision,
      permanentId,
    );
    if (options.length === 0) {
      event.preventDefault();
      return;
    }
    const permanent =
      findPermanent(state?.players[0], permanentId) ??
      findPermanent(state?.players[1], permanentId);
    const origin: PriorityOriginSelection = {
      kind: "permanent",
      permanentId,
      label: permanent?.card.name ?? `Permanent ${permanentId}`,
    };
    event.dataTransfer.effectAllowed = "move";
    event.dataTransfer.setData("text/plain", `permanent:${permanentId}`);
    setSelectedPriorityOrigin(origin);
    setDraggedPriorityOrigin(origin);
    setPendingPriorityOptions([]);
  };
  const toggleAttacker = (id: string) => {
    const next = new Set(selectedAttackers);
    if (next.has(id)) next.delete(id);
    else next.add(id);
    setSelectedAttackers(next);
  };

  return (
    <div
      className={`app-shell game-shell ${
        stack.length ? "has-stack" : ""
      } ${snapshot.decision && hasVisibleDecision ? "has-decision" : ""}`}
    >
      <TopBar
        inGame
        onNewGame={() => setSetupOpen(true)}
        seed={currentConfig?.seed}
        gameId={snapshot.id}
      />
      {state ? (
        <>
          <div className="game-layout">
            <MatchLog entries={snapshot.log ?? []} />
            <main className="table-stage">
              <PhaseRibbon
                phase={state.phase}
                turn={state.turnNumber}
                activeLabel={playerLabel(
                  snapshot,
                  state.activePlayer as PlayerIndex,
                )}
                hasPriority={Boolean(snapshot.decision && hasVisibleDecision)}
              />
              <div className="battlefield">
                <BattlefieldSide
                  player={state.players[farSeat]}
                  seat={farSeat}
                  snapshot={snapshot}
                  meta={meta}
                  active={state.activePlayer === farSeat}
                  opponent
                  targetedPermanentIds={targetedPermanentIds}
                  priorityDestinationIds={priorityPermanentDestinationIds}
                  priorityOriginIds={priorityOriginIds}
                  priorityPlayerTargets={priorityPlayerTargets}
                  selectedPriorityOriginId={
                    selectedPriorityOrigin?.kind === "permanent"
                      ? selectedPriorityOrigin.permanentId
                      : undefined
                  }
                  draggingPriority={Boolean(draggedPriorityOrigin)}
                  onChoosePriorityDestination={(id) =>
                    choosePriorityDestination(`permanent:${id}`)
                  }
                  onChoosePriorityPlayer={(seat) =>
                    choosePriorityDestination(`player:${seat}`)
                  }
                  onSelectPriorityOrigin={selectPriorityPermanentOrigin}
                  onStartPriorityOriginDrag={startPriorityPermanentDrag}
                  onEndPriorityDrag={() => setDraggedPriorityOrigin(null)}
                />
                <div className="midline">
                  <span />
                  <strong>{formatPhase(state.phase)}</strong>
                  <span />
                </div>
                <BattlefieldSide
                  player={state.players[nearSeat]}
                  seat={nearSeat}
                  snapshot={snapshot}
                  meta={meta}
                  active={state.activePlayer === nearSeat}
                  attackerDecision={
                    state.activePlayer === nearSeat ? attackerDecision : undefined
                  }
                  selectedAttackers={selectedAttackers}
                  targetedPermanentIds={targetedPermanentIds}
                  priorityDestinationIds={priorityPermanentDestinationIds}
                  priorityOriginIds={priorityOriginIds}
                  priorityPlayerTargets={priorityPlayerTargets}
                  priorityPlayTarget={priorityPlayOptions.length > 0}
                  selectedPriorityOriginId={
                    selectedPriorityOrigin?.kind === "permanent"
                      ? selectedPriorityOrigin.permanentId
                      : undefined
                  }
                  draggingPriority={Boolean(draggedPriorityOrigin)}
                  onToggleAttacker={toggleAttacker}
                  onChoosePriorityDestination={(id) =>
                    choosePriorityDestination(`permanent:${id}`)
                  }
                  onChoosePriorityPlayer={(seat) =>
                    choosePriorityDestination(`player:${seat}`)
                  }
                  onChoosePriorityPlay={() =>
                    choosePriorityDestination("play")
                  }
                  onSelectPriorityOrigin={selectPriorityPermanentOrigin}
                  onStartPriorityOriginDrag={startPriorityPermanentDrag}
                  onEndPriorityDrag={() => setDraggedPriorityOrigin(null)}
                />
              </div>
              <div
                className={`player-console ${
                  snapshot.decision && hasVisibleDecision
                    ? "has-decision"
                    : ""
                }`}
              >
                <FaceUpHand
                  cards={state.players[nearSeat].hand ?? []}
                  label={`${playerLabel(snapshot, nearSeat)} · hand`}
                  playableCardIds={playableHandCardIds}
                  selectedCardKey={
                    selectedPriorityOrigin?.kind === "hand"
                      ? selectedPriorityOrigin.renderKey
                      : undefined
                  }
                  draggedCardKey={
                    draggedPriorityOrigin?.kind === "hand"
                      ? draggedPriorityOrigin.renderKey
                      : undefined
                  }
                  onCardClick={selectHandCard}
                  onCardDoubleClick={doubleClickHandCard}
                  onCardDragStart={startHandCardDrag}
                  onCardDragEnd={() => setDraggedPriorityOrigin(null)}
                />
                {snapshot.decision && hasVisibleDecision && (
                  <DecisionDock
                    decision={snapshot.decision}
                    state={state}
                    busy={acting}
                    selectedAttackers={selectedAttackers}
                    setSelectedAttackers={setSelectedAttackers}
                    onSubmit={act}
                    pendingPriorityOptions={pendingPriorityOptions}
                    onChoosePriorityExact={submitPriorityOption}
                  />
                )}
              </div>
            </main>
            <StackRail
              stack={stack}
              priorityStackTargetIds={priorityStackTargetIds}
              draggingPriority={Boolean(draggedPriorityOrigin)}
              onChoosePriorityStack={(id) =>
                choosePriorityDestination(`stack:${id}`)
              }
              onEndPriorityDrag={() => setDraggedPriorityOrigin(null)}
            />
          </div>
          {!snapshot.decision && snapshot.status === "playing" ? (
            <div className="watching-banner">
              <span className="spinner" />
              The pilots are thinking…
            </div>
          ) : null}
        </>
      ) : (
        <div className="state-missing">
          <span className="spinner" />
          Waiting for the battlefield…
        </div>
      )}

      <SetupDrawer
        open={setupOpen}
        meta={meta}
        loadingMeta={loadingMeta}
        metaError={metaError}
        initialConfig={currentConfig}
        creating={creating}
        onClose={() => setSetupOpen(false)}
        onRetryMeta={loadMeta}
        onCreate={startGame}
      />
      <GameOver
        snapshot={snapshot}
        meta={meta}
        busy={creating}
        onRematch={replay}
        onNewGame={() => setSetupOpen(true)}
      />
      {gameError && (
        <div className="toast toast-error">
          <span>{gameError}</span>
          <button type="button" onClick={() => setGameError(null)}>
            Dismiss
          </button>
        </div>
      )}
    </div>
  );
}

function TopBar({
  inGame,
  seed,
  gameId,
  onNewGame,
}: {
  inGame: boolean;
  seed?: number;
  gameId?: string;
  onNewGame: () => void;
}) {
  return (
    <header className="top-bar">
      <div className="wordmark">
        <span className="wordmark-mark" aria-hidden="true">
          O
        </span>
        <span>
          <strong>OLD SCHOOL</strong>
          <small>ARENA LAB</small>
        </span>
      </div>
      <div className="top-context">
        {seed !== undefined && (
          <span>
            SEED <strong>{seed}</strong>
          </span>
        )}
        {gameId && (
          <span className="game-id" title={gameId}>
            MATCH <strong>{gameId.slice(0, 8).toUpperCase()}</strong>
          </span>
        )}
        {inGame && <span className="live-indicator">LIVE</span>}
      </div>
      <button className="new-game-button" type="button" onClick={onNewGame}>
        <span aria-hidden="true">＋</span>
        New match
      </button>
    </header>
  );
}
