//! Odyssey cards used by the staged Premodern deck tranche.

use super::{CardRecord, PrintingRecord};
use crate::card::{
    AbilityDef, CardArt, CardRules, CardSet, EffectDef, EffectRecipientDef, ObjectPredicateDef,
    PlayerRelation, ZoneKind, ZonePlacement, cards,
};
use crate::mana_cost;

// ODY 113 — Upheaval
pub(in crate::card::sets) static UPHEAVAL: CardRecord = CardRecord::new(
    cards::UPHEAVAL,
    "Upheaval",
    CardArt::new("9e201229-34a6-48c8-a07c-d8aefcf5f8a7", "Kev Walker"),
    CardSet::Odyssey,
    CardRules::new_sorcery(mana_cost!("{4}{U}{U}")).with_ability(AbilityDef::spell(
        "Return all permanents to their owners' hands.",
        EffectDef::MoveToZone {
            object: EffectRecipientDef::MatchingObjects {
                object: ObjectPredicateDef::Any,
                zones: &[ZoneKind::Battlefield],
                controller: PlayerRelation::Any,
            },
            zone: ZoneKind::Hand,
            placement: ZonePlacement::Top,
            controller: None,
        },
    )),
);

pub(in crate::card::sets) static CARDS: &[&CardRecord] = &[&UPHEAVAL];

pub(in crate::card::sets) static ADDITIONAL_PRINTINGS: &[PrintingRecord] = &[];
