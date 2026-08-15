//! Mercadian Masques cards used by the staged Premodern deck tranche.

use super::{CardRecord, PrintingRecord};
use crate::card::{
    AbilityCostDef, AbilityDef, AbilityTargetDef, CardArt, CardRules, CardSet, CardSupertype,
    CardType, EffectDef, EffectRecipientDef, ManaColor, ObjectPredicateDef, PlayerRelation,
    abilities, cards,
};
use crate::{TargetIndex, mana_cost};

static NONBASIC_LAND: ObjectPredicateDef = ObjectPredicateDef::All(&[
    ObjectPredicateDef::HasType(CardType::Land),
    ObjectPredicateDef::Not(&ObjectPredicateDef::Supertype(CardSupertype::Basic)),
]);

// MMQ 316 — Dust Bowl
pub(in crate::card::sets) static DUST_BOWL: CardRecord = CardRecord::new(
    cards::DUST_BOWL,
    "Dust Bowl",
    CardArt::new("75b03c30-c2b8-4207-b675-26c59c40a7e5", "Ben Thompson"),
    CardSet::MercadianMasques,
    CardRules::new_land(&[]).with_abilities(&[
        abilities::tap_for(ManaColor::Colorless),
        AbilityDef::activated_with_targets(
            "{3}, {T}, Sacrifice a land: Destroy target nonbasic land.",
            &[
                AbilityCostDef::Mana(mana_cost!("{3}")),
                AbilityCostDef::TapSource,
                AbilityCostDef::SacrificePermanent {
                    object: ObjectPredicateDef::HasType(CardType::Land),
                    controller: PlayerRelation::You,
                },
            ],
            &[AbilityTargetDef::exactly_one_permanent(NONBASIC_LAND)],
            EffectDef::destroy_target(TargetIndex::PRIMARY, true),
        ),
    ]),
);

// MMQ 324 — Rishadan Port
pub(in crate::card::sets) static RISHADAN_PORT: CardRecord = CardRecord::new(
    cards::RISHADAN_PORT,
    "Rishadan Port",
    CardArt::new("477a1f53-5cdf-4b45-b584-2e36b31a3fdb", "Jerry Tiritilli"),
    CardSet::MercadianMasques,
    CardRules::new_land(&[]).with_abilities(&[
        abilities::tap_for(ManaColor::Colorless),
        AbilityDef::activated_with_targets(
            "{1}, {T}: Tap target land.",
            &[
                AbilityCostDef::Mana(mana_cost!("{1}")),
                AbilityCostDef::TapSource,
            ],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::HasType(CardType::Land),
            )],
            EffectDef::Tap {
                object: EffectRecipientDef::Target(TargetIndex::PRIMARY),
            },
        ),
    ]),
);

pub(in crate::card::sets) static CARDS: &[&CardRecord] = &[&DUST_BOWL, &RISHADAN_PORT];

pub(in crate::card::sets) static ADDITIONAL_PRINTINGS: &[PrintingRecord] = &[];
