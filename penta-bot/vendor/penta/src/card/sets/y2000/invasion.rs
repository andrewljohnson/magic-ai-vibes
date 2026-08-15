//! Invasion cards used by the staged Premodern deck tranche.

use super::{CardRecord, PrintingRecord};
use crate::card::{
    AbilityCostDef, AbilityDef, AddManaEffectDef, AppliedEffectDef, CardArt, CardRules, CardSet,
    CardType, EffectDef, EffectDurationDef, EffectRecipientDef, ManaColor, ObjectPredicateDef,
    PlayerRelation, TopCardSelectionDef, TriggerEventDef, ValueDef, ZoneKind, ZonePlacement,
    abilities, cards,
};
use crate::mana_cost;

// INV 57 — Fact or Fiction
pub(in crate::card::sets) static FACT_OR_FICTION: CardRecord = CardRecord::new(
    cards::FACT_OR_FICTION,
    "Fact or Fiction",
    CardArt::new(
        "7fd4d018-dcf3-4439-8445-02d66e44f7d3",
        "Terese Nielsen",
    ),
    CardSet::Invasion,
    CardRules::new_instant(mana_cost!("{3}{U}")).with_ability(AbilityDef::spell(
        "Reveal the top five cards of your library. An opponent separates those cards into two piles. Put one pile into your hand and the other into your graveyard.",
        EffectDef::RevealAndSplitIntoPiles {
            count: ValueDef::Constant(5),
            rest: ZoneKind::Graveyard,
            placement: ZonePlacement::Top,
        },
    )),
);

static OPT_DRAW: EffectDef = EffectDef::DrawCards {
    recipient: EffectRecipientDef::Controller,
    amount: ValueDef::Constant(1),
};

static OPT_SELECTION: TopCardSelectionDef = TopCardSelectionDef {
    count: ValueDef::Constant(1),
    minimum: 0,
    maximum: 1,
    selected_zone: ZoneKind::Library,
    selected_placement: ZonePlacement::Bottom,
    rest_zone: ZoneKind::Library,
    rest_placement: ZonePlacement::Top,
    then: Some(&OPT_DRAW),
};

// INV 64 — Opt
pub(in crate::card::sets) static OPT: CardRecord = CardRecord::new(
    cards::OPT,
    "Opt",
    CardArt::new("958262ec-8e52-40cf-a9fd-a60e42643e15", "John Howe"),
    CardSet::Invasion,
    CardRules::new_instant(mana_cost!("{U}")).with_ability(AbilityDef::spell(
        "Scry 1.\nDraw a card.",
        EffectDef::LookAtTopAndSelect {
            player: EffectRecipientDef::Controller,
            selection: &OPT_SELECTION,
        },
    )),
);

// INV 317 — Tsabo's Web
pub(in crate::card::sets) static TSABOS_WEB: CardRecord = CardRecord::new(
    cards::TSABOS_WEB,
    "Tsabo's Web",
    CardArt::new("0dee69f8-cceb-41b9-a0ee-6b2ac9f4bad9", "Carl Critchlow"),
    CardSet::Invasion,
    CardRules::new_artifact(mana_cost!("{2}")).with_abilities(&[
        AbilityDef::triggered(
            "When this artifact enters, draw a card.",
            TriggerEventDef::ZoneChanged {
                object: ObjectPredicateDef::Source,
                from: None,
                to: Some(ZoneKind::Battlefield),
            },
            EffectDef::DrawCards {
                recipient: EffectRecipientDef::Controller,
                amount: ValueDef::Constant(1),
            },
        ),
        AbilityDef::static_ability(
            "Each land with an activated ability that isn't a mana ability doesn't untap during its controller's untap step.",
            EffectDef::Apply {
                recipient: EffectRecipientDef::MatchingObjects {
                    object: ObjectPredicateDef::All(&[
                        ObjectPredicateDef::HasType(CardType::Land),
                        ObjectPredicateDef::HasNonManaActivatedAbility,
                    ]),
                    zones: &[ZoneKind::Battlefield],
                    controller: PlayerRelation::Any,
                },
                effect: AppliedEffectDef::DoesNotUntapDuringUntapStep,
                duration: EffectDurationDef::WhileSourceRemainsInZone,
            },
        ),
    ]),
);

// INV 321 — Coastal Tower
pub(in crate::card::sets) static COASTAL_TOWER: CardRecord = CardRecord::new(
    cards::COASTAL_TOWER,
    "Coastal Tower",
    CardArt::new("d115dbff-e35b-495f-a1e3-19651895927e", "Don Hazeltine"),
    CardSet::Invasion,
    CardRules::new_land(&[]).with_abilities(&[
        abilities::enters_tapped("This land enters tapped."),
        AbilityDef::activated_mana(
            "{T}: Add {W} or {U}.",
            &[AbilityCostDef::TapSource],
            EffectDef::AddMana(AddManaEffectDef::choice(&[
                ManaColor::White,
                ManaColor::Blue,
            ])),
        ),
    ]),
);

pub(in crate::card::sets) static CARDS: &[&CardRecord] =
    &[&FACT_OR_FICTION, &OPT, &TSABOS_WEB, &COASTAL_TOWER];

pub(in crate::card::sets) static ADDITIONAL_PRINTINGS: &[PrintingRecord] = &[];
