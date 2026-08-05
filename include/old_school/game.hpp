#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace old_school {

enum class CardId : std::uint8_t {
    Forest,
    Mountain,
    GrizzlyBears,
    LightningBolt,
    IronrootTreefolk,
    FireElemental,
    Island,
    Counterspell,
    WaterElemental,
    Tsunami,
    Plains,
    Millstone,
    Moat,
    FlyingMen,
    IronclawOrcs,
    GrayOgre,
    HillGiant,
    Disintegrate,
    GiantGrowth,
    MoxSapphire,
    SolRing,
    AncestralRecall,
    TimeWalk,
    Braingeyser,
    ForceSpike,
    AirElemental,
    BlackLotus,
    Channel,
    LlanowarElves,
    MossBeast,
    ForestColossus,
    SavannahLions,
    SerendibEfreet,
    SerraAngel,
    BlackVise,
    MoxPearl,
    MoxRuby,
    Disenchant,
    PsionicBlast,
    SwordsToPlowshares,
    Plateau,
    Tundra,
    VolcanicIsland,
    WheelOfFortune,
    LibraryOfAlexandria,
    MishrasFactory,
    StripMine,
    SuChi,
    SageOfLatNam,
    Triskelion,
    ManaDrain,
    Armageddon,
    DemonicTutor,
    MindTwist,
    Recall,
    CopyArtifact,
    FellwarStone,
    MoxEmerald,
    MoxJet,
    CityOfBrass,
    UndergroundSea,
    Badlands,
    Timetwister,
    BenalishHero,
    MesaPegasus,
    ThunderSpirit,
    WhiteKnight,
    ChaosOrb,
    JalumTome,
    Crusade,
    JuzamDjinn,
    SedgeTroll,
    HypnoticSpecter,
    DarkRitual,
    Shatter,
    Swamp,
    Taiga,
    KirdApe,
    ScrybSprites,
    ArgothianPixies,
    ErhnamDjinn,
    Berserk,
    Regrowth,
    SylvanLibrary,
    Pendelhaven,
    Atog,
    AnkhOfMishra,
    RelicBarrier,
};

inline constexpr std::size_t kCardCount =
    static_cast<std::size_t>(CardId::RelicBarrier) + 1;

enum class CardType : std::uint8_t {
    Land,
    Creature,
    Instant,
    Sorcery,
    Artifact,
    Enchantment,
};

struct ManaCost {
    int generic = 0;
    int green = 0;
    int red = 0;
    int blue = 0;
    int white = 0;
    int black = 0;

    bool operator==(const ManaCost&) const = default;
};

struct CardDefinition {
    CardId id;
    std::string_view name;
    CardType type;
    ManaCost cost;
    int power = 0;
    int toughness = 0;
    int effect_damage = 0;
    bool flying = false;
    // Zero means the creature has no power-based blocking restriction.
    int cannot_block_power_at_least = 0;
    // Vigilant creatures do not tap when attacking.
    bool vigilance = false;
};

const CardDefinition& card_definition(CardId card);

std::vector<CardId> rg_berserk_deck();
std::vector<CardId> atog_deck();
std::vector<CardId> br_midrange_deck();
std::vector<CardId> robots_deck();
std::vector<CardId> white_weenie_deck();
std::vector<CardId> uwr_deck();

using PermanentId = std::uint64_t;

struct LandPermanent {
    CardId card;
    bool tapped = false;
    // Assigned when the land enters play; targeting (Strip Mine, the
    // simplified Chaos Orb) needs stable identities. Defaulted last so
    // existing aggregate initializers stay valid.
    PermanentId id = 0;
    // Copy Artifact aimed at an animated Factory yields an un-animated
    // Factory land: physical card stays CopyArtifact, face lives here.
    CardId copy_of = CardId::Forest;
    bool is_copy = false;
    // Set when the land arrives, cleared at its controller's next
    // turn start: an animated Factory is summoning-sick the turn its
    // land entered, exactly like any other fresh creature.
    bool entered_this_turn = false;

    bool operator==(const LandPermanent&) const = default;
};

