//! Coldsnap card records required by supported formats.

use super::{CardRecord, PrintingAnchor, PrintingRecord};
use crate::card::{
    AbilityCostDef, AbilityDef, AbilityTargetDef, AbilityTargetPredicate, CardArt, CardRules,
    CardSet, EffectDef, EffectRecipientDef, InstalledTriggerDef, PlayerRelation,
    TopCardSelectionDef, TriggerEventDef, TurnStepDef, ValueDef, ZoneKind, ZonePlacement,
};
use crate::{TargetIndex, mana_cost};

// CSP 138 — Mishra's Bauble
/// Nothing is taken and nothing moves: the whole effect is the looking, so
/// the selection takes none of the one card and puts it back where it was.
static BAUBLE_LOOK: TopCardSelectionDef = TopCardSelectionDef {
    count: ValueDef::Constant(1),
    object: None,
    minimum: 0,
    maximum: 0,
    select_all_matching: false,
    reveal_selected: false,
    counted: None,
    selected_zone: ZoneKind::Library,
    selected_placement: ZonePlacement::Top,
    rest_zone: ZoneKind::Library,
    rest_placement: ZonePlacement::Top,
    rest_random_order: false,
    rest_counters: None,
    selected_order_follows_choice: false,
    then: None,
    selected_hidden: false,
    selected_linked_to_source: false,
    selected_face_down: None,
};

/// "The next turn's upkeep", whoever's turn that is -- so a Bauble cracked
/// on your own turn draws on their upkeep, and one cracked on theirs draws
/// on yours. The listener is consumed by the first upkeep it sees, which is
/// what "the next" means.
static BAUBLE_DRAWS_NEXT_UPKEEP: AbilityDef = AbilityDef::triggered(
    "Draw a card at the beginning of the next turn's upkeep.",
    TriggerEventDef::StepBegins {
        step: TurnStepDef::Upkeep,
        player: PlayerRelation::Any,
    },
    EffectDef::DrawCards {
        recipient: EffectRecipientDef::Controller,
        amount: ValueDef::Constant(1),
    },
);

static BAUBLE_EFFECT: EffectDef = EffectDef::Sequence(&[
    EffectDef::LookAtTopAndSelect {
        player: EffectRecipientDef::Target(TargetIndex::PRIMARY),
        looker: EffectRecipientDef::Controller,
        selection: &BAUBLE_LOOK,
    },
    EffectDef::InstallTrigger(InstalledTriggerDef::once(&BAUBLE_DRAWS_NEXT_UPKEEP)),
]);

static A_PLAYER: [AbilityTargetDef; 1] = [AbilityTargetDef::exactly_one(
    AbilityTargetPredicate::Player(PlayerRelation::Any),
)];

static BAUBLE_COSTS: [AbilityCostDef; 2] =
    [AbilityCostDef::TapSource, AbilityCostDef::SacrificeSource];

pub(in crate::card::sets) static MISHRA_S_BAUBLE: CardRecord = CardRecord::new(
    PrintingAnchor::scryfall("8a720448-017f-4f4a-9501-678245eaed17"),
    "Mishra's Bauble",
    CardArt::new("8a720448-017f-4f4a-9501-678245eaed17", "Chippy"),
    CardSet::Coldsnap,
    // A free artifact that replaces itself a turn later. The looking is
    // incidental; what the card is played for is being an artifact that cost
    // nothing and a card that comes back.
    CardRules::new_artifact(mana_cost!("{0}")).with_ability(AbilityDef::activated_with_targets(
        "{T}, Sacrifice this artifact: Look at the top card of target player's library. Draw a \
         card at the beginning of the next turn's upkeep.",
        &BAUBLE_COSTS,
        &A_PLAYER,
        BAUBLE_EFFECT,
    )),
);

// CSP 145 — Dark Depths
// Audit: metadata-only — Card rules have not been implemented.
pub(in crate::card::sets) static DARK_DEPTHS: CardRecord = CardRecord::new(
    PrintingAnchor::scryfall("92409c3a-fb1a-4205-9fe1-0f5affc7b21d"),
    "Dark Depths",
    crate::card::CardArt::new("92409c3a-fb1a-4205-9fe1-0f5affc7b21d", "Stephan Martiniere"),
    crate::card::CardSet::Coldsnap,
    crate::card::CardRules::unsupported(),
);

pub(in crate::card::sets) static CARDS: &[&CardRecord] = &[&MISHRA_S_BAUBLE, &DARK_DEPTHS];

pub(in crate::card::sets) static ADDITIONAL_PRINTINGS: &[PrintingRecord] = &[];
