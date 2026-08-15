//! Judgment cards used by the staged Premodern deck tranche.

use super::{CardRecord, PrintingRecord};
use crate::card::{
    AbilityCostDef, AbilityDef, AbilityTargetDef, AppliedEffectDef, CardArt, CardRules, CardSet,
    CardType, EffectDef, EffectDurationDef, EffectRecipientDef, ObjectPredicateDef, PlayerRelation,
    abilities, cards,
};
use crate::{TargetIndex, mana_cost};

static SAFEKEEPER_SHROUD: AbilityDef = abilities::shroud();

// JUD 133 — Sylvan Safekeeper
pub(in crate::card::sets) static SYLVAN_SAFEKEEPER: CardRecord = CardRecord::new(
    cards::SYLVAN_SAFEKEEPER,
    "Sylvan Safekeeper",
    CardArt::new("f1b8413f-c9fc-4cea-b416-a1fcf651b009", "Pete Venters"),
    CardSet::Judgment,
    CardRules::new_creature(mana_cost!("{G}"), &["Human", "Wizard"], 1, 1).with_ability(
        AbilityDef::activated_with_targets(
            "Sacrifice a land: Target creature you control gains shroud until end of turn.",
            &[AbilityCostDef::SacrificePermanent {
                object: ObjectPredicateDef::HasType(CardType::Land),
                controller: PlayerRelation::You,
            }],
            &[AbilityTargetDef::exactly_one_permanent(
                ObjectPredicateDef::All(&[
                    ObjectPredicateDef::HasType(CardType::Creature),
                    ObjectPredicateDef::ControlledBy(PlayerRelation::You),
                ]),
            )],
            EffectDef::Apply {
                recipient: EffectRecipientDef::Target(TargetIndex::PRIMARY),
                effect: AppliedEffectDef::GrantAbility(&SAFEKEEPER_SHROUD),
                duration: EffectDurationDef::UntilEndOfTurn,
            },
        ),
    ),
);

pub(in crate::card::sets) static CARDS: &[&CardRecord] = &[&SYLVAN_SAFEKEEPER];

pub(in crate::card::sets) static ADDITIONAL_PRINTINGS: &[PrintingRecord] = &[];