struct CreaturePermanent {
    PermanentId id;
    CardId card;
    bool tapped = false;
    bool summoning_sick = true;
    int damage = 0;
    int temporary_power_bonus = 0;
    int temporary_toughness_bonus = 0;
    bool exile_on_death_this_turn = false;
    // +1/+1 counters (Triskelion); count into power and toughness.
    int plus_counters = 0;
    // Static battlefield buffs (Crusade, Sedge Troll's Swamp bonus,
    // Kird Ape's Forest bonus), recomputed after every resolution.
    // Power and toughness are tracked separately because Kird Ape's
    // +1/+2 is asymmetric.
    int static_power_bonus = 0;
    // Copy Artifact can copy artifact creatures (Su-Chi, Triskelion,
    // an animated Factory): the physical card stays CopyArtifact, the
    // behavioral face lives here.
    CardId copy_of = CardId::Forest;
    bool is_copy = false;
    // Toughness half of the static buff (see static_power_bonus).
    // Defaulted last so existing aggregate initializers stay valid.
    int static_toughness_bonus = 0;
    // Berserk grants trample until end of turn; cleared at cleanup
    // like the temporary pump bonuses.
    bool trample = false;
    // Set when this creature is declared as an attacker, cleared at
    // cleanup. Berserk's destroy clause reads it.
    bool attacked_this_turn = false;
    // Set when Berserk resolves on this creature this turn: if it
    // also attacked, it is destroyed at cleanup.
    bool berserked_this_turn = false;

    bool operator==(const CreaturePermanent&) const = default;
};

struct ArtifactPermanent {
    PermanentId id;
    CardId card;
    bool tapped = false;
    // Copy Artifact: the physical card stays CopyArtifact (zone
    // conservation), behavior follows the copied artifact.
    CardId copy_of = CardId::Forest;  // Forest = not a copy
    bool is_copy = false;

    bool operator==(const ArtifactPermanent&) const = default;
};

struct PlayerState {
    int life = 20;
    // Channel's until-end-of-turn effect: pay 1 life for 1 mana.
    bool channel_active = false;
    std::vector<CardId> library;
    std::vector<CardId> hand;
    std::vector<CardId> graveyard;
    std::vector<CardId> exile;
    std::vector<LandPermanent> lands;
    std::vector<CreaturePermanent> creatures;
    std::vector<ArtifactPermanent> artifacts;
    std::vector<CardId> enchantments;
    // Mana abilities are implicit and do not use the stack. Unspent mana
    // remains available through the current phase, then is cleared.
    ManaCost mana_pool;
    bool land_played_this_turn = false;
    // Mana Drain: generic mana owed to this player at their next
    // turn's beginning.
    int pending_mana = 0;

    bool operator==(const PlayerState&) const = default;
};

struct Target {
    std::size_t player = 0;
    std::optional<PermanentId> creature;

    static Target player_target(std::size_t player_index);
    static Target creature_target(std::size_t controller,
                                  PermanentId creature_id);

    bool operator==(const Target&) const = default;
};

struct PlayerGameStats {
    std::size_t cards_drawn = 0;
    std::size_t lands_played = 0;
    std::size_t spells_cast = 0;
    std::size_t spells_countered = 0;
    std::size_t damage_to_opponent = 0;
    std::size_t decisions = 0;
    std::size_t monte_carlo_rollouts = 0;
    std::size_t mulligans = 0;

    bool operator==(const PlayerGameStats&) const = default;
};

using StackObjectId = std::uint64_t;

enum class StackObjectKind : std::uint8_t {
    Spell,
    ActivatedAbility,
};

struct StackObject {
    StackObjectKind kind = StackObjectKind::Spell;
    StackObjectId id;
    CardId card;
    std::size_t controller;
    std::optional<Target> target;
    std::optional<StackObjectId> spell_target;
    int x_value = 0;
    // Demonic Tutor's selection travels with the spell.
    std::optional<CardId> chosen_card;

    bool operator==(const StackObject&) const = default;
};

struct GameState {
    std::array<PlayerState, 2> players;
    std::array<PlayerGameStats, 2> stats;
    std::vector<StackObject> stack;
    // Time Walk queues an extra turn for its controller. A queued turn is
    // consumed only after that player's current turn finishes.
    std::array<std::size_t, 2> extra_turns_pending = {0, 0};
    // Drawing from an empty library is recorded during spell resolution and
    // converted to a terminal result immediately after that object resolves.
    std::array<bool, 2> failed_draw = {false, false};
    std::size_t active_player = 0;
    std::size_t starting_player = 0;
    std::size_t turn_number = 0;
    PermanentId next_permanent_id = 1;
    StackObjectId next_stack_object_id = 1;

    bool operator==(const GameState&) const = default;
};

// Samples a complete state consistent with everything `observer` can know.
// The original decklists and all public physical zones determine each
// player's hidden card pool. The observer's hand is preserved, the opposing
// hand is sampled by its public size, and both libraries receive fresh random
// orders. Spell stack objects are physical cards; activated abilities are not.
GameState sample_determinization(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t observer, std::uint64_t seed);

enum class PriorityActionKind : std::uint8_t {
    Pass,
    PlayLand,
    CastCreature,
    CastSorcery,
    CastArtifact,
    CastEnchantment,
    CastLightningBolt,
    CastDisintegrate,
    CastGiantGrowth,
    CastCounterspell,
    CastAncestralRecall,
    CastBraingeyser,
    CastForceSpike,
    ActivateMillstone,
    CastPsionicBlast,
    CastSwordsToPlowshares,
    CastDisenchant,
    ActivateLibrary,
    ActivateMishrasFactory,
    ActivateStripMine,
    CastManaDrain,
    CastMindTwist,
    CastRecall,
    CastDemonicTutor,
    CastCopyArtifact,
    ActivateSage,
    ActivateTriskelion,
    TapLandForMana,
    ActivateChaosOrb,
    ActivateJalumTome,
    CastDarkRitual,
    CastShatter,
    CastBerserk,
    CastRegrowth,
    ActivatePendelhaven,
    ActivateAtog,
    ActivateRelicBarrier,
};

// One explicit mana float: tap this permanent for the given face
// (0 generic, 1 green, 2 red, 3 blue, 4 white, 5 black) before paying
// a spell's cost. Carried by payment-variant actions.
struct PaymentTap {
    PermanentId permanent = 0;
    int face = 0;

    bool operator==(const PaymentTap&) const = default;
};

struct PriorityAction {
    PriorityActionKind kind = PriorityActionKind::Pass;
    CardId card = CardId::Forest;
    std::optional<Target> target;
    std::optional<StackObjectId> spell_target;
    std::optional<PermanentId> source_permanent;
    int x_value = 0;
    // Demonic Tutor's selected card.
    std::optional<CardId> chosen_card;
    // Non-empty: pay by floating exactly these taps first (the auto
    // payer then consumes the floated pool before touching anything
    // else). Empty: the mana planner chooses the payment.
    std::vector<PaymentTap> pre_taps;

    static PriorityAction pass();
    static PriorityAction play_land(CardId land);
    static PriorityAction cast_creature(CardId creature);
    static PriorityAction cast_sorcery(CardId sorcery);
    static PriorityAction cast_artifact(CardId artifact);
    static PriorityAction cast_enchantment(CardId enchantment);
    static PriorityAction cast_lightning_bolt(Target bolt_target);
    static PriorityAction cast_disintegrate(int x_value,
                                            Target disintegrate_target);
    static PriorityAction cast_giant_growth(Target growth_target);
    static PriorityAction cast_counterspell(StackObjectId target_spell);
    static PriorityAction cast_ancestral_recall(Target draw_target);
    static PriorityAction cast_braingeyser(int x_value,
                                           Target draw_target);
    static PriorityAction cast_force_spike(StackObjectId target_spell);
    static PriorityAction cast_psionic_blast(Target blast_target);
    static PriorityAction cast_swords(Target creature_target);
    // Disenchant: artifact by permanent id, or an enchantment by index
    // into its controller's enchantment row (creature field empty).
    static PriorityAction cast_disenchant_artifact(std::size_t owner,
                                                   PermanentId artifact);
    static PriorityAction cast_disenchant_enchantment(std::size_t owner,
                                                       int index);
    static PriorityAction activate_library(PermanentId library);
    static PriorityAction animate_factory(PermanentId factory);
    static PriorityAction activate_strip_mine(PermanentId strip_mine,
                                              std::size_t owner,
                                              PermanentId land);
    static PriorityAction activate_millstone(PermanentId millstone,
                                             Target mill_target);
    static PriorityAction cast_mana_drain(StackObjectId target_spell);
    static PriorityAction cast_mind_twist(int x_value,
                                          std::size_t target_player);
    static PriorityAction cast_recall(int x_value);
    static PriorityAction cast_demonic_tutor(CardId chosen);
    static PriorityAction cast_copy_artifact(std::size_t owner,
                                             PermanentId artifact);
    static PriorityAction activate_sage(PermanentId sage,
                                        PermanentId artifact);
    static PriorityAction activate_triskelion(PermanentId triskelion,
                                              Target target);
    // Human-only manual mana: tap a land for one face. `color_index`
    // rides in x_value: 0 generic, 1 green, 2 red, 3 blue, 4 white,
    // 5 black. Never offered to bot seats or SPZ search.
    static PriorityAction tap_land_for_mana(PermanentId land,
                                            int color_index);
    // Chaos Orb (house rules: no flip): tap and sacrifice the orb to
    // destroy any permanent. Creatures/lands/artifacts target by
    // permanent id; an enchantment targets its owner with the row
    // index in x_value.
    static PriorityAction activate_chaos_orb(PermanentId orb,
                                             Target target);
    static PriorityAction activate_chaos_orb_enchantment(
        PermanentId orb, std::size_t owner, int index);
    static PriorityAction activate_jalum_tome(PermanentId tome);
    static PriorityAction cast_dark_ritual();
    static PriorityAction cast_shatter(std::size_t owner,
                                       PermanentId artifact);
    // Berserk: target creature doubles its power (+X/+0 where X is
    // its power at resolution), gains trample until end of turn, and
    // is destroyed at end of turn if it attacked this turn.
    static PriorityAction cast_berserk(Target creature_target);
    // Regrowth: Demonic Tutor's chosen-card pattern with the source
    // being the caster's own graveyard.
    static PriorityAction cast_regrowth(CardId chosen);
    // Pendelhaven: tap to give a creature whose CURRENT stats are
    // exactly 1/1 +1/+2 until end of turn.
    static PriorityAction activate_pendelhaven(PermanentId land,
                                               Target creature_target);
    // Atog: sacrifice one of your artifacts (no mana): +2/+2 until
    // end of turn. Multiple activations per turn are legal.
    static PriorityAction activate_atog(PermanentId atog,
                                        PermanentId artifact);
    // Relic Barrier: tap to tap target artifact (any player's).
    static PriorityAction activate_relic_barrier(PermanentId barrier,
                                                 std::size_t owner,
                                                 PermanentId artifact);

    bool operator==(const PriorityAction&) const = default;
};

// Sorcery actions means the active player is in a main phase. Lands and
// creatures additionally require an empty stack; instants do not.
// Mana arithmetic over the implicit (stackless) mana system. Payment taps
// producers, sacrifices Black Lotuses, and converts life through an active
// Channel, exactly as spell casting does.
bool can_pay(const PlayerState& player, const ManaCost& cost);
bool pay_mana(PlayerState& player, const ManaCost& cost);
// True when the land can produce the given colored mana face.
bool land_provides(CardId land, int ManaCost::*color);

// Alternative full payments for `cost` from this player's untapped
// sources, as explicit pre-tap lists (never Lotus/Channel funded).
// Distinct from each other and from the auto planner's choice; at most
// `max_variants` entries.
std::vector<std::vector<PaymentTap>> alternative_payments(
    const PlayerState& player, const ManaCost& cost,
    std::size_t max_variants);
int maximum_available_mana(const PlayerState& player);

std::vector<PriorityAction>
legal_priority_actions(const GameState& state, std::size_t player,
                       bool sorcery_actions);
bool apply_priority_action(GameState& state, std::size_t player,
                           const PriorityAction& action,
                           bool sorcery_actions);
enum class ForceSpikePaymentChoice : std::uint8_t {
    PayIfAble,
    Decline,
};

bool resolve_top_of_stack(
    GameState& state,
    ForceSpikePaymentChoice force_spike_payment =
        ForceSpikePaymentChoice::PayIfAble);

// Advances only turn ownership, consuming a queued extra turn when present.
// The caller remains responsible for incrementing turn_number and beginning
// the turn.
std::size_t advance_turn_player(GameState& state);

struct PriorityState {
    std::size_t player = 0;
    int consecutive_passes = 0;

    bool operator==(const PriorityState&) const = default;
};

enum class TurnPhase : std::uint8_t {
    FirstMain,
    BeginCombat,
    DeclareAttackers,
    DeclareBlockers,
    DamageOrder,
    EndCombat,
    SecondMain,
};

// A perspective-safe snapshot for an interactive player. Public zones contain
// card identities; hidden zones expose counts only. The observer's own hand is
// the sole hidden zone whose card identities are present unless a human
// controller explicitly opts into the debug-only opponent-hand reveal below.
struct PublicPlayerState {
    int life = 20;
    bool channel_active = false;
    std::size_t library_size = 0;
    std::size_t hand_size = 0;
    std::vector<CardId> graveyard;
    std::vector<CardId> exile;
    std::vector<LandPermanent> lands;
    std::vector<CreaturePermanent> creatures;
    std::vector<ArtifactPermanent> artifacts;
    std::vector<CardId> enchantments;
    ManaCost mana_pool;
    int pending_mana = 0;
    bool land_played_this_turn = false;

    bool operator==(const PublicPlayerState&) const = default;
};

struct PlayerObservation {
    std::size_t observer = 0;
    std::array<PublicPlayerState, 2> players;
    std::vector<CardId> hand;
    // Populated only for an opted-in human debug controller.
    // observe_game_state() leaves this empty, preserving the normal
    // hidden-information boundary.
    std::optional<std::vector<CardId>> revealed_opponent_hand;
    std::vector<StackObject> stack;
    std::array<std::size_t, 2> extra_turns_pending = {0, 0};
    std::size_t active_player = 0;
    std::size_t starting_player = 0;
    std::size_t turn_number = 0;

    bool operator==(const PlayerObservation&) const = default;
};

PlayerObservation observe_game_state(const GameState& state,
                                     std::size_t observer);

// One entry per untapped defending creature that can block at least one
// attacker. The creature may be left unassigned or assigned to exactly one of
// `legal_attackers`.
struct LegalBlockerChoice {
    PermanentId blocker = 0;
    std::vector<PermanentId> legal_attackers;
};

enum class GameEventKind : std::uint8_t {
    TurnStarted,
    PriorityActionSelected,
    StackObjectResolved,
    AttackersDeclared,
    BlockersDeclared,
    DamageOrderChosen,
    CombatResolved,
    CardsDiscarded,
    MulliganTaken,
};

// Event payloads contain public game information only. `blocks` uses
// (attacker, blocker) pairs and its per-attacker order is combat damage order
// for DamageOrderChosen and CombatResolved.
struct GameEvent {
    GameEventKind kind = GameEventKind::TurnStarted;
    std::size_t player = 0;
    TurnPhase phase = TurnPhase::FirstMain;
    std::optional<PriorityAction> priority_action;
    std::optional<StackObject> stack_object;
    std::vector<PermanentId> attackers;
    std::vector<std::pair<PermanentId, PermanentId>> blocks;
    std::vector<CardId> cards;

    bool operator==(const GameEvent&) const = default;
};

struct HumanController {
    // Return an index into the supplied legal action list.
    std::function<std::size_t(
        const PlayerObservation&, TurnPhase,
        const std::vector<PriorityAction>&)>
        choose_priority_action;
    // Return a unique subset of the supplied eligible attacker IDs.
    std::function<std::vector<PermanentId>(
        const PlayerObservation&,
        const std::vector<PermanentId>&)>
        choose_attackers;
    // Return (attacker, blocker) pairs. Each blocker may appear at most once
    // and may only choose from its LegalBlockerChoice entry.
    std::function<std::vector<std::pair<PermanentId, PermanentId>>(
        const PlayerObservation&,
        const std::vector<PermanentId>&,
        const std::vector<LegalBlockerChoice>&)>
        choose_blockers;
    // Return an exact permutation of `blockers`, first to receive damage
    // first. Called only when at least two creatures block one attacker.
    std::function<std::vector<PermanentId>(
        const PlayerObservation&, PermanentId,
        const std::vector<PermanentId>&)>
        choose_damage_order;
    // Return exactly `excess` unique zero-based positions from this
    // observation's hand. Cleanup publishes the discarded card identities.
    std::function<std::vector<std::size_t>(
        const PlayerObservation&, std::size_t excess)>
        choose_cleanup_discards;
    // Optional Sylvan Library upkeep decision: after drawing the two
    // extra cards, return exactly `count` unique hand positions to
    // put back on top of the library (the first position listed is
    // drawn next). Absent, the seat returns its two lowest-valued
    // cards (handcrafted valuation), which is also every bot's rule.
    std::function<std::vector<std::size_t>(
        const PlayerObservation&, std::size_t count)>
        choose_sylvan_returns;
    // Optional Paris-mulligan decision at game start: return true to
    // shuffle the hand away and draw one fewer card. Absent, a
    // controller seat always keeps; a pure-bot seat uses the default
    // heuristic.
    std::function<bool(const PlayerObservation&)> choose_mulligan;
    // Optional transcript hook. It receives this controller's observation
    // after public state changes (or at the declaration point for passes).
    std::function<void(const PlayerObservation&, const GameEvent&)>
        observe;
    // Interactive inspection aid. This affects only this human controller's
    // callbacks; bot observations, training features, and rollouts stay
    // hidden-information safe.
    bool reveal_opponent_hand = false;
    // A human bluffing interface may need to pause even when Pass is the
    // sole legal priority action. The default preserves automatic forced
    // passes for terminal play, simulations, and tests.
    bool bluff_mode = false;
    // Offer manual TapLandForMana actions in priority windows. Only the
    // web arena's real human seat sets this; SPZ controller seats and
    // simulations keep the automatic payer exclusively.
    bool offers_mana_taps = false;
    // Offer payment-plan variants of cast actions (same spell, an
    // explicit alternative set of pre-taps). Experimental SPZ lever.
    bool offers_payment_variants = false;
};

enum class PriorityPassResult : std::uint8_t {
    Passed,
    StackObjectResolved,
    WindowEnded,
};

PriorityPassResult pass_priority(GameState& state,
                                 PriorityState& priority);

// Rules-only immediate-consequence transition used by evaluators that need
// to observe what a Priority action actually does, rather than the unresolved
// object it may first place on the stack. No opposing response is chosen:
// only Pass is taken until the object created by `action` (or the current top
// object when `action` is Pass) leaves the stack. Empty-stack actions stop
// after their immediate application.
struct ResolvedPriorityActionConsequence {
    GameState state;
    PriorityState priority;
    bool window_ended = false;
    bool terminal = false;
    int winner = -1;
    std::size_t priority_passes = 0;
    std::size_t stack_resolutions = 0;

    bool operator==(
        const ResolvedPriorityActionConsequence&) const = default;
};

std::optional<ResolvedPriorityActionConsequence>
resolve_priority_action_consequence(
    const GameState& state, std::size_t player, bool sorcery_actions,
    int consecutive_passes, const PriorityAction& action);

// A deterministic, rules-only proof that a legal Priority action is strictly
// worse than Pass. Both branches are force-passed until the current
// stack/window settles. Malformed or nonsettling comparisons fail closed by
// retaining the candidate.
struct ValuePassDominanceActionDiagnostic {
    PriorityAction action;
    bool comparison_settled = false;
    bool strictly_dominated_by_pass = false;

    bool operator==(
        const ValuePassDominanceActionDiagnostic&) const = default;
};

struct ValuePassDominanceDiagnostic {
    std::vector<ValuePassDominanceActionDiagnostic> actions;
    std::size_t pass_index = 0;
    bool pass_settled = false;
    std::size_t failed_comparisons = 0;

    std::vector<PriorityAction> retained_actions() const;

    bool operator==(const ValuePassDominanceDiagnostic&) const = default;
};

ValuePassDominanceDiagnostic diagnose_value_pass_dominance(
    const GameState& state, std::size_t player,
    bool sorcery_actions, TurnPhase phase, int consecutive_passes);

// Attackers and blockers are identified by permanent ID. A repeated attacker in
// blocks represents multiple creatures blocking it; block order is damage order.
// Returns the complete engine-authoritative attacker set in battlefield order,
// including all current permanent- and battlefield-based attack restrictions.
std::vector<PermanentId> legal_attackers(
    const GameState& state, std::size_t attacking_player);

bool resolve_combat(
    GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& attackers,
    const std::vector<std::pair<PermanentId, PermanentId>>& blocks);

void begin_turn(GameState& state, std::size_t player);
// Default keep/mulligan heuristic: mulligan seven- and six-card hands
// whose mana-source count is unplayable (fewer than two or more than
// five sources).
bool default_mulligan_choice(const std::vector<CardId>& hand);
// Handcrafted keep/mulligan: mulligan hands with fewer than three lands
// (down to four cards). The lotus-combo deck instead digs for the kill
// (two Black Lotus, a Channel, and a Disintegrate), also stopping at
// four cards.
bool handcrafted_mulligan_choice(const std::vector<CardId>& hand,
                                 const std::vector<CardId>& deck);
inline constexpr std::size_t kMaximumHandSize = 7;
std::vector<CardId> cleanup_turn(
    GameState& state, std::size_t active_player,
    const std::vector<std::size_t>& discard_indices);
// Sylvan Library's upkeep trigger fires when its controller has at
// least one Sylvan Library in play. Multiple copies do NOT stack: one
// trigger per upkeep regardless of count (simplest legal reading).
bool has_sylvan_library(const GameState& state, std::size_t player);
// After the two extra draws, move the chosen hand positions to the
// TOP of the library. Deterministic order: indices[0] ends on top and
// is drawn next, indices[1] directly beneath it.
void sylvan_return_to_library(
    GameState& state, std::size_t player,
    const std::vector<std::size_t>& indices);

enum class EndReason : std::uint8_t {
    LifeTotal,
    EmptyLibrary,
    TurnLimit,
};

enum class BotKind : std::uint8_t {
    Random,
    MonteCarlo,
    DeepMonteCarlo,
    Handcrafted,
};

inline constexpr std::size_t kBotKindCount = 4;
inline constexpr std::size_t kBotMatchupCount =
    kBotKindCount * (kBotKindCount - 1) / 2;

struct BotConfig {
    BotKind kind = BotKind::Random;
    // Complete random continuations sampled for every legal action.
    std::size_t rollouts_per_action = 2;
    double exploration_rate = 0.0;
};

struct GameResult {
    // 0 or 1 for a winner; -1 for a draw.
    int winner = -1;
    EndReason reason = EndReason::TurnLimit;
    std::size_t turns = 0;
    std::size_t starting_player = 0;
    std::array<int, 2> ending_life = {20, 20};
    std::array<PlayerGameStats, 2> player_stats;
    std::array<BotKind, 2> bots = {
        BotKind::Random,
        BotKind::Random,
    };

    bool operator==(const GameResult&) const = default;
};

// Canonical soft terminal label: draws are 0.5 and decisive results are
// discounted by game length.
double discounted_terminal_target(
    const GameResult& result, std::size_t perspective);

struct GameConfig {
    std::size_t max_turns = 500;
    std::optional<std::size_t> starting_player;
    std::array<BotConfig, 2> bots = {BotConfig{}, BotConfig{}};
    // A present controller overrides the corresponding BotConfig only for
    // real-game decisions. Internal bot rollouts remove these callbacks.
    std::array<std::optional<HumanController>, 2> human_controllers;
};

// Evaluation-only held-out state used by focused tests.
GameState white_lock_plan_diagnostic_state();

std::vector<double> handcrafted_priority_scores(
    const GameState& state, std::size_t player,
    const std::vector<PriorityAction>& candidates,
    TurnPhase phase = TurnPhase::FirstMain);
std::array<double, 2> handcrafted_binary_attack_scores(
    const GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& selected_attackers,
    PermanentId subject,
    const std::vector<PermanentId>& remaining_attackers);

class Game {
  public:
    Game(std::vector<CardId> player_zero_deck,
         std::vector<CardId> player_one_deck, std::uint64_t seed,
         GameConfig config = {});

    GameResult run();
    GameResult run_with_trace(std::vector<GameState>& trace);
    const GameState& state() const;

  private:
    friend std::vector<double> handcrafted_priority_scores(
        const GameState& state, std::size_t player,
        const std::vector<PriorityAction>& candidates,
        TurnPhase phase);

    void initialize();
    bool draw_card(std::size_t player);
    std::optional<GameResult>
    play_priority_window(bool sorcery_actions, TurnPhase phase);
    std::optional<GameResult>
    continue_priority_window(bool sorcery_actions,
                             TurnPhase phase, PriorityState priority);
    std::optional<GameResult> play_combat();
    std::optional<GameResult> play_combat_after_beginning();
    std::optional<GameResult> play_combat_with_attackers(
        std::vector<PermanentId> attackers);
    PriorityAction
    choose_priority_action(const std::vector<PriorityAction>& actions,
                           std::size_t player, bool sorcery_actions,
                           TurnPhase phase);
    std::optional<GameResult>
    finish_turn_after_priority_phase(TurnPhase phase);
    PriorityAction
    choose_handcrafted_action(const std::vector<PriorityAction>& actions,
                              std::size_t player, TurnPhase phase);
    double handcrafted_action_score(const PriorityAction& action,
                                    std::size_t player,
                                    TurnPhase phase) const;
    double rollout_action(const PriorityAction& action,
                          std::size_t player, bool sorcery_actions,
                          std::uint64_t seed) const;
    std::vector<std::size_t>
    choose_cleanup_discards(std::size_t player,
                            std::size_t excess);
    void perform_cleanup();
    // Sylvan Library's upkeep: draw two extra cards, then return two
    // chosen cards to the top of the library. Returns a result only
    // when a draw from an empty library ends the game.
    std::optional<GameResult>
    perform_sylvan_library_upkeep(std::size_t player);
    GameResult run_from_turn(std::size_t first_turn);
    std::optional<GameResult> life_total_result() const;
    GameResult make_result(int winner, EndReason reason) const;
    const HumanController*
    human_controller(std::size_t player) const;
    PlayerObservation
    human_observation(std::size_t player) const;
    bool has_human_observer() const;
    void notify_human_observers(const GameEvent& event) const;

    std::array<std::vector<CardId>, 2> decks_;
    std::mt19937_64 random_;
    GameConfig config_;
    GameState state_;
    std::optional<GameResult> setup_result_;
    std::vector<GameState>* trace_ = nullptr;
};

struct DeckSimulationStats {
    std::size_t games = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t draws = 0;
    std::size_t on_play_games = 0;
    std::size_t on_play_wins = 0;
    std::size_t on_draw_games = 0;
    std::size_t on_draw_wins = 0;
    std::int64_t total_ending_life = 0;
    std::size_t total_cards_drawn = 0;
    std::size_t total_lands_played = 0;
    std::size_t total_spells_cast = 0;
    std::size_t total_spells_countered = 0;
    std::size_t total_damage_to_opponent = 0;

    double win_rate() const;
    double on_play_win_rate() const;
    double on_draw_win_rate() const;
    double average_ending_life() const;
    double average_cards_drawn() const;
    double average_lands_played() const;
    double average_spells_cast() const;
    double average_spells_countered() const;
    double average_damage_to_opponent() const;
};

struct BotSimulationStats {
    // One entry per player seat, so two seat-games are recorded per game.
    std::size_t games = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t draws = 0;
    std::size_t total_decisions = 0;
    std::size_t total_rollouts = 0;

    double win_rate() const;
    double average_decisions() const;
    double average_rollouts() const;
    double average_rollouts_per_decision() const;
};

struct BotMatchupStats {
    BotKind first_bot = BotKind::Random;
    BotKind second_bot = BotKind::MonteCarlo;
    std::size_t games = 0;
    std::size_t first_wins = 0;
    std::size_t second_wins = 0;
    std::size_t draws = 0;

    double first_win_rate() const;
    double second_win_rate() const;
};

struct SimulationSummary {
    std::size_t games = 0;
    std::array<DeckSimulationStats, 2> decks;
    std::array<std::array<DeckSimulationStats, kBotKindCount>, 2>
        deck_bots;
    std::array<BotSimulationStats, kBotKindCount> bots;
    std::array<BotMatchupStats, kBotMatchupCount> bot_matchups;
    std::size_t draws = 0;
    std::size_t life_total_finishes = 0;
    std::size_t empty_library_finishes = 0;
    std::size_t turn_limit_draws = 0;
    std::size_t total_turns = 0;

    double average_turns() const;
};

SimulationSummary run_simulation(std::size_t games, std::uint64_t seed,
                                 GameConfig game_config = {});

enum class DeckId : std::uint8_t {
    RGBerserk,
    Atog,
    BRMidrange,
    Robots,
    WhiteWeenie,
    UWR,
};

inline constexpr std::size_t kDeckCount = 6;
inline constexpr std::size_t kDistinctDeckPairingCount =
    kDeckCount * (kDeckCount - 1) / 2;

std::string_view deck_name(DeckId deck);
std::string_view deck_list(DeckId deck);
std::string_view bot_name(BotKind bot);

struct DeckEvolutionConfig {
    std::size_t generations = 10;
    std::size_t population = 16;
    std::size_t repetitions_per_opponent = 4;
    BotConfig pilot = {
        .kind = BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    // Optional controller-driven pilot (for example Self-Play Zero). When
    // set it is invoked once per evolution game with the two seat decks and
    // a deterministic seed, and the returned controllers drive both seats;
    // `pilot` is then ignored for decisions.
    std::function<std::array<std::optional<HumanController>, 2>(
        const std::array<std::vector<CardId>, 2>& decks,
        std::uint64_t game_seed)>
        controller_pilot;
};

struct EvolvedDeck {
    std::vector<CardId> cards;
    DeckSimulationStats total;
    std::array<DeckSimulationStats, kDeckCount> by_opponent;
};

struct DeckEvolutionSummary {
    EvolvedDeck best;
    // The final generation's strongest distinct decks (best first,
    // up to five), with the same evaluation stats as `best`.
    std::vector<EvolvedDeck> top;
    std::vector<double> generation_best_win_rates;
};

DeckEvolutionSummary
evolve_deck(DeckEvolutionConfig config, std::uint64_t seed,
            GameConfig game_config = {});

} // namespace old_school
